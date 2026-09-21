"""Native probe runner against lodge.profiles.codec_inspect (authored inputs only)."""
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import profiles, sessions  # noqa: E402
from lodge.store import FrontendError  # noqa: E402

DRIVER, PROBE = sys.argv[1], sys.argv[2]
POSIX = os.name == 'posix'


def native(*arguments, data=b'', env=None):
    environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
    environment.update(env or {})
    done = subprocess.run([DRIVER, *arguments], input=data, capture_output=True, timeout=60,
                          env=environment, check=True)
    kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
    return kind, rest


def save(name=b'Hunter', registration=3):
    content = bytearray(1660)
    content[:len(name)] = name
    content[128:132] = registration.to_bytes(4, 'little')
    return bytes(content)


class ProbeProcess(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        os.environ.pop('C2_PROFILE_PROBE', None)

    def tearDown(self):
        self.temp.cleanup()

    def helper(self, body):
        if not POSIX:
            self.skipTest('authored script helpers need a POSIX shebang')
        path = self.root / 'helper'
        path.write_text(f'#!{sys.executable}\nimport sys, time, os\n{body}\n')
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return str(path)

    def same(self, content, kind, probe, dialect='unknown'):
        expected = profiles.codec_inspect(content, kind, probe, dialect)
        got = native('inspect', kind, dialect, probe or '-', '15000', data=content)
        self.assertEqual(got[0], 'ok', got)
        self.assertEqual(json.loads(got[1]), expected)
        self.assertEqual(list(json.loads(got[1])), list(expected))

    def test_real_probe_matches_reference(self):
        self.same(save(), 'sav', PROBE)
        self.same(save(b'\xe9\xff' + b'x' * 126), 'sav', PROBE)
        self.same(bytes(7176), 'sab', PROBE)
        self.same(bytes(7176), 'other', PROBE)

    def test_short_circuits_never_run_helper(self):
        missing = str(self.root / 'absent')
        self.same(save(), 'sav', missing, 'iceage-triassic')
        self.same(b'abc', 'sav', missing)
        self.same(save(), 'sab', missing)
        self.same(save(), 'sav', None)

    def test_environment_fallback(self):
        got = native('inspect', 'sav', 'unknown', '-', '15000', data=save(), env={'C2_PROFILE_PROBE': PROBE})
        self.assertEqual(json.loads(got[1]), profiles.codec_inspect(save(), 'sav', PROBE))

    def test_failure_exit_code(self):
        helper = self.helper('sys.exit(7)')
        with self.assertRaises(FrontendError) as caught:
            profiles.codec_inspect(save(), 'sav', helper)
        self.assertEqual(native('inspect', 'sav', 'unknown', helper, '15000', data=save()),
                         ('frontend', str(caught.exception)))

    def test_signal_exit_code(self):
        helper = self.helper('os.kill(os.getpid(), 9)')
        with self.assertRaises(FrontendError) as caught:
            profiles.codec_inspect(save(), 'sav', helper)
        self.assertEqual(native('inspect', 'sav', 'unknown', helper, '15000', data=save()),
                         ('frontend', str(caught.exception)))

    def test_timeout_kills_and_reaps_child(self):
        marker = self.root / 'pid'
        helper = self.helper(f'open({str(marker)!r}, "w").write(str(os.getpid()))\ntime.sleep(60)')
        started = time.monotonic()
        self.assertEqual(native('inspect', 'sav', 'unknown', helper, '700', data=save()),
                         ('timeout', 'codec helper timed out'))
        self.assertLess(time.monotonic() - started, 20)
        pid = int(marker.read_text())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)

    def test_helper_ignoring_stdin_does_not_kill_caller(self):
        helper = self.helper('sys.stdin.close()\nprint(\'{"layout":"x","codec_roundtrip_exact":false}\')')
        got = native('inspect', 'sav', 'unknown', helper, '15000', data=save())
        self.assertEqual(got[0], 'ok', got)

    def test_invalid_json_and_hex_are_not_frontend_errors(self):
        self.assertEqual(native('inspect', 'sav', 'unknown', self.helper('print("nope")'), '15000', data=save())[0], 'json')
        helper = self.helper('print(\'{"name_hex":"zz"}\')')
        self.assertEqual(native('inspect', 'sav', 'unknown', helper, '15000', data=save())[0], 'invalid')

    def test_missing_executable_is_oserror(self):
        self.assertEqual(native('inspect', 'sav', 'unknown', str(self.root / 'absent'), '15000', data=save())[0], 'oserror')

    def test_large_bidirectional_streams_do_not_deadlock(self):
        program = ('import sys\nd = sys.stdin.buffer.read()\nsys.stderr.buffer.write(d[::-1])\n'
                   'sys.stdout.buffer.write(d)\nsys.exit(5)')
        data = bytes(range(256)) * 8192
        kind, rest = native('run', '30000', str(1 << 24), sys.executable, '-c', program, data=data)
        self.assertEqual(kind, 'ok')
        code, out, err = rest.split(' ')
        self.assertEqual((code, bytes.fromhex(out), bytes.fromhex(err)), ('5', data, data[::-1]))

    def test_arguments_are_literal_not_shell(self):
        literal = sessions.LITERAL_ARGUMENT + ' "quoted" \\tail\\'
        kind, rest = native('run', '30000', '65536', sys.executable, '-c',
                            'import sys; sys.stdout.write(sys.argv[1])', literal)
        self.assertEqual((kind, bytes.fromhex(rest.split(' ')[1]).decode()), ('ok', literal))

    def test_output_limit_refuses_without_truncating(self):
        kind, _ = native('run', '30000', '1000', sys.executable, '-c', 'print("x" * 100000)')
        self.assertEqual(kind, 'limit')

    def test_codec_evidence(self):
        self.assertEqual(json.loads(native('evidence', PROBE)[1]), sessions.codec_evidence(PROBE))
        self.assertEqual(native('evidence', '-'), ('frontend', 'session requires an explicit profile codec helper'))
        self.assertEqual(native('evidence', str(self.root)), ('frontend', 'explicit executable must be a regular file'))
        self.assertEqual(native('evidence', str(self.root / 'absent'))[0], 'oserror')
        self.assertEqual(json.loads(native('evidence', '-', env={'C2_PROFILE_PROBE': PROBE})[1]),
                         sessions.codec_evidence(PROBE))


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
