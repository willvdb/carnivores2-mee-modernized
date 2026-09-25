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

DRIVER, PROBE, HELPER = sys.argv[1:4]
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

    def test_compiled_argument_fidelity(self):
        args = ['', 'a b', 'tabs\there', '"', '\\', 'space tail\\', 'x\\"y', '; $() &', 'plain']
        kind, rest = native('run', '10000', '65536', HELPER, 'args', *args)
        self.assertEqual(kind, 'ok', rest)
        self.assertEqual(bytes.fromhex(rest.split(' ')[1]), b''.join(a.encode() + b'\0' for a in args))

    def test_compiled_simultaneous_io_and_nonzero_exit(self):
        data = bytes(range(256)) * 8192
        kind, rest = native('run', '15000', str(1 << 24), HELPER, 'streams', data=data)
        self.assertEqual(kind, 'ok', rest[:100])
        code, out, err = rest.split(' ')
        self.assertEqual(code, '7')
        self.assertEqual(bytes.fromhex(out), b'x' * (1 << 21) + data)
        self.assertEqual(bytes.fromhex(err), b'x' * (1 << 21))

    def test_compiled_early_stdin_close(self):
        kind, rest = native('run', '5000', '65536', HELPER, 'early-close', data=b'x' * (1 << 22))
        self.assertEqual((kind, rest), ('ok', '9 ' + b'closed'.hex() + ' '))

    def test_compiled_overflow_and_timeout(self):
        self.assertEqual(native('run', '5000', '1000', HELPER, 'overflow')[0], 'limit')
        start = time.monotonic()
        self.assertEqual(native('run', '1000', '65536', HELPER, 'hold')[0], 'timeout')
        self.assertLess(time.monotonic() - start, 5)

    def test_failure_after_spawn_cleans_up(self):
        start = time.monotonic()
        for _ in range(5):
            self.assertEqual(native('io-failure', HELPER), ('ok', b'after\0'.hex()))
        self.assertLess(time.monotonic() - start, 5)

    def test_inheritance_isolation(self):
        self.assertEqual(native('inheritance', HELPER), ('ok', '0'))

    @unittest.skipUnless(POSIX, 'POSIX descriptor allocation regression')
    def test_initially_closed_standard_descriptors(self):
        self.assertEqual(native('closed-stdio', HELPER), ('ok', b'stdio\0'.hex()))

    def test_descendant_pipe_deadline(self):
        marker = self.root / 'descendant'
        start = time.monotonic()
        try:
            self.assertEqual(native('run', '1500', '65536', HELPER, 'descendant', str(marker))[0], 'timeout')
            self.assertLess(time.monotonic() - start, 5)
            self.assertTrue(marker.exists())
            if not POSIX:
                import ctypes
                kernel = ctypes.WinDLL('kernel32', use_last_error=True)
                kernel.OpenProcess.restype = ctypes.c_void_p
                kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
                kernel.CloseHandle.argtypes = [ctypes.c_void_p]
                handle = kernel.OpenProcess(0x100000, False, int(marker.read_text()))
                if handle:
                    try:
                        self.assertEqual(kernel.WaitForSingleObject(handle, 3000), 0)
                    finally:
                        kernel.CloseHandle(handle)
        finally:
            # POSIX intentionally retains Python's direct-child-only cleanup.
            if POSIX and marker.exists():
                try:
                    os.kill(int(marker.read_text()), 9)
                except ProcessLookupError:
                    pass

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

    def test_inspect_set_through_helper_matches_reference(self):
        root = self.root.resolve() / 'game'
        root.mkdir()
        (root / 'trophy03.sav').write_bytes(save(registration=3))
        (root / 'trophy03.sab').write_bytes(bytes(7176))
        (root / 'trophy04.sav').write_bytes(save(registration=6))
        (root / 'TROPHY05.SAV').write_bytes(b'short')
        (root / 'trophy03.bak').write_bytes(b'companion')
        for dialect in ('unknown', 'iceage-triassic'):
            for probe in (PROBE, None):
                for state in profiles.inventory(root):
                    expected = profiles.inspect_set(root, state, probe, dialect)
                    got = native('inspect-set', str(root), state['key'], dialect, probe or '-')
                    self.assertEqual(got[0], 'ok', got)
                    self.assertEqual(json.dumps(json.loads(got[1])), json.dumps(expected))
        codes = [d['code'] for d in profiles.inspect_set(root, profiles.inventory(root)[1], PROBE)['diagnostics']]
        self.assertIn('registration-mismatch', codes)

    def test_inspect_bytes_matches_reference(self):
        from lodge import session_io
        root = self.root.resolve() / 'state'
        root.mkdir()
        (root / 'trophy03.sav').write_bytes(save(registration=5))
        (root / 'trophy03.sab').write_bytes(b'bad')
        (root / 'trophy04.sav').write_bytes(save(registration=4))
        _, blobs = session_io.capture(root)
        decoded, diagnostics = session_io.inspect_bytes(blobs, 3, PROBE)
        self.assertEqual([d['code'] for d in diagnostics], ['unreadable-state', 'registration-mismatch'])
        got = native('inspect-bytes', str(root), '3', PROBE)
        self.assertEqual(got[0], 'ok', got)
        self.assertEqual(json.dumps(json.loads(got[1])), json.dumps({'decoded': decoded, 'diagnostics': diagnostics}))

    def test_codec_evidence(self):
        self.assertEqual(json.loads(native('evidence', PROBE)[1]), sessions.codec_evidence(PROBE))
        self.assertEqual(native('evidence', '-'), ('frontend', 'session requires an explicit profile codec helper'))
        self.assertEqual(native('evidence', str(self.root)), ('frontend', 'explicit executable must be a regular file'))
        self.assertEqual(native('evidence', str(self.root / 'absent'))[0], 'oserror')
        self.assertEqual(json.loads(native('evidence', '-', env={'C2_PROFILE_PROBE': PROBE})[1]),
                         sessions.codec_evidence(PROBE))


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
