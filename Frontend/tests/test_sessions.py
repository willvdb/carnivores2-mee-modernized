import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from lodge.discovery import register
from lodge.profiles import associate
from lodge.session_io import capture, read_journal, session_root, transition
from lodge.sessions import prepare_session
from lodge.session_runner import recover_session, run_session
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


def capture_native(root):
    return {p.name: p.read_bytes() for p in root.iterdir() if p.suffix in ('.sav', '.sab')}


if __name__ == '__main__':
    unittest.main()
