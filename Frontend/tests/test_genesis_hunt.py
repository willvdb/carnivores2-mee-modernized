"""Policy characterization uses authored observations; never fake content hashes."""
import copy
import json
import os
from pathlib import Path
import subprocess
import unittest

from lodge.genesis import GENESIS_REVISION, observer_policy
from lodge.genesis_hunt import hunt_policy, SCORE_MODIFIERS
from lodge.store import FrontendError


def observations():
    return {'dialect': {'observed': 'mee-newer', 'effective': 'mee-newer'},
            'areas': [{'id': f'areas:{i}', 'ordinal': i, 'launch_stem': f'area{i+1}', 'price': 5}
                      for i in range(8)],
            'licenses': [{'id': f'licenses:{i}', 'ordinal': i, 'ai': 10,
                          'label': 'Grouped selection', 'price': 10} for i in range(9)],
            'weapons': [{'id': f'weapons:{i}', 'ordinal': i, 'label': 'Weapon', 'price': 15}
                        for i in range(8)],
            'equipment': [None]*4, 'physical_maps': [None]*9,
            'score_modifier_observations': [], 'diagnostics': []}


def selection():
    return {'area': 'areas:0', 'mode': 'hunt', 'time_of_day': 1,
            'licenses': ['licenses:0'], 'weapons': ['weapons:0'], 'equipment': []}


class HuntPolicyTests(unittest.TestCase):
    def setUp(self):
        self.catalog, self.selection = observations(), selection()

    def policy(self, score=100, revision=None, slot=0):
        return hunt_policy(revision or GENESIS_REVISION, self.catalog, slot, self.selection, score)

    def test_all_single_bits_with_actual_production_argument_consumer(self):
        probe = os.environ.get('C2_LAUNCH_ARGUMENT_PROBE')
        self.assertTrue(probe and Path(probe).is_file(), 'build mandatory production argument probe')
        for license in range(9):
            for weapon in (0, 7):
                for time in (0, 1, 2):
                    self.selection.update(licenses=[f'licenses:{license}'], weapons=[f'weapons:{weapon}'], time_of_day=time)
                    p = self.policy()
                    expected = ['reg=0', 'prj=huntdat/areas/area1', f'din={1<<license}',
                                f'wep={1<<weapon}', f'dtm={time}', SCORE_MODIFIERS]
                    self.assertEqual(p['candidate_argv'], expected)
                    result = subprocess.run([probe, *expected], capture_output=True, check=True)
                    self.assertEqual(json.loads(result.stdout), {'din': 1 << (license+10),
                        'wep': 1 << weapon, 'time': time, 'observer': False, 'equipment': False,
                        'modifiers': [0.85,0.70,0.80,1.0,1.25,1.0]})
        # Duplicate AI attributes never collapse independent catalog identities.
        self.assertEqual(len({e['ai'] for e in self.catalog['licenses']}), 1)

    def test_eligibility_is_sum_without_debit_or_rank_normalization(self):
        before = copy.deepcopy(self.catalog)
        self.assertEqual(self.policy(30)['score_requirement'], 30)
        self.assertEqual(self.policy(2147483647)['score_mutation'], 'none')
        self.assertEqual(self.catalog, before)
        for score in (29, -1, True, 30.0, '100', None, 2147483648):
            with self.subTest(score=score), self.assertRaises(FrontendError): self.policy(score)
        self.catalog['licenses'][0]['price'] = 1000
        self.assertEqual(self.policy(1020)['score_requirement'], 1020)
        with self.assertRaises(FrontendError): self.policy(999)

    def test_strict_selection_types_bounds_duplicates_and_no_escape_hatch(self):
        valid = selection()
        for field, bad in [('area',None), ('area','areas:8'), ('area', ['areas:0']),
                ('licenses',[]), ('licenses',['licenses:0']*2), ('licenses',['licenses:9']),
                ('licenses',[True]), ('licenses','licenses:0'), ('weapons',('weapons:0',)),
                ('weapons',['weapons:0','weapons:7']), ('weapons',['weapons:8']),
                ('equipment',['equipment:0']), ('equipment',False), ('mode','observer'),
                ('mode','survival'), ('time_of_day',True), ('time_of_day',3),
                ('argv',['-debug']), ('flags',[])]:
            self.selection = {**valid, field: bad}
            with self.subTest(field=field,bad=bad), self.assertRaises(FrontendError): self.policy()
        self.selection=valid
        for slot in (-1,8,True,0.0):
            with self.assertRaises(FrontendError): self.policy(slot=slot)

    def test_revision_dialect_ambiguity_and_availability_fail_closed(self):
        for field, value in [('sha256','0'*64),('file_count',343),('byte_count',0)]:
            with self.assertRaises(FrontendError): self.policy(revision={**GENESIS_REVISION,field:value})
        original = observations()
        for mutate in [lambda c: c['dialect'].update(effective='mee-older'),
                       lambda c: c['dialect'].update(observed='classic'),
                       lambda c: c['licenses'][0].update(id='licenses:1'),
                       lambda c: c['licenses'][0].update(ordinal=True),
                       lambda c: c['licenses'][0].update(price=None),
                       lambda c: c['licenses'][0].update(label=''),
                       lambda c: c['weapons'][0].update(price=-1),
                       lambda c: c['areas'][0].update(launch_stem=None),
                       lambda c: c['areas'][0].update(launch_stem='area2'),
                       lambda c: c['score_modifier_observations'].append({}),
                       lambda c: c['diagnostics'].append({'code':'surplus-price'})]:
            self.catalog=copy.deepcopy(original); mutate(self.catalog)
            with self.assertRaises(FrontendError): self.policy()

    def test_observer_and_hunt_have_distinct_flags_and_loadouts(self):
        with self.assertRaises(FrontendError):
            observer_policy(GENESIS_REVISION,self.catalog,0,self.selection,100)
        obs={**selection(),'mode':'observer','licenses':[],'weapons':[]}
        argv=observer_policy(GENESIS_REVISION,self.catalog,0,obs,100)['candidate_argv']
        self.assertIn('-observ',argv); self.assertIn('din=0',argv); self.assertIn('wep=0',argv)
        self.assertNotIn('-observ',self.policy()['candidate_argv'])

    def test_advisory_catalog_paths_are_not_engine_resource_bases(self):
        # Generic catalog references are relative to the install; the engine
        # prepends HUNTDAT/WEAPONS. Exact pinned content owns availability.
        self.catalog['weapons'][0]['declared_references'] = [
            {'field': 'file', 'reference': 'weapon.car', 'status': 'missing'}]
        self.assertIn('wep=1', self.policy()['candidate_argv'])
