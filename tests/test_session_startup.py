"""Asset-free subprocess checks of the actual engine's earliest startup boundary."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ENGINE = Path(sys.argv.pop(1)).resolve(strict=True)
EXIT_PROBE = Path(sys.argv.pop(1)).resolve(strict=True)


def inventory(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


class Startup(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='c2 startup spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        for name in ('content/HuNtDaT', 'engine', 'source', 'baseline',
                     'work/state', 'work/config', 'work/output'):
            (self.root / name).mkdir(parents=True)
        self.engine = self.root / 'engine' / ENGINE.name
        shutil.copy2(ENGINE, self.engine)
        self.exit_probe = self.engine.parent / EXIT_PROBE.name
        shutil.copy2(EXIT_PROBE, self.exit_probe)
        for dll in ENGINE.parent.glob('*.dll'):
            shutil.copy2(dll, self.engine.parent / dll.name)
        self.content = self.root / 'content'
        for path in ('content/config.cfg', 'content/trophy00.sav', 'content/render.log',
                     'content/HuNtDaT/sentinel', 'engine/config.cfg'):
            (self.root / path).write_bytes(b'original sentinel: ' + path.encode())
        # Original zero-valued fixed-width codec fixtures; no personal state.
        for folder in ('source', 'baseline', 'work/state'):
            for suffix, size in (('sav', 1660), ('sab', 7176)):
                (self.root / folder / ('trophy00.' + suffix)).write_bytes(bytes(size))
        self.protected = {p: inventory(self.root / p) for p in ('content', 'engine', 'source', 'baseline')}
        self.args = ['--session-contract=1', '--session-slot=0',
                     '--session-root=' + str(self.root / 'work'),
                     '--session-source=' + str(self.root / 'source'),
                     '--session-baseline=' + str(self.root / 'baseline')]

    def tearDown(self):
        for path, before in self.protected.items():
            self.assertEqual(inventory(self.root / path), before, path)

    def run_engine(self, args):
        return subprocess.run([str(self.engine), *args], cwd=self.content,
                              env={**os.environ, 'SDL_VIDEODRIVER': 'c2-intentionally-unavailable'},
                              stdin=subprocess.DEVNULL, capture_output=True, timeout=15)

    def test_query_is_machine_readable_and_has_no_filesystem_side_effects(self):
        before = inventory(self.root)
        result = self.run_engine(['--session-capabilities'])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)['version'], 1)
        self.assertEqual(inventory(self.root), before)

    def test_incomplete_duplicate_unknown_and_ambiguous_options_fail_before_logs(self):
        for args in (self.args[:-1], self.args + ['--session-slot=0'],
                     self.args + ['reg=0'], ['--Session-root=x'],
                     ['--session-capabilities', '-list-displays']):
            with self.subTest(args=args):
                before = inventory(self.root)
                result = self.run_engine(args)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn(b'Session setup failed:', result.stderr)
                self.assertEqual(inventory(self.root), before)

    def test_missing_profile_does_not_repair_or_fall_back(self):
        (self.root / 'work/state/trophy00.sav').unlink()
        result = self.run_engine(self.args)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse((self.root / 'work/state/trophy00.sav').exists())
        self.assertEqual(list((self.root / 'work/output').iterdir()), [])

    def test_baseline_mismatch_never_gets_repaired(self):
        (self.root / 'work/state/trophy00.sav').write_bytes(b'bad baseline')
        result = self.run_engine(self.args)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual((self.root / 'work/state/trophy00.sav').read_bytes(), b'bad baseline')

    @unittest.skipUnless(sys.platform == 'linux', 'SDL-only startup failure injection')
    def test_platform_failure_uses_only_session_logs_and_returns_failure(self):
        result = self.run_engine(self.args)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual({p.name for p in (self.root / 'work/output').iterdir()},
                         {'render.log', 'carnivor.log'})
        self.assertEqual(list((self.root / 'work/config').iterdir()), [])

    def test_production_exit_closes_logs_and_preserves_failures_without_destructors(self):
        for mode, expected in (('clean', 0), ('io', 3), ('fatal', 1), ('early', 1)):
            with self.subTest(mode=mode):
                for log in (self.root / 'work/output').iterdir():
                    log.unlink()
                result = subprocess.run([str(self.exit_probe), *self.args, mode],
                                        cwd=self.content, capture_output=True, timeout=10)
                self.assertEqual(result.returncode, expected, result.stderr)
                self.assertIn(b'Log closed', (self.root / 'work/output/carnivor.log').read_bytes())

    @unittest.skipUnless(os.name == 'nt', 'Windows junction regression')
    def test_junction_is_rejected_before_startup(self):
        junction = self.root / 'junction'
        subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(self.root / 'work')],
                       check=True, capture_output=True)
        self.addCleanup(lambda: junction.rmdir())
        args = [a for a in self.args if not a.startswith('--session-root=')]
        result = self.run_engine(args + ['--session-root=' + str(junction)])
        self.assertEqual(result.returncode, 2, result.stderr)


unittest.main(verbosity=2)
