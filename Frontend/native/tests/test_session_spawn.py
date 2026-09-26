"""Sessions-level spawn failure: the journal's spawn-failed diagnostic is str(OSError).

The synthetic fixture next to the sessions driver is copied into a disposable
directory with the driver, prepared (evidence pins the copy), then stripped of
its execute bit: preflight still matches (same path, same bytes), so the
launch reaches Popen and fails there. The reference message is what Python's
Popen raises for that same file; the journal is otherwise the reference shape.
A missing cwd cannot reach the spawn through run_session (preflight captures
work/state first); that case is covered at the supervisor level.
"""
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
from subprocess import Popen
import sys
import tempfile
import unittest

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
DRIVER, PROBE, CHILD = (str(Path(a).resolve()) for a in sys.argv[1:4])
from lodge.discovery import register  # noqa: E402
from lodge.profiles import associate  # noqa: E402
from lodge.session_io import read_journal, session_root  # noqa: E402
from lodge.store import Store, hunter  # noqa: E402
from support import game  # noqa: E402
from test_launch import SCRIPT  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

POSIX = os.name == 'posix'


@unittest.skipUnless(POSIX, 'the execute-bit spawn failure is POSIX')
class SessionSpawn(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        root = game(self.base)
        (root / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (root / 'trophy00.sav').write_bytes(save_bytes())
        (root / 'trophy00.sab').write_bytes(room_bytes())
        self.store = Store(self.base / 'Lodge')
        with self.store.transaction() as data:
            h = hunter(data, 'create', name='Synthetic test hunter')
            i = register(data, root, dialect='c2-classic')
            self.association = associate(self.store, data, h['id'], i['id'], 'trophy00', 'personal', 'managed', PROBE)['id']
        binaries = self.base / 'bin'
        binaries.mkdir()
        self.driver = binaries / Path(DRIVER).name
        self.child = binaries / Path(CHILD).name
        shutil.copy2(DRIVER, self.driver)
        shutil.copy2(CHILD, self.child)

    def native(self, command, *args, stdin=None):
        environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
        done = subprocess.run([str(self.driver), command, *args], input=json.dumps(stdin).encode() if stdin is not None else b'',
                              capture_output=True, timeout=120, env=environment, check=True)
        kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
        return kind, json.loads(rest) if kind == 'ok' else rest

    def test_non_executable_fixture_records_the_reference_spawn_message(self):
        kind, prepared = self.native('prepare-synthetic', str(self.store.directory), self.association, 'areas:0', 'unchanged', PROBE,
                                     stdin={'time_of_day': 1, 'timeout': 5})
        self.assertEqual(kind, 'ok', prepared)
        self.assertEqual(prepared['execution']['executable']['path'], str(self.child))
        self.child.chmod(stat.S_IRUSR | stat.S_IWUSR)
        with self.assertRaises(PermissionError) as caught:
            Popen(prepared['execution']['argv'], executable=prepared['execution']['executable']['path'],
                  cwd=prepared['execution']['cwd'], shell=False, stdin=subprocess.DEVNULL,
                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True, start_new_session=True)
        kind, journal = self.native('run', str(self.store.directory), prepared['id'], PROBE)
        self.assertEqual(kind, 'ok', journal)
        self.assertEqual(journal['state'], 'failed')
        self.assertEqual(journal['diagnostics'], [{'code': 'spawn-failed', 'message': str(caught.exception)}])
        self.assertEqual(journal['diagnostics'][0]['message'], f"[Errno 13] Permission denied: {str(self.child)!r}")
        self.assertEqual([e['state'] for e in journal['transitions']], ['prepared', 'launching', 'failed'])
        self.assertIsNone(journal['process'])
        self.assertEqual(read_journal(self.store, journal['id']), journal)
        self.assertEqual(list((session_root(self.store, journal['id']) / 'logs').iterdir()), [])


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
