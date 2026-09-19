import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

from lodge.discovery import register
from lodge.profiles import associate
from lodge.session_io import capture, read_journal, session_root, transition
from lodge.sessions import prepare_session
from lodge.session_runner import recover_session, run_session
from lodge.reconciliation import reconcile_session
from lodge.store import FrontendError, Store, hunter, valid_id
from support import game
from test_launch import SCRIPT
from test_profiles import save_bytes, room_bytes


@unittest.skipUnless(os.environ.get('C2_PROFILE_PROBE'), 'build codec helper')
class SessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.game = game(self.temp.name)
        (self.game / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (self.game / 'trophy00.sav').write_bytes(save_bytes())
        (self.game / 'trophy00.sab').write_bytes(room_bytes())
        self.store = Store(Path(self.temp.name) / 'Lodge')
        with self.store.transaction() as data:
            h = hunter(data, 'create', name='Synthetic test hunter')
            i = register(data, self.game, dialect='c2-classic')
            a = associate(self.store, data, h['id'], i['id'], 'trophy00', 'personal', 'managed')
            self.association = a['id']
        self.source = self.store.directory / 'snapshots' / self.association
        self.original = capture(self.source)
        self.native = capture_native(self.game)
        self.manifest = self.store.path.read_bytes()

    def prepare(self, scenario='unchanged', **kwargs):
        return prepare_session(self.store, self.association, 'areas:0', scenario=scenario, **kwargs)

    def assert_sources_untouched(self):
        self.assertEqual(capture(self.source), self.original)
        self.assertEqual(capture_native(self.game), self.native)
        self.assertEqual(self.store.path.read_bytes(), self.manifest)

    def test_uuid_workspace_and_pinned_provenance(self):
        j = self.prepare()
        root = session_root(self.store, j['id'])
        self.assertTrue(valid_id(j['id']))
        self.assertNotIn(j['id'], [j['pins'][k] for k in ('hunter_id', 'instance_id', 'association_id')])
        self.assertEqual(capture(root / 'baseline'), self.original)
        self.assertEqual(capture(root / 'work/state'), self.original)
        self.assertEqual(j, read_journal(self.store, j['id']))
        self.assertFalse(j['process_launch_allowed'])
        self.assert_sources_untouched()

    def test_journal_atomic_replace_failure_retains_prepared(self):
        j = self.prepare()
        with patch('lodge.store.os.replace', side_effect=OSError('interrupted replacement')):
            with self.assertRaises(OSError):
                transition(session_root(self.store, j['id']), j, 'launching')
        self.assertEqual(j['state'], 'prepared')
        self.assertEqual(read_journal(self.store, j['id'])['state'], 'prepared')
        self.assertFalse(list(session_root(self.store, j['id']).glob('.pending-*')))

    def test_illegal_transition_and_traversal(self):
        j = self.prepare()
        with self.assertRaises(FrontendError):
            transition(session_root(self.store, j['id']), j, 'candidate')
        with self.assertRaises(FrontendError):
            read_journal(self.store, '../outside')

    def test_referenced_bundled_and_unknown_rejected(self):
        for field, value in (('ownership', 'referenced'), ('origin', 'bundled-example'), ('origin', 'unknown')):
            with self.subTest(value=value):
                data = self.store.read()
                data['associations'][self.association][field] = value
                with patch.object(self.store, 'read', return_value=data):
                    with self.assertRaises(FrontendError):
                        self.prepare()

    def test_source_drift_and_unreadable_rejected(self):
        (self.source / 'trophy00.sav').write_bytes(b'broken')
        with self.assertRaises(FrontendError):
            self.prepare()

    def test_missing_engine_is_not_required_for_synthetic_content(self):
        (self.game / 'CARN2.EXE').unlink()
        with self.store.transaction() as data:
            next(iter(data['instances'].values()))['engine_evidence'] = []
        self.assertEqual(self.prepare()['pins']['engine_evidence'], [])

    def test_invalid_selection_scenario_and_timeout_rejected(self):
        for kwargs in ({'scenario': '../evil'}, {'timeout': float('nan')}, {'timeout': 300}, {'time_of_day': True}):
            with self.subTest(kwargs=kwargs), self.assertRaises(FrontendError):
                self.prepare(**kwargs)
        with self.assertRaises(FrontendError):
            prepare_session(self.store, self.association, 'areas:999')

    def test_process_argv_cwd_exit_and_single_launch(self):
        j = self.prepare()
        result = run_session(self.store, j['id'])
        self.assertEqual(result['state'], 'returned')
        self.assertEqual(result['process']['exit_code'], 0)
        root = session_root(self.store, j['id'])
        receipt = json.loads((root / 'logs/stdout.log').read_text())
        self.assertEqual(receipt['cwd'], str(root / 'work'))
        self.assertEqual(receipt['argv'], j['execution']['argv'][3:])
        self.assertIn('$(never-a-shell)', receipt['argv'][-1])
        self.assertEqual(receipt['pid'], result['process']['pid'])
        with self.assertRaises(FrontendError):
            run_session(self.store, j['id'])
        self.assertFalse(result['capabilities']['engine_process_executed'])
        self.assert_sources_untouched()

    def test_nonzero_and_terminated_are_distinct_from_spawn_failure(self):
        for scenario in ('nonzero', 'terminated'):
            with self.subTest(scenario=scenario):
                result = run_session(self.store, self.prepare(scenario)['id'])
                self.assertEqual(result['state'], 'returned')
                self.assertNotEqual(result['process']['exit_code'], 0)
        j = self.prepare()
        with patch('lodge.session_runner.Popen', side_effect=OSError('synthetic spawn failure')):
            result = run_session(self.store, j['id'])
        self.assertEqual(result['state'], 'failed')
        self.assertIsNone(result['process'])
        self.assertEqual(result['diagnostics'][-1]['code'], 'spawn-failed')

    def test_timeout_and_cancellation_reap_owned_child(self):
        timed = run_session(self.store, self.prepare('hang', timeout=0.15)['id'])
        self.assertEqual(timed['process']['stop_reason'], 'timeout')
        self.assertIsNotNone(timed['process']['exit_code'])
        cancel = threading.Event()
        cancel.set()
        cancelled = run_session(self.store, self.prepare('hang')['id'], cancel=cancel)
        self.assertEqual(cancelled['process']['stop_reason'], 'cancelled')
        self.assertIsNotNone(cancelled['process']['exit_code'])

    def test_logs_are_bounded_and_drained(self):
        result = run_session(self.store, self.prepare('logs')['id'])
        root = session_root(self.store, result['id'])
        for log in result['logs'].values():
            self.assertTrue(log['truncated'])
            self.assertEqual((root / log['path']).stat().st_size, 65536)

    def test_pin_changes_fail_without_spawning(self):
        for target in ('content', 'engine', 'snapshot', 'workspace', 'baseline', 'fixture', 'codec', 'hunter', 'selection'):
            with self.subTest(target=target):
                j = self.prepare()
                root = session_root(self.store, j['id'])
                if target in ('fixture', 'codec', 'selection'):
                    from lodge.session_io import persist
                    if target == 'fixture':
                        j['execution']['fixture']['sha256'] = '0' * 64
                    elif target == 'codec':
                        j['pins']['codec']['sha256'] = '0' * 64
                    else:
                        j['pins']['selection']['equipment'] = ['equipment:0']
                    persist(root, j)
                    with patch('lodge.session_runner.Popen') as spawn:
                        result = run_session(self.store, j['id'])
                        spawn.assert_not_called()
                elif target == 'hunter':
                    data = self.store.read()
                    data['hunters'][j['pins']['hunter_id']]['name'] = 'Changed'
                    with patch.object(self.store, 'read', return_value=data), patch('lodge.session_runner.Popen') as spawn:
                        result = run_session(self.store, j['id'])
                        spawn.assert_not_called()
                else:
                    path = {'content': self.game / 'HUNTDAT/AREAS/AREA1.MAP',
                            'engine': self.game / 'CARN2.EXE',
                            'snapshot': self.source / 'trophy00.sav',
                            'workspace': root / 'work/state/trophy00.sav',
                            'baseline': root / 'baseline/trophy00.sav'}[target]
                    before = path.read_bytes()
                    path.write_bytes(before + b'changed')
                    with patch('lodge.session_runner.Popen') as spawn:
                        result = run_session(self.store, j['id'])
                        spawn.assert_not_called()
                    path.write_bytes(before)
                self.assertEqual(result['state'], 'failed')
                self.assertIsNone(result['process'])

    def test_persistent_engine_review_prevents_preparation(self):
        data = self.store.read()
        i = next(iter(data['instances'].values()))
        i['engine_relocation_reviews'] = [{'status': 'required'}]
        with patch.object(self.store, 'read', return_value=data), self.assertRaises(FrontendError):
            self.prepare()

    def test_ambiguous_recovery_never_signals_or_spawns(self):
        for stage in ('launching', 'running'):
            j = self.prepare()
            root = session_root(self.store, j['id'])
            transition(root, j, 'launching')
            if stage == 'running':
                transition(root, j, 'running', process={'pid': os.getpid()})
            with patch('lodge.session_runner.Popen') as spawn:
                result = recover_session(self.store, j['id'])
                spawn.assert_not_called()
            self.assertEqual(result['state'], 'interrupted')
            self.assertFalse((root / 'returned').exists())
        self.assert_sources_untouched()

    def test_clean_return_candidates_preserve_authority_and_exact_bytes(self):
        for scenario, changed in (('unchanged', []), ('sav', ['trophy00.sav']),
                                  ('pair', ['trophy00.sab', 'trophy00.sav'])):
            with self.subTest(scenario=scenario):
                j = run_session(self.store, self.prepare(scenario)['id'])
                result = reconcile_session(self.store, j['id'])
                root = session_root(self.store, j['id'])
                self.assertEqual(result['state'], 'candidate')
                self.assertEqual(result['reconciliation']['changed_members'], changed)
                self.assertEqual(result['reconciliation']['authority'], 'original-managed-snapshot')
                self.assertEqual(capture(root / 'returned'), capture(root / 'work/state'))
                self.assertEqual(reconcile_session(self.store, j['id']), result)
                self.assertFalse(result['capabilities']['hunt_save_round_trip_validated'])
                self.assertEqual(result['capabilities']['returned_native_state_readable'], 'yes')
                if scenario != 'unchanged':
                    self.assertEqual(result['returned_observation']['trophy00.sav']['score'], 175)
                self.assert_sources_untouched()

    def test_failure_matrix_quarantines_and_keeps_exact_return_evidence(self):
        cases = {'nonzero': 'unclean-process-return', 'changed-nonzero': 'unclean-process-return',
                 'corrupt-sav': 'unreadable-state', 'corrupt-sab': 'unreadable-state',
                 'missing-sab': 'missing-state-member', 'deleted-sav': 'missing-state-member',
                 'extra': 'unexpected-state-member', 'registration': 'registration-mismatch',
                 'hang': 'unclean-process-return', 'terminated': 'unclean-process-return'}
        for scenario, diagnostic in cases.items():
            with self.subTest(scenario=scenario):
                j = self.prepare(scenario, timeout=0.2 if scenario == 'hang' else 5)
                run_session(self.store, j['id'])
                result = reconcile_session(self.store, j['id'])
                root = session_root(self.store, j['id'])
                self.assertEqual(result['state'], 'quarantined')
                self.assertIn(diagnostic, [d['code'] for d in result['diagnostics']])
                self.assertEqual(capture(root / 'returned'), capture(root / 'work/state'))
                self.assert_sources_untouched()

    def test_returned_and_inspecting_recovery_are_idempotent(self):
        for stage in ('returned', 'inspecting'):
            j = run_session(self.store, self.prepare('sav')['id'])
            if stage == 'inspecting':
                transition(session_root(self.store, j['id']), j, 'inspecting')
            result = recover_session(Store(self.store.directory), j['id'])
            self.assertEqual(result['state'], 'candidate')
            self.assertEqual(recover_session(self.store, j['id']), result)
        self.assert_sources_untouched()

    def test_crash_during_capture_resumes_but_changed_evidence_never_overwritten(self):
        for mutate in (False, True):
            with self.subTest(mutate=mutate):
                j = run_session(self.store, self.prepare('pair')['id'])
                root = session_root(self.store, j['id'])
                with patch('lodge.reconciliation.write_blobs', side_effect=SystemExit('frontend stops')):
                    with self.assertRaises(SystemExit):
                        reconcile_session(self.store, j['id'])
                self.assertEqual(read_journal(self.store, j['id'])['state'], 'inspecting')
                before = read_journal(self.store, j['id'])['return_capture']
                if mutate:
                    (root / 'work/state/trophy00.sav').write_bytes(save_bytes(score=999))
                result = recover_session(self.store, j['id'])
                self.assertEqual(result['state'], 'quarantined' if mutate else 'candidate')
                self.assertEqual(result['return_capture'], before)

    def test_actual_frontend_restart_after_durable_return(self):
        j = self.prepare('sav')
        code = '''
import os, sys
from lodge.store import Store
from lodge import session_runner
original = session_runner.transition
def interrupted(root, journal, state, **fields):
    original(root, journal, state, **fields)
    if state == 'returned':
        os._exit(91)
session_runner.transition = interrupted
session_runner.run_session(Store(sys.argv[1]), sys.argv[2])
'''
        result = subprocess.run([sys.executable, '-c', code, str(self.store.directory), j['id']],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 91, result.stderr)
        self.assertEqual(read_journal(self.store, j['id'])['state'], 'returned')
        # This test owns/reaped the frontend and the returned journal proves it
        # reaped the fixed child. Simulate the documented manual stale-lock step.
        (self.store.directory / 'lodge.lock').unlink()
        self.assertEqual(recover_session(Store(self.store.directory), j['id'])['state'], 'candidate')
        self.assert_sources_untouched()

    def test_failed_running_journal_write_still_reaps_child(self):
        j = self.prepare('hang')
        children = []
        def spawn(*args, **kwargs):
            p = subprocess.Popen(*args, **kwargs)
            children.append(p)
            return p
        def fail_running(root, journal, state, **kwargs):
            if state == 'running':
                raise OSError('simulated disk failure')
            return transition(root, journal, state, **kwargs)
        with patch('lodge.session_runner.Popen', side_effect=spawn), patch('lodge.session_runner.transition', side_effect=fail_running):
            with self.assertRaises(OSError):
                run_session(self.store, j['id'])
        self.assertEqual(len(children), 1)
        self.assertIsNotNone(children[0].poll())
        self.assertEqual(recover_session(self.store, j['id'])['state'], 'interrupted')

    def test_optional_sab_absence_and_later_addition_policy(self):
        (self.source / 'trophy00.sab').unlink()
        with self.store.transaction() as data:
            a = data['associations'][self.association]
            a['files'] = [f for f in a['files'] if f['kind'] == 'sav']
        j = run_session(self.store, self.prepare()['id'])
        self.assertEqual(reconcile_session(self.store, j['id'])['state'], 'candidate')
        j = run_session(self.store, self.prepare()['id'])
        (session_root(self.store, j['id']) / 'work/state/trophy00.sab').write_bytes(room_bytes())
        self.assertEqual(reconcile_session(self.store, j['id'])['state'], 'quarantined')

    def test_links_and_unexpected_directories_never_followed(self):
        j = run_session(self.store, self.prepare()['id'])
        root = session_root(self.store, j['id'])
        extra = root / 'work/state/nested'
        extra.mkdir()
        (extra / 'opaque').write_bytes(b'retain unexpected nested bytes')
        self.assertEqual(reconcile_session(self.store, j['id'])['state'], 'quarantined')
        self.assertEqual((root / 'returned/nested/opaque').read_bytes(), b'retain unexpected nested bytes')
        if os.name == 'posix':
            j = self.prepare()
            root = session_root(self.store, j['id'])
            state = root / 'work/state/trophy00.sav'
            state.unlink()
            state.symlink_to(self.source / 'trophy00.sav')
            self.assertEqual(run_session(self.store, j['id'])['state'], 'failed')
            j = run_session(self.store, self.prepare()['id'])
            root = session_root(self.store, j['id'])
            (root / 'work/state/outside').symlink_to(self.source)
            result = reconcile_session(self.store, j['id'])
            self.assertEqual(result['state'], 'quarantined')
            self.assertFalse((root / 'returned/outside').exists())
        self.assert_sources_untouched()

    def test_malformed_and_foreign_journal_fail_cleanly(self):
        for field, value in (('schema_version', 2), ('transitions', [None]),
                             ('path_flavor', 'nt' if os.name == 'posix' else 'posix'),
                             ('baseline_members', [{'path': '../outside'}])):
            j = self.prepare()
            j[field] = value
            (session_root(self.store, j['id']) / 'journal.json').write_text(json.dumps(j))
            with self.subTest(field=field), self.assertRaises(FrontendError):
                read_journal(self.store, j['id'])

    def test_native_execution_evidence_cannot_enter_fixed_runner(self):
        from lodge.session_io import persist
        j = self.prepare()
        j['execution'].update(kind='native-engine', argv=['/arbitrary/engine'])
        persist(session_root(self.store, j['id']), j)
        with patch('lodge.session_runner.Popen') as spawn:
            result = run_session(self.store, j['id'])
            spawn.assert_not_called()
        self.assertEqual(result['state'], 'failed')

    def test_codec_drift_is_rejected_before_helper_execution(self):
        from lodge.session_io import persist
        j = self.prepare()
        j['pins']['codec']['sha256'] = '0' * 64
        persist(session_root(self.store, j['id']), j)
        with patch('lodge.profiles.subprocess.run') as helper:
            result = run_session(self.store, j['id'])
            helper.assert_not_called()
        self.assertEqual(result['state'], 'failed')

    def test_cli_session_pipeline_and_genesis_refusal(self):
        cli = Path(__file__).resolve().parents[1] / 'frontend.py'
        def command(*args, expected=0):
            process = subprocess.run([sys.executable, str(cli), '--store', str(self.store.directory), *args],
                                     capture_output=True, text=True, timeout=15)
            self.assertEqual(process.returncode, expected, process.stderr)
            return json.loads(process.stdout if expected == 0 else process.stderr)
        prepared = command('session', 'prepare-synthetic', self.association, '--area', 'areas:0', '--scenario', 'sav')
        returned = command('session', 'run', prepared['id'])
        self.assertEqual(returned['state'], 'candidate')
        self.assertEqual(command('session', 'inspect', prepared['id']), returned)
        self.assertEqual(command('session', 'recover', prepared['id']), returned)
        self.assertEqual(command('session', 'reconcile', prepared['id']), returned)
        refused = command('genesis-observer-plan', self.association, '--area', 'areas:0', expected=2)
        self.assertIn('unpinned', refused['error'])
        self.assert_sources_untouched()

    def test_oversized_and_linked_returns_stay_quarantined_in_workspace(self):
        for kind in ('oversized', 'hardlink'):
            with self.subTest(kind=kind):
                j = run_session(self.store, self.prepare()['id'])
                root = session_root(self.store, j['id'])
                extra = root / 'work/state/trophy00.extra'
                if kind == 'oversized':
                    with extra.open('wb') as stream:
                        stream.truncate(16 * 1024 * 1024 + 1)
                else:
                    os.link(root / 'work/state/trophy00.sav', extra)
                result = reconcile_session(self.store, j['id'])
                self.assertEqual(result['state'], 'quarantined')
                self.assertTrue(extra.exists())
                self.assertFalse((root / 'returned/trophy00.extra').exists())
                self.assertIn('unsafe-state-entry', [d['code'] for d in result['diagnostics']])
        self.assert_sources_untouched()

    def test_partial_capture_resumes_without_overwriting_changed_captured_bytes(self):
        from lodge.session_io import persist, write_blobs
        for altered in (False, True):
            j = run_session(self.store, self.prepare('pair')['id'])
            root = session_root(self.store, j['id'])
            transition(root, j, 'inspecting')
            entries, blobs = capture(root / 'work/state')
            j['return_capture'] = entries
            persist(root, j)
            saved = b'changed evidence' if altered else blobs['trophy00.sav']
            write_blobs(root / 'returned', {'trophy00.sav': saved})
            result = recover_session(self.store, j['id'])
            self.assertEqual(result['state'], 'quarantined' if altered else 'candidate')
            self.assertEqual((root / 'returned/trophy00.sav').read_bytes(), saved)

    def test_source_and_baseline_drift_after_return_prevent_clean_candidate(self):
        for target in ('source', 'baseline'):
            j = run_session(self.store, self.prepare('sav')['id'])
            root = session_root(self.store, j['id'])
            path = (self.source if target == 'source' else root / 'baseline') / 'trophy00.sav'
            before = path.read_bytes()
            path.write_bytes(save_bytes(score=999))
            result = reconcile_session(self.store, j['id'])
            path.write_bytes(before)
            self.assertEqual(result['state'], 'quarantined')
            self.assertEqual(capture(root / 'returned'), capture(root / 'work/state'))
        self.assert_sources_untouched()


def capture_native(root):
    return {p.name: p.read_bytes() for p in root.iterdir() if p.suffix in ('.sav', '.sab')}


if __name__ == '__main__':
    unittest.main()
