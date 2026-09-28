"""Shared disposable session setup, independent of either test suite."""
from pathlib import Path
import tempfile
import unittest

from lodge.discovery import register
from lodge.profiles import associate
from lodge.session_io import capture
from lodge.sessions import prepare_session
from lodge.store import Store, hunter
from c2_test_support import game, SCRIPT, save_bytes, room_bytes


class SessionFixture(unittest.TestCase):
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


def capture_native(root):
    return {p.name: p.read_bytes() for p in root.iterdir() if p.suffix in ('.sav', '.sab')}
