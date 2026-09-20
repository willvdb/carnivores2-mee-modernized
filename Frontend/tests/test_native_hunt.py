"""Asset-free production lifecycle; edition policy double is fixture-only."""
import hashlib
import os
from pathlib import Path
import threading
import unittest
from unittest.mock import patch

from lodge.native_hunt import prepare_hunt, run_hunt, plan_hunt
from lodge.native_observer import run_native
from lodge.reconciliation import reconcile_session
from lodge.session_io import capture, session_root
from lodge.session_runner import run_session
from lodge.store import FrontendError
import test_sessions
from test_genesis_hunt import selection


class NativeHuntTests(unittest.TestCase):
    def setUp(self):
        self.fixture = test_sessions.SessionTests(); self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.store, self.association = self.fixture.store, self.fixture.association
        self.engine = Path(os.environ['C2_NATIVE_TEST_ENGINE']).resolve()
        self.digest = hashlib.sha256(self.engine.read_bytes()).hexdigest()
        self.policy = patch('lodge.native_hunt.hunt_policy', side_effect=self.fixture_policy)
        self.policy.start(); self.addCleanup(self.policy.stop)
        self.addCleanup(self.fixture.assert_sources_untouched)

    @staticmethod
    def fixture_policy(revision, catalog, slot, chosen, score):
        # Deliberately distinct policy provenance. No production hash/gate patched.
        return {'adapter':'asset-free-hunt-policy-double', 'selection':chosen,
                'candidate_argv':[f'reg={slot}','prj=huntdat/areas/area1','din=1','wep=1',
                                  f"dtm={chosen['time_of_day']}",'smod=0.85,0.70,0.80,1.0,1.25,1.0']}

    def prepare(self, **kwargs):
        return prepare_hunt(self.store,self.association,selection(),self.engine,self.digest,True,**kwargs)

    def launch(self,journal,**kwargs):
        return run_hunt(self.store,journal['id'],self.engine,self.digest,True,**kwargs)

    def test_native_candidate_preserves_original_and_baseline(self):
        planned = plan_hunt(self.store,self.association,selection())
        self.assertFalse(planned['process_launch_allowed'])
        j=self.prepare(); root=session_root(self.store,j['id'])
        self.assertEqual(j['schema_version'],3)
        self.assertEqual(j['execution']['timeout_seconds'],900)
        with patch.dict(os.environ,{'C2_NATIVE_FIXTURE_BEHAVIOR':'changed'}):
            self.assertEqual(self.launch(j)['state'],'returned')
        result=reconcile_session(self.store,j['id'])
        self.assertEqual(result['state'],'candidate',result['diagnostics'])
        self.assertEqual(result['reconciliation']['changed_members'],['trophy00.sav'])
        self.assertEqual(result['capabilities']['native_hunt_lifecycle'],'completed')
        self.assertFalse(result['capabilities']['hunt_save_round_trip_validated'])
        self.assertEqual(capture(root/'baseline'),self.fixture.original)
        self.assertEqual(capture(root/'returned'),capture(root/'work/state'))
        self.assertEqual(result['returned_observation']['trophy00.sav']['score'],
                         j['pins']['source_observation']['trophy00.sav']['score']+7)

    def test_unmodified_policy_rejects_fixture_and_gates_are_distinct(self):
        self.policy.stop()
        with self.assertRaisesRegex(FrontendError,'unpinned content'): self.prepare()
        self.policy.start()
        j=self.prepare()
        with self.assertRaises(FrontendError): run_session(self.store,j['id'])
        with self.assertRaises(FrontendError): run_native(self.store,j['id'],self.engine,self.digest,True)
        with self.assertRaises(FrontendError): run_hunt(self.store,j['id'],self.engine,self.digest,False)
        with self.assertRaises(FrontendError): self.prepare(timeout=5)

    def test_failure_corruption_missing_and_cancellation_quarantine(self):
        for scenario in ('nonzero','corrupt','missing','hang'):
            with self.subTest(scenario=scenario):
                j=self.prepare(); cancel=threading.Event()
                timer=threading.Timer(0.15,cancel.set); timer.start()
                try:
                    with patch.dict(os.environ,{'C2_NATIVE_FIXTURE_BEHAVIOR':scenario}):
                        self.launch(j,cancel=cancel if scenario=='hang' else None)
                finally: timer.cancel()
                if scenario=='missing':
                    (session_root(self.store,j['id'])/'work/state/trophy00.sab').unlink()
                result=reconcile_session(self.store,j['id'])
                self.assertEqual(result['state'],'quarantined',result['diagnostics'])

    def test_tampered_selection_fails_preflight(self):
        j=self.prepare()
        from lodge.session_io import persist
        j['pins']['selection']['licenses']=['licenses:8']
        persist(session_root(self.store,j['id']),j)
        self.assertEqual(self.launch(j)['state'],'failed')
