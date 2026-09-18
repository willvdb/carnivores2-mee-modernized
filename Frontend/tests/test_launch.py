import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from lodge.discovery import register
from lodge.launch import prepare, simulated_return
from lodge.profiles import associate
from lodge.store import Store, hunter
from support import game
from test_profiles import save_bytes, room_bytes

SCRIPT = """weapons {
{
 name = 'Synthetic weapon'
}
}
characters {
{
 name = 'Synthetic group'
 ai = 10
}
}
prices {
 area = 5
 dino = 10
 weapon = 20
}
"""


@unittest.skipUnless(os.environ.get('C2_PROFILE_PROBE'), 'build codec helper and set C2_PROFILE_PROBE')
class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = game(self.temp.name)
        (self.root / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (self.root / 'trophy00.sav').write_bytes(save_bytes())
        (self.root / 'trophy00.sab').write_bytes(room_bytes())
        self.store = Store(Path(self.temp.name) / 'Lodge')
        with self.store.transaction() as data:
            hunter_id = hunter(data, 'create', name='Hunter')['id']
            instance = register(data, self.root, dialect='c2-classic')
            self.association = associate(self.store, data, hunter_id, instance['id'], 'trophy00', 'personal')['id']

    def test_dry_run_exact_argv_and_no_native_mutation(self):
        before = (self.root / 'trophy00.sav').read_bytes()
        result = prepare(self.store, self.store.read(), self.association, 'areas:0', ['licenses:0'], ['weapons:0'])
        self.assertEqual(result['candidate_argv'], ['reg=0', 'prj=huntdat/areas/area1', 'din=1', 'wep=1', 'dtm=1'])
        self.assertFalse(result['process_launch_allowed'])
        self.assertEqual(result['affordability']['listed_selection_requirement'], 35)
        self.assertEqual(result['affordability']['native_score'], 100)
        self.assertEqual(result['capabilities']['launch_tested'], 'unknown')
        self.assertEqual(before, (self.root / 'trophy00.sav').read_bytes())

    def test_selection_validation_and_observer_exception(self):
        data = self.store.read()
        normal = prepare(self.store, data, self.association, 'areas:0')
        self.assertIn('empty-hunt-selection', [d['code'] for d in normal['diagnostics']])
        observer = prepare(self.store, data, self.association, 'areas:0', mode='observer')
        self.assertIn('-observ', observer['candidate_argv'])
        bad = prepare(self.store, data, self.association, 'areas:999', ['licenses:0', 'licenses:0'], ['weapons:999'])
        codes = [d['code'] for d in bad['diagnostics']]
        self.assertIn('unknown-selection', codes)
        self.assertIn('duplicate-selection', codes)
        self.assertEqual(bad['candidate_argv'], [])

    def test_return_refresh_retains_provenance(self):
        data = self.store.read()
        before = data['associations'][self.association]['files']
        (self.root / 'trophy00.sav').write_bytes(save_bytes(score=175))
        result = simulated_return(self.store, data, self.association)
        self.assertEqual(result['state']['status'], 'changed-state')
        self.assertEqual(next(f['decoded']['score'] for f in result['state']['files'] if f['kind'] == 'sav'), 175)
        self.assertEqual(data['associations'][self.association]['files'], before)
        self.assertFalse(result['process_executed'])
        self.assertEqual(result['hunt_save_round_trip_validated'], 'unknown')

    def test_update_invalidates_request(self):
        (self.root / 'HUNTDAT/_RES.TXT').write_text('weapons {}\ncharacters {}\n// changed\n')
        result = prepare(self.store, self.store.read(), self.association, 'areas:0', mode='observer')
        self.assertEqual(result['candidate_argv'], [])
        self.assertIn('revision-review-required', [d['code'] for d in result['diagnostics']])

    def test_cli_end_to_end_and_clean_errors(self):
        cli = Path(__file__).resolve().parents[1] / 'frontend.py'
        def run(*args):
            result = subprocess.run([sys.executable, str(cli), '--store', str(self.store.directory), *args], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return json.loads(result.stdout)
        self.assertEqual(len(run('hunter', 'list')['hunters']), 1)
        self.assertEqual(len(run('expedition', 'list')), 1)
        self.assertEqual(run('launch-dry-run', self.association, '--area', 'areas:0', '--mode', 'observer')['result'], 'dry-run-only')
        self.assertFalse(run('simulate-return', self.association)['process_executed'])
        self.assertEqual(run('host-settings', '--json', '{"display":{"mode":"borderless"}}')['display']['mode'], 'borderless')
        failed = subprocess.run([sys.executable, str(cli), '--store', str(self.store.directory), 'hunter', 'select', 'missing'], capture_output=True, text=True)
        self.assertEqual(failed.returncode, 2)
        self.assertIn('error', json.loads(failed.stderr))


if __name__ == '__main__':
    unittest.main()
