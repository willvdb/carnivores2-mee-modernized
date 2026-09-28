import copy
import unittest

from lodge.genesis import GENESIS_REVISION, POLICY_ID, observer_policy
from lodge.store import FrontendError


class GenesisPolicyTests(unittest.TestCase):
    def setUp(self):
        # Authored policy observations, not copied content or runtime evidence.
        self.catalog = {'dialect': {'observed': 'mee-newer', 'effective': 'mee-newer'},
                        'areas': [{'id': f'areas:{i}', 'launch_stem': f'area{i+1}', 'price': 5}
                                  for i in range(8)],
                        'licenses': [None] * 9, 'weapons': [None] * 8,
                        'equipment': [None] * 4, 'physical_maps': [None] * 9,
                        'score_modifier_observations': [], 'diagnostics': []}
        self.selection = {'area': 'areas:0', 'mode': 'observer', 'time_of_day': 1,
                          'licenses': [], 'weapons': [], 'equipment': []}

    def plan(self, revision=None, slot=0, score=100):
        return observer_policy(revision or GENESIS_REVISION, self.catalog, slot, self.selection, score)

    def test_observer_exact_args_and_independent_capabilities(self):
        result = self.plan()
        self.assertEqual(result['adapter'], POLICY_ID)
        self.assertEqual(result['candidate_argv'], ['reg=0', 'prj=huntdat/areas/area1',
            'din=0', 'wep=0', 'dtm=1', '-observ', 'smod=0.85,0.70,0.80,1.0,1.25,1.0'])
        self.assertFalse(result['process_launch_allowed'])
        self.assertEqual(result['capabilities']['genesis_policy'], 'structurally-validated')
        self.assertFalse(result['capabilities']['engine_process_executed'])
        self.assertFalse(result['capabilities']['hunt_save_round_trip_validated'])

    def test_unpinned_revision_including_counts_refused(self):
        for field, value in (('sha256', '0' * 64), ('file_count', 343), ('byte_count', 768073100)):
            with self.subTest(field=field), self.assertRaises(FrontendError):
                self.plan({**GENESIS_REVISION, field: value})

    def test_nonobserver_loadouts_and_equipment_refused(self):
        for field, value in (('mode', 'hunt'), ('mode', 'survival'), ('licenses', ['licenses:0']),
                             ('weapons', ['weapons:0']), ('equipment', ['equipment:0'])):
            original = copy.deepcopy(self.selection)
            self.selection[field] = value
            with self.subTest(field=field), self.assertRaises(FrontendError):
                self.plan()
            self.selection = original

    def test_slot_time_and_area_path_bounds(self):
        for time in (0, 1, 2):
            self.selection['time_of_day'] = time
            self.assertIn(f'dtm={time}', self.plan()['candidate_argv'])
        self.selection['time_of_day'] = 3
        with self.assertRaises(FrontendError):
            self.plan()
        self.selection['time_of_day'] = 1
        for slot in (-1, 8, True):
            with self.assertRaises(FrontendError):
                self.plan(slot=slot)
        for stem in ('../area1', 'area1;evil', '/area1', None):
            self.catalog['areas'][0]['launch_stem'] = stem
            with self.assertRaises(FrontendError):
                self.plan()

    def test_no_guessed_dialect_score_or_modifier_policy(self):
        with self.assertRaises(FrontendError):
            self.plan(score=4)
        self.catalog['dialect']['observed'] = 'mee-older'
        with self.assertRaises(FrontendError):
            self.plan()
        self.catalog['dialect']['observed'] = 'mee-newer'
        self.catalog['score_modifier_observations'] = [{'name': 'accessories'}]
        with self.assertRaises(FrontendError):
            self.plan()

    def test_external_area_candidate_and_no_synthetic_runtime_claim(self):
        self.selection['area'] = 'areas:5'
        self.catalog['areas'][5]['launch_stem'] = 'external'
        result = self.plan()
        self.assertIn('prj=huntdat/areas/external', result['candidate_argv'])
        self.assertEqual(result['capabilities']['modern_engine_compatibility'], 'unknown')


if __name__ == '__main__':
    unittest.main()
