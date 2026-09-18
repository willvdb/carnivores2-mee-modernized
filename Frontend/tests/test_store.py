import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from lodge.store import FrontendError, Store, hunter


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.store = Store(self.temp.name)

    def test_identity_rename_archive_and_backup(self):
        with self.store.transaction() as data:
            identity = hunter(data, "create", name="Hunter Ω")["id"]
            data["extension"] = {"keep": [42]}
        original = self.store.path.read_bytes()
        with self.store.transaction() as data:
            hunter(data, "rename", identity, "New name")
        self.assertEqual(self.store.read()["hunters"][identity]["name"], "New name")
        self.assertEqual(self.store.path.with_suffix('.json.bak').read_bytes(), original)
        self.assertEqual(self.store.read()["extension"], {"keep": [42]})
        with self.store.transaction() as data:
            hunter(data, "archive", identity)
        self.assertIsNone(self.store.read()["active_hunter"])
        self.assertIn(identity, self.store.read()["hunters"])

    def test_schema_duplicates_and_recovery(self):
        with self.store.transaction() as data:
            hunter(data, "create", name="One")
        with self.store.transaction() as data:
            hunter(data, "create", name="Two")
        self.store.path.write_text('{"schema_version":1,"schema_version":1}')
        with self.assertRaises(FrontendError):
            self.store.read()
        self.store.restore_backup()
        self.assertEqual(len(self.store.read()["hunters"]), 1)
        self.assertEqual(len(list(Path(self.temp.name).glob('lodge.recovery-*'))), 1)
        value = self.store.read()
        for version in (0, 2, True, "1"):
            value['schema_version'] = version
            self.store.path.write_text(json.dumps(value))
            with self.assertRaises(FrontendError):
                self.store.read()

    def test_lock_rejects_second_writer(self):
        with self.store.transaction():
            with self.assertRaises(FrontendError):
                with self.store.transaction():
                    pass

    def test_missing_manifest_does_not_ignore_backup(self):
        with self.store.transaction() as data:
            hunter(data, 'create', name='A')
        with self.store.transaction() as data:
            hunter(data, 'create', name='B')
        self.store.path.unlink()
        with self.assertRaises(FrontendError):
            self.store.read()
        self.store.restore_backup()
        self.assertEqual(len(self.store.read()['hunters']), 1)

    def test_malformed_active_reference_fails_cleanly(self):
        with self.store.transaction() as data:
            hunter(data, 'create', name='A')
        data = self.store.read()
        data['active_hunter'] = []
        self.store.path.write_text(json.dumps(data))
        with self.assertRaises(FrontendError):
            self.store.read()

    def test_invalid_transaction_preserves_manifest(self):
        with self.store.transaction() as data:
            hunter(data, "create", name="A")
        before = self.store.path.read_bytes()
        with self.assertRaises(FrontendError):
            with self.store.transaction() as data:
                data['active_hunter'] = 'missing'
        self.assertEqual(before, self.store.path.read_bytes())

    def test_failed_replace_preserves_original(self):
        with self.store.transaction() as data:
            hunter(data, 'create', name='A')
        before = self.store.path.read_bytes()
        with patch('lodge.store.os.replace', side_effect=OSError('simulated disk failure')):
            with self.assertRaises(OSError):
                with self.store.transaction() as data:
                    hunter(data, 'create', name='B')
        self.assertEqual(before, self.store.path.read_bytes())
        self.assertFalse((self.store.directory / 'lodge.lock').exists())


if __name__ == '__main__':
    unittest.main()
