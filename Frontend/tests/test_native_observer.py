"""Asset-free adapter tests. The policy double below does NOT certify Genesis."""
import hashlib
import os
from pathlib import Path
import unittest
import threading
import shutil
from unittest.mock import patch

from lodge.genesis import GENESIS_REVISION
from lodge.native_observer import (CAPABILITY, CONFIG, prepare_native, query_contract,
                                  run_native, trusted_engine)
from lodge.reconciliation import reconcile_session
from lodge.session_io import capture, persist, read_journal, session_root
from lodge.session_runner import recover_session, run_session
from lodge.store import FrontendError
import test_sessions


class NativeObserverTests(unittest.TestCase):
    def setUp(self):
        # Reuse fixture setup, not the inherited suite or synthetic runner spec.
        self.fixture = test_sessions.SessionTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.store, self.association = self.fixture.store, self.fixture.association
        path = os.environ.get('C2_NATIVE_TEST_ENGINE')
        if not path or not Path(path).is_file():
            self.fail('build the required native session fixture; native tests never silently skip')
        self.engine = Path(path).resolve()
        self.digest = hashlib.sha256(self.engine.read_bytes()).hexdigest()
        # Structural Genesis policy has its own unmocked tests. Here only that
        # gate is doubled, explicitly labeled fixture-only, so synthetic content
        # tests real process/contract/workspace integration without a false hash.
        self.policy = patch('lodge.native_observer.observer_policy', return_value={
            'adapter': 'asset-free-policy-double',
            'candidate_argv': ['reg=0', 'prj=huntdat/areas/area1', 'din=0', 'wep=0',
                               'dtm=1', '-observ', 'smod=0.85,0.70,0.80,1.0,1.25,1.0']})
        self.policy.start(); self.addCleanup(self.policy.stop)
        self.addCleanup(self.fixture.assert_sources_untouched)

    def prepare(self, **kwargs):
        return prepare_native(self.store, self.association, 'areas:0', self.engine,
                              self.digest, experimental=True, **kwargs)

    def launch(self, journal):
        return run_native(self.store, journal['id'], self.engine, self.digest, experimental=True)

    def test_explicit_trust_and_capability_are_separate_requirements(self):
        with self.assertRaises(FrontendError):
            trusted_engine(self.engine, self.digest, False)
        with self.assertRaises(FrontendError):
            trusted_engine(self.engine, '0' * 64, True)
        evidence = trusted_engine(self.engine, self.digest, True)
        self.assertEqual(query_contract(evidence), CAPABILITY)
        with patch('lodge.native_observer.query_contract', side_effect=FrontendError('unsupported contract')):
            with self.assertRaises(FrontendError):
                self.prepare()

    def test_unmodified_genesis_policy_refuses_fixture_content(self):
        self.policy.stop()
        with self.assertRaisesRegex(FrontendError, 'unpinned content'):
            self.prepare()
        self.policy.start()

    def test_incompatible_capability_is_rejected_from_actual_child(self):
        with patch.dict(os.environ, {'C2_NATIVE_FIXTURE_BEHAVIOR': 'bad-contract'}):
            with self.assertRaisesRegex(FrontendError, 'unsupported engine'):
                self.prepare()

    def test_nonzero_native_return_and_cancellation_are_quarantined(self):
        j = self.prepare()
        with patch.dict(os.environ, {'C2_NATIVE_FIXTURE_BEHAVIOR': 'nonzero'}):
            self.assertEqual(self.launch(j)['process']['exit_code'], 7)
        self.assertEqual(reconcile_session(self.store,j['id'])['state'], 'quarantined')
        j = self.prepare()
        cancelled = threading.Event()
        timer = threading.Timer(0.2, cancelled.set)
        timer.start()
        try:
            with patch.dict(os.environ, {'C2_NATIVE_FIXTURE_BEHAVIOR': 'hang'}):
                returned = run_native(self.store,j['id'],self.engine,self.digest,True,cancel=cancelled)
        finally:
            timer.cancel()
        self.assertEqual(returned['process']['stop_reason'], 'cancelled')
        self.assertIsInstance(returned['process']['exit_code'], int)
        self.assertEqual(reconcile_session(self.store,j['id'])['state'], 'quarantined')

    def test_native_spec_schema_layout_and_interactive_validation_timeout(self):
        j = self.prepare()
        self.assertEqual(j['schema_version'], 2)
        self.assertEqual(j['execution']['timeout_seconds'], 900)
        self.assertNotEqual(j['pins']['revision'], GENESIS_REVISION)
        self.assertFalse(j['process_launch_allowed'])
        self.assertFalse(j['synthetic_process_launch_allowed'])
        self.assertEqual(j['execution']['cwd'], str(self.fixture.game))
        self.assertFalse(j['execution']['shell'])
        self.assertNotIn('reg=0', j['execution']['argv'])
        self.assertIn('--session-slot=0', j['execution']['argv'])
        root = session_root(self.store, j['id'])
        self.assertEqual({p.name for p in (root/'work').iterdir()}, {'state','config','output'})
        self.assertEqual((root/'work/config/config.cfg').read_bytes(), CONFIG)
        with self.assertRaises(FrontendError):
            self.prepare(timeout=5)

    def test_real_native_child_returns_candidate_with_outputs_outside_state(self):
        j = self.prepare()
        returned = self.launch(j)
        self.assertEqual(returned['state'], 'returned', returned['diagnostics'])
        self.assertEqual(returned['process']['exit_code'], 0)
        candidate = reconcile_session(self.store, j['id'])
        self.assertEqual(candidate['state'], 'candidate', candidate['diagnostics'])
        self.assertTrue(candidate['capabilities']['engine_process_executed'])
        self.assertEqual(candidate['capabilities']['synthetic_child_lifecycle'], 'not-executed')
        self.assertFalse(candidate['capabilities']['observer_session_launched'])
        self.assertFalse(candidate['capabilities']['hunt_save_round_trip_validated'])
        root = session_root(self.store, j['id'])
        self.assertEqual(capture(root/'returned')[0], j['baseline_members'])
        self.assertIn(b'not Genesis', (root/'work/output/render.log').read_bytes())
        self.assertEqual(read_journal(self.store,j['id'])['reconciliation']['promotion'], 'deferred')

    def test_generic_synthetic_run_cannot_authorize_native_and_native_cannot_run_synthetic(self):
        j = self.prepare()
        with self.assertRaises(FrontendError):
            run_session(self.store,j['id'])
        self.assertEqual(read_journal(self.store,j['id'])['state'], 'prepared')
        synthetic = self.fixture.prepare()
        with self.assertRaises(FrontendError):
            self.launch(synthetic)

    def test_changed_spec_baseline_config_and_engine_trust_block_spawn(self):
        for mutation in ('argv', 'contract-type', 'baseline', 'config', 'output'):
            with self.subTest(mutation=mutation):
                j = self.prepare(); root = session_root(self.store,j['id'])
                if mutation == 'argv':
                    j['execution']['argv'].append('-debug'); persist(root,j)
                elif mutation == 'contract-type':
                    j['execution']['contract']['version'] = True; persist(root,j)
                elif mutation == 'baseline':
                    (root/'baseline/trophy00.sav').write_bytes(b'changed')
                elif mutation == 'config':
                    (root/'work/config/config.cfg').write_bytes(b'fov 90')
                else:
                    (root/'work/output/render.log').write_bytes(b'preexisting')
                with patch('lodge.session_runner.Popen', side_effect=AssertionError('must not spawn')):
                    self.assertEqual(self.launch(j)['state'], 'failed')
        j = self.prepare()
        with self.assertRaises(FrontendError):
            run_native(self.store,j['id'],self.engine,'0'*64,True)

    def test_native_screenshot_name_follows_platform_without_extra_output_permission(self):
        for accepted in (True, False):
            with self.subTest(accepted=accepted):
                j=self.prepare(); self.launch(j)
                name = 'HUNT0001.BM' if os.name == 'nt' else 'HUNT0001.BMP'
                if not accepted:
                    name += '.unclassified'
                (session_root(self.store,j['id'])/'work/output'/name).write_bytes(b'BM fixture')
                result = reconcile_session(self.store,j['id'])
                self.assertEqual(result['state'], 'candidate' if accepted else 'quarantined', result['diagnostics'])

    def test_unexpected_output_config_or_native_members_quarantine(self):
        for relative in ('work/extra', 'work/output/glperf-capture.csv', 'work/config/extra.cfg', 'work/state/extra.log'):
            with self.subTest(relative=relative):
                j=self.prepare(); self.launch(j)
                (session_root(self.store,j['id'])/relative).write_bytes(b'extra evidence')
                self.assertEqual(reconcile_session(self.store,j['id'])['state'], 'quarantined')

    def test_selected_engine_drift_before_launch_and_after_return(self):
        directory = Path(self.fixture.temp.name).resolve() / 'selected-engine'
        directory.mkdir()
        self.engine = directory / self.engine.name
        shutil.copy2(os.environ['C2_NATIVE_TEST_ENGINE'], self.engine)
        j = self.prepare()
        with self.engine.open('ab') as stream:
            stream.write(b'changed trusted image')
        with self.assertRaises(FrontendError):
            self.launch(j)
        self.assertEqual(read_journal(self.store,j['id'])['state'], 'prepared')
        shutil.copy2(os.environ['C2_NATIVE_TEST_ENGINE'], self.engine)
        j = self.prepare(); self.launch(j)
        with self.engine.open('ab') as stream:
            stream.write(b'changed after return')
        result = reconcile_session(self.store,j['id'])
        self.assertEqual(result['state'], 'quarantined')
        self.assertIn('selected-engine-changed-on-return', [d['code'] for d in result['diagnostics']])

    def test_returned_and_partial_inspecting_recovery_is_candidate_only(self):
        from lodge.session_io import transition, write_blobs
        for partial in (False, True):
            with self.subTest(partial=partial):
                j = self.prepare(); j = self.launch(j)
                root = session_root(self.store,j['id'])
                if partial:
                    transition(root,j,'inspecting')
                    entries, blobs = capture(root/'work/state')
                    j['return_capture'] = entries; persist(root,j)
                    write_blobs(root/'returned', {'trophy00.sav':blobs['trophy00.sav']})
                with patch('lodge.native_observer.query_contract', side_effect=AssertionError('recovery never executes engine')):
                    recovered = recover_session(self.store,j['id'])
                self.assertEqual(recovered['state'], 'candidate', recovered['diagnostics'])
                self.assertEqual(capture(root/'returned')[0], j['baseline_members'])
                self.assertEqual(recovered['reconciliation']['promotion'], 'deferred')

    def test_native_execution_evidence_drift_on_return_is_quarantined(self):
        for field, value in (('contract', {**CAPABILITY, 'version':99}),
                             ('contract', {**CAPABILITY, 'version':True}), ('shell', 0),
                             ('argv', ['/unexpected']),
                             ('cwd', '/unexpected'), ('config_sha256', '0'*64)):
            with self.subTest(field=field):
                j=self.prepare(); j=self.launch(j)
                j['execution'][field] = value
                persist(session_root(self.store,j['id']), j)
                with patch('lodge.native_observer.query_contract', side_effect=AssertionError('no query on return')):
                    result=recover_session(self.store,j['id'])
                self.assertEqual(result['state'], 'quarantined')
                self.assertIn('native-execution-evidence-changed-on-return', [d['code'] for d in result['diagnostics']])

    def test_unknown_or_mismatched_journal_version_rejected(self):
        from lodge.session_io import validate_journal
        j=self.prepare()
        for version in (1, 3, True):
            j['schema_version']=version
            with self.assertRaises(FrontendError):
                validate_journal(j,j['id'])

    def test_native_recovery_never_signals_journal_pid(self):
        from lodge.session_io import transition
        j=self.prepare(); root=session_root(self.store,j['id'])
        transition(root,j,'launching')
        transition(root,j,'running',process={'pid':os.getpid()})
        with patch('lodge.session_runner.stop_owned', side_effect=AssertionError('must not signal')):
            self.assertEqual(recover_session(self.store,j['id'])['state'], 'interrupted')
        self.assertFalse((root/'returned').exists())

    def test_workspace_overlap_rejected(self):
        with patch.object(self.store, 'directory', self.fixture.game/'lodge'):
            with self.assertRaises(FrontendError):
                self.prepare()
