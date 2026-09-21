"""Native refresh-state against lodge.profiles.refresh_association (disposable stores)."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
from lodge.discovery import register  # noqa: E402
from lodge.profiles import associate, refresh_association  # noqa: E402
from lodge.store import FrontendError, Store, hunter  # noqa: E402
from support import game  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

DRIVER, PROBE = sys.argv[1], sys.argv[2]


def native(directory, identity, probe=PROBE):
    environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
    done = subprocess.run([DRIVER, 'refresh-state', str(directory), identity, probe or '-'],
                          capture_output=True, timeout=60, env=environment, check=True)
    kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
    return kind, rest


class RefreshState(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        os.environ.pop('C2_PROFILE_PROBE', None)
        self.base = Path(self.temp.name).resolve()
        self.root = game(self.base)
        (self.root / 'trophy00.sav').write_bytes(save_bytes())
        (self.root / 'trophy00.sab').write_bytes(room_bytes())
        self.store = Store(self.base / 'Lodge')
        with self.store.transaction() as data:
            hunter_id = hunter(data, 'create', name='Hunter')['id']
            instance = register(data, self.root, dialect='c2-classic')
            self.referenced = associate(self.store, data, hunter_id, instance['id'], 'trophy00', 'personal',
                                        'referenced', PROBE)['id']

    def both(self, identity, probe=PROBE):
        """Run the reference on one copy of the store and native on another."""
        twin = self.base / 'Twin'
        shutil.rmtree(twin, ignore_errors=True)
        shutil.copytree(self.store.directory, twin)
        try:
            with self.store.transaction() as data:
                expected = ('ok', refresh_association(self.store, data, identity, probe))
        except FrontendError as error:
            expected = ('frontend', str(error))
        kind, rest = native(twin, identity, probe)
        if expected[0] == 'ok':
            self.assertEqual(kind, 'ok', rest)
            self.assertEqual(json.dumps(json.loads(rest)), json.dumps(expected[1]))
        else:
            self.assertEqual((kind, rest), expected)
        self.assertEqual((twin / 'lodge.json').read_bytes(), (self.store.directory / 'lodge.json').read_bytes())
        self.assertEqual(sorted(p.name for p in twin.iterdir()), sorted(p.name for p in self.store.directory.iterdir()))
        return expected

    def test_unchanged_changed_missing_and_helperless(self):
        self.assertEqual(self.both(self.referenced)[1]['status'], 'unchanged-state')
        self.assertEqual(self.both(self.referenced, None)[1]['status'], 'unchanged-state')
        (self.root / 'trophy00.sav').write_bytes(save_bytes()[:-1] + b'\x7f')
        result = self.both(self.referenced)[1]
        self.assertEqual((result['status'], result['diagnostics'][-1]['code']), ('changed-state', 'state-drift'))
        (self.root / 'trophy00.sav').unlink()
        (self.root / 'trophy00.sab').unlink()
        self.assertEqual(self.both(self.referenced)[1]['status'], 'missing-state')

    def test_managed_copy(self):
        with self.store.transaction() as data:
            hunter_id = next(iter(data['hunters']))
            instance_id = next(iter(data['instances']))
            managed = associate(self.store, data, hunter_id, instance_id, 'trophy00', 'personal', 'managed', PROBE)
        if managed.get('authority') == 'managed-state-history':
            # Generation resolution over the mutable manifest is not wired natively:
            # the refusal must be explicit and must write nothing.
            before = (self.store.directory / 'lodge.json').read_bytes()
            self.assertEqual(native(self.store.directory, managed['id'])[0], 'not-implemented')
            self.assertEqual((self.store.directory / 'lodge.json').read_bytes(), before)
            self.assertFalse((self.store.directory / 'lodge.lock').exists())
        else:
            self.assertEqual(self.both(managed['id'])[1]['status'], 'unchanged-state')
            # The managed copy is observed, not the installation.
            (self.root / 'trophy00.sav').write_bytes(save_bytes()[:-1] + b'\x7f')
            self.assertEqual(self.both(managed['id'])[1]['status'], 'unchanged-state')

    def test_unknown_association_writes_nothing(self):
        before = (self.store.directory / 'lodge.json').read_bytes()
        self.assertEqual(self.both('0a9b8c7d-1234-4abc-8def-001122334455'), ('frontend', 'unknown association ID'))
        self.assertEqual((self.store.directory / 'lodge.json').read_bytes(), before)
        self.assertFalse((self.store.directory / 'lodge.lock').exists())


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
