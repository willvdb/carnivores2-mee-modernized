import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from lodge.discovery import register
from lodge.profiles import associate
from lodge.session_io import capture, read_journal, session_root, transition
from lodge.sessions import prepare_session
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


def capture_native(root):
    return {p.name: p.read_bytes() for p in root.iterdir() if p.suffix in ('.sav', '.sab')}


if __name__ == '__main__':
    unittest.main()
