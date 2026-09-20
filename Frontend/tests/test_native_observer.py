"""Asset-free adapter tests. The policy double below does NOT certify Genesis."""
import hashlib
import os
from pathlib import Path
import unittest
import threading
import shutil
import stat
import subprocess
from types import SimpleNamespace
from unittest.mock import patch

from lodge.genesis import GENESIS_REVISION
from lodge.native_observer import (CAPABILITY, CONFIG, prepare_native, query_contract,
                                  run_native, trusted_engine)
from lodge.reconciliation import reconcile_session
from lodge.session_io import capture, persist, read_journal, session_root
from lodge.session_runner import recover_session, run_session
from lodge.store import FrontendError
from test_profiles import save_bytes
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

    def assert_observed(self, result, root, readable, changed):
        self.assertEqual(result['capabilities']['returned_native_state_readable'], readable)
        self.assertEqual(result['reconciliation']['changed_members'], changed)
        self.assertEqual(result['reconciliation']['comparison_status'], 'complete')
        self.assertEqual(result['reconciliation']['observation'], {
            'inventory': 'complete', 'byte_capture': 'complete',
            'retained_capture': 'verified', 'codec_inspection': 'complete'})
        self.assertEqual(result['returned_members'], capture(root/'work/state')[0])
        self.assertEqual(capture(root/'returned'), capture(root/'work/state'))
        self.assertEqual(capture(root/'baseline'), self.fixture.original)
        self.assertEqual(result['reconciliation']['promotion'], 'deferred')

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
        self.assert_observed(candidate, root, 'yes', [])
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

    def test_ancillary_findings_preserve_exact_native_observation(self):
        for changed_config in (False, True):
            with self.subTest(changed_config=changed_config):
                j = self.prepare(); self.launch(j)
                root = session_root(self.store, j['id'])
                anomaly = root/'work/output/unexpected.txt'
                anomaly.write_bytes(b'retained output anomaly')
                if changed_config:
                    (root/'work/state/trophy00.sav').write_bytes(save_bytes(score=175))
                    (root/'work/config/config.cfg').write_bytes(b'invalid config')
                result = reconcile_session(self.store, j['id'])
                self.assertEqual(result['state'], 'quarantined')
                self.assert_observed(result, root, 'yes', ['trophy00.sav'] if changed_config else [])
                findings = {d.get('domain'): d['message'] for d in result['diagnostics']
                            if d['code'] == 'native-workspace-review-required'}
                self.assertIn('unexpected native output', findings['output'])
                if changed_config:
                    self.assertIn('configuration changed', findings['config'])
                    self.assertEqual(result['returned_observation']['trophy00.sav']['score'], 175)
                self.assertEqual(anomaly.read_bytes(), b'retained output anomaly')

    def test_unavailable_state_capture_does_not_invent_member_observations(self):
        j = self.prepare(); self.launch(j)
        root = session_root(self.store, j['id'])
        def fail_state(path):
            if path == root/'work/state':
                raise OSError('injected state capture failure')
            return capture(path)
        with patch('lodge.reconciliation.capture', side_effect=fail_state), \
                patch('lodge.reconciliation.write_blobs') as copy, \
                patch('lodge.reconciliation.inspect_bytes') as inspect:
            result = reconcile_session(self.store, j['id'])
            copy.assert_not_called(); inspect.assert_not_called()
        self.assert_unavailable(result, root)
        self.assertIn('injected state capture failure', str(result['diagnostics']))

    def assert_unavailable(self, result, root):
        self.assertEqual(result['state'], 'quarantined')
        self.assertEqual(result['capabilities']['returned_native_state_readable'], 'unknown')
        self.assertIsNone(result['returned_members'])
        self.assertIsNone(result['returned_observation'])
        self.assertIsNone(result['reconciliation']['changed_members'])
        self.assertEqual(result['reconciliation']['comparison_status'], 'unavailable')
        self.assertTrue(all(v == 'unavailable' for v in result['reconciliation']['observation'].values()))
        self.assertFalse((root/'returned').exists())
        self.assertTrue({'missing-state-member', 'unreadable-state', 'unexpected-state-member'}.isdisjoint(
            d['code'] for d in result['diagnostics']))

    def test_linked_state_directory_and_ancestor_are_never_read(self):
        for relative in ('work/state', 'work'):
            with self.subTest(relative=relative):
                j = self.prepare(); self.launch(j)
                root = session_root(self.store, j['id'])
                target = root/relative
                original = root/'retained-work'
                target.rename(original)
                if os.name == 'nt':
                    # Junctions do not require Windows symlink privileges.
                    subprocess.run(['cmd', '/c', 'mklink', '/J', str(target), str(original)],
                                   check=True, capture_output=True)
                else:
                    target.symlink_to(original, target_is_directory=True)
                real_open = Path.open
                def guard(path, *args, **kwargs):
                    if path.is_relative_to(target):
                        self.fail('must not open files through unsafe state/ancestor')
                    return real_open(path, *args, **kwargs)
                try:
                    with patch.object(Path, 'open', guard), \
                            patch('lodge.reconciliation.write_blobs') as copy:
                        result = reconcile_session(self.store, j['id'])
                        copy.assert_not_called()
                    self.assert_unavailable(result, root)
                finally:
                    if os.name == 'nt':
                        target.rmdir()
                    else:
                        target.unlink()

    def test_reparse_attribute_on_state_ancestor_blocks_inventory(self):
        j = self.prepare(); self.launch(j)
        root = session_root(self.store, j['id'])
        work = root/'work'
        real_lstat = Path.lstat
        real_iterdir = Path.iterdir
        def reparse(path, *args, **kwargs):
            info = real_lstat(path, *args, **kwargs)
            if path == work:
                return SimpleNamespace(st_file_attributes=stat.FILE_ATTRIBUTE_REPARSE_POINT,
                                       st_mode=info.st_mode, st_nlink=info.st_nlink)
            return info
        def guard(path):
            if path.is_relative_to(work):
                self.fail('must not inventory through a reparse ancestor')
            return real_iterdir(path)
        with patch.object(Path, 'lstat', reparse), \
                patch.object(Path, 'iterdir', guard), \
                patch('lodge.reconciliation.write_blobs') as copy:
            result = reconcile_session(self.store, j['id'])
            copy.assert_not_called()
        self.assert_unavailable(result, root)
        self.assertIn('reparse point', str(result['diagnostics']))

    def test_observed_invalid_or_missing_pair_is_not_unknown(self):
        for mutation, changed, diagnostic in (
                ('truncated', ['trophy00.sav'], 'unreadable-state'),
                ('missing', ['trophy00.sab'], 'missing-state-member'),
                ('empty', ['trophy00.sab', 'trophy00.sav'], 'missing-state-member')):
            with self.subTest(mutation=mutation):
                j = self.prepare(); self.launch(j)
                root = session_root(self.store, j['id'])
                if mutation == 'truncated':
                    (root/'work/state/trophy00.sav').write_bytes(b'truncated')
                else:
                    (root/'work/state/trophy00.sab').unlink()
                    if mutation == 'empty':
                        (root/'work/state/trophy00.sav').unlink()
                result = reconcile_session(self.store, j['id'])
                self.assertEqual(result['state'], 'quarantined')
                self.assert_observed(result, root, 'no', changed)
                self.assertIn(diagnostic, [d['code'] for d in result['diagnostics']])

    def test_codec_failure_keeps_observed_bytes_without_claiming_corruption(self):
        j = self.prepare(); self.launch(j)
        root = session_root(self.store, j['id'])
        with patch('lodge.reconciliation.inspect_bytes', side_effect=FrontendError('codec helper timed out')):
            result = reconcile_session(self.store, j['id'])
        self.assertEqual(result['state'], 'quarantined')
        self.assertEqual(result['capabilities']['returned_native_state_readable'], 'unknown')
        self.assertIsNone(result['returned_observation'])
        self.assertEqual(result['reconciliation']['changed_members'], [])
        self.assertEqual(result['reconciliation']['observation']['codec_inspection'], 'unavailable')
        self.assertEqual(result['reconciliation']['observation']['retained_capture'], 'verified')
        self.assertEqual(capture(root/'returned'), capture(root/'work/state'))
        self.assertNotIn('unreadable-state', [d['code'] for d in result['diagnostics']])

    def test_uncaptured_expected_member_does_not_claim_byte_change_or_corruption(self):
        j = self.prepare(); self.launch(j)
        root = session_root(self.store, j['id'])
        from lodge.profiles import MAX_STATE_BYTES
        with (root/'work/state/trophy00.sav').open('wb') as stream:
            stream.truncate(MAX_STATE_BYTES + 1)
        result = reconcile_session(self.store, j['id'])
        self.assertEqual(result['state'], 'quarantined')
        self.assertEqual(result['capabilities']['returned_native_state_readable'], 'unknown')
        self.assertIsNone(result['reconciliation']['changed_members'])
        self.assertEqual(result['reconciliation']['comparison_status'], 'unavailable')
        self.assertEqual(result['reconciliation']['observation']['byte_capture'], 'partial')
        self.assertEqual(result['returned_observation'].keys(), {'trophy00.sab'})
        self.assertFalse((root/'returned/trophy00.sav').exists())
        self.assertEqual((root/'returned/trophy00.sab').read_bytes(),
                         (root/'work/state/trophy00.sab').read_bytes())
        self.assertNotIn('unreadable-state', [d['code'] for d in result['diagnostics']])

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

    def test_ancillary_anomaly_recovery_retains_partial_and_divergent_evidence(self):
        from lodge.session_io import transition, write_blobs
        for stage in ('returned', 'partial', 'divergent-returned', 'divergent-work'):
            with self.subTest(stage=stage):
                j = self.prepare(); j = self.launch(j)
                root = session_root(self.store, j['id'])
                entries, blobs = capture(root/'work/state')
                (root/'work/output/extra.log').write_bytes(b'output anomaly')
                j['diagnostics'].append({'code': 'earlier-failure'})
                persist(root, j)
                if stage != 'returned':
                    transition(root, j, 'inspecting')
                    j['return_capture'] = entries; persist(root, j)
                    saved = b'divergent prior evidence' if stage == 'divergent-returned' else blobs['trophy00.sav']
                    write_blobs(root/'returned', {'trophy00.sav': saved})
                    if stage == 'divergent-work':
                        (root/'work/state/trophy00.sav').write_bytes(save_bytes(score=175))
                with patch('lodge.native_observer.query_contract', side_effect=AssertionError('no engine query')), \
                        patch('lodge.session_runner.Popen', side_effect=AssertionError('no launch')), \
                        patch('lodge.session_runner.stop_owned', side_effect=AssertionError('no PID signal')):
                    result = recover_session(self.store, j['id'])
                self.assertEqual(result['state'], 'quarantined')
                self.assertEqual(result['return_capture'], entries)
                self.assertIn('earlier-failure', [d['code'] for d in result['diagnostics']])
                self.assertIn('unexpected native output', str(result['diagnostics']))
                if stage.startswith('divergent'):
                    self.assertEqual((root/'returned/trophy00.sav').read_bytes(), saved)
                    self.assertFalse((root/'returned/trophy00.sab').exists())
                    self.assertIn('return-review-required', [d['code'] for d in result['diagnostics']])
                    self.assertEqual(result['reconciliation']['observation']['retained_capture'], 'unavailable')
                    self.assertEqual(result['capabilities']['returned_native_state_readable'], 'unknown')
                    self.assertEqual(result['reconciliation']['changed_members'],
                                     ['trophy00.sav'] if stage == 'divergent-work' else [])
                else:
                    self.assert_observed(result, root, 'yes', [])
                self.assertEqual(capture(root/'baseline'), self.fixture.original)
                terminal_bytes = (root/'journal.json').read_bytes()
                self.assertEqual(recover_session(self.store, j['id']), result)
                self.assertEqual((root/'journal.json').read_bytes(), terminal_bytes)

    def test_crash_during_partial_copy_keeps_ancillary_findings_durable(self):
        from lodge.session_io import write_blobs
        j = self.prepare(); self.launch(j)
        root = session_root(self.store, j['id'])
        anomaly = root/'work/output/extra.log'
        anomaly.write_bytes(b'output anomaly')
        def partial_copy(destination, blobs):
            write_blobs(destination, {'trophy00.sav': blobs['trophy00.sav']})
            raise SystemExit('frontend crash during copy')
        with patch('lodge.reconciliation.write_blobs', side_effect=partial_copy):
            with self.assertRaises(SystemExit):
                reconcile_session(self.store, j['id'])
        interrupted = read_journal(self.store, j['id'])
        self.assertEqual(interrupted['state'], 'inspecting')
        self.assertIn('unexpected native output', str(interrupted['diagnostics']))
        # Removing the anomaly after a crash cannot erase its durable finding.
        anomaly.unlink()
        result = recover_session(self.store, j['id'])
        self.assertEqual(result['state'], 'quarantined')
        self.assert_observed(result, root, 'yes', [])
        self.assertIn('unexpected native output', str(result['diagnostics']))

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

    def test_historical_terminal_journals_are_not_rewritten(self):
        for native in (False, True):
            with self.subTest(native=native):
                j = self.prepare() if native else self.fixture.prepare()
                self.launch(j) if native else run_session(self.store, j['id'])
                result = reconcile_session(self.store, j['id'])
                root = session_root(self.store, j['id'])
                # Older terminal schema-1/2 journals have no stage/comparison fields.
                result['reconciliation'].pop('observation')
                result['reconciliation'].pop('comparison_status')
                persist(root, result)
                original = (root/'journal.json').read_bytes()
                with patch('lodge.reconciliation.capture', side_effect=AssertionError('no reinspection')):
                    self.assertEqual(recover_session(self.store, j['id']), result)
                    self.assertEqual(reconcile_session(self.store, j['id']), result)
                self.assertEqual((root/'journal.json').read_bytes(), original)

    def test_native_recovery_never_signals_journal_pid(self):
        from lodge.session_io import transition
        for stage in ('launching', 'running'):
            with self.subTest(stage=stage):
                j=self.prepare(); root=session_root(self.store,j['id'])
                transition(root,j,'launching')
                if stage == 'running':
                    transition(root,j,'running',process={'pid':os.getpid()})
                with patch('lodge.session_runner.stop_owned', side_effect=AssertionError('must not signal')), \
                        patch('lodge.session_runner.Popen', side_effect=AssertionError('must not launch')), \
                        patch('lodge.native_observer.query_contract', side_effect=AssertionError('must not query')), \
                        patch('lodge.reconciliation.capture', side_effect=AssertionError('must not inspect live state')):
                    interrupted = recover_session(self.store,j['id'])
                    self.assertEqual(interrupted['state'], 'interrupted')
                    self.assertEqual(recover_session(self.store,j['id']), interrupted)
                self.assertFalse((root/'returned').exists())

    def test_workspace_overlap_rejected(self):
        with patch.object(self.store, 'directory', self.fixture.game/'lodge'):
            with self.assertRaises(FrontendError):
                self.prepare()
