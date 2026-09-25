"""Native store operations against the unchanged lodge reference (disposable stores).

Every case runs the reference on one copy of a store and the native driver on
a twin copy, then compares results, refusal messages and the exact persisted
bytes. Native-generated UUIDs and timestamps are the only tolerated
differences: each is checked for shape (and timestamps for freshness) and
substituted position by position, never by dropping objects. The manifest's
encoding is additionally checked byte for byte by re-encoding the native
content with the reference encoder.
"""
import copy
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import uuid

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
from lodge.discovery import register  # noqa: E402
from lodge.managed_state import UPGRADE_BACKUP  # noqa: E402
from lodge.profiles import associate  # noqa: E402
from lodge.session_io import encode  # noqa: E402
from lodge.store import FrontendError, Store, _unique_object, hunter, validate  # noqa: E402
from support import game  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

DRIVER, PROBE = sys.argv[1:3]
TIMESTAMP = re.compile(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{6})?\+00:00')
LOCK_MESSAGE = 'frontend writer lock exists: {}; verify its owner before manual recovery'


def is_uuid(value):
    try:
        return isinstance(value, str) and str(uuid.UUID(value)) == value
    except ValueError:
        return False


def is_timestamp(value):
    return isinstance(value, str) and TIMESTAMP.fullmatch(value) is not None


def transaction_bytes(value):
    return (json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False) + '\n').encode('utf-8')


def loads(data):
    return json.loads(data.decode('utf-8'), object_pairs_hook=_unique_object)


class Volatile:
    """Position-by-position reconciliation of native-generated identities and timestamps."""

    def __init__(self, case, window):
        self.case, self.window, self.mapping = case, window, {}

    def bind(self, actual, expected):
        if actual == expected:
            return
        if is_uuid(expected):
            self.case.assertTrue(is_uuid(actual), (actual, expected))
        elif is_timestamp(expected):
            self.case.assertTrue(is_timestamp(actual), (actual, expected))
            stamp = datetime.fromisoformat(actual)
            self.case.assertEqual(stamp.isoformat(), actual)
            before, after = self.window
            self.case.assertTrue(before <= stamp <= after, (actual, before, after))
        else:
            self.case.fail(f'native {actual!r} differs from reference {expected!r}')
        previous = self.mapping.setdefault(actual, expected)
        self.case.assertEqual(previous, expected, f'native value {actual!r} bound twice')

    def reconcile(self, actual, expected, where='result'):
        if isinstance(expected, dict):
            self.case.assertIsInstance(actual, dict, where)
            self.case.assertEqual(len(actual), len(expected), where)
            for (ak, av), (ek, ev) in zip(actual.items(), expected.items()):
                self.bind(ak, ek)
                self.reconcile(av, ev, f'{where}.{ek}')
        elif isinstance(expected, list):
            self.case.assertIsInstance(actual, list, where)
            self.case.assertEqual(len(actual), len(expected), where)
            for index, (av, ev) in enumerate(zip(actual, expected)):
                self.reconcile(av, ev, f'{where}[{index}]')
        elif isinstance(expected, str):
            self.case.assertIsInstance(actual, str, where)
            self.bind(actual, expected)
        else:
            self.case.assertEqual(json.dumps(actual), json.dumps(expected), where)

    def substitute(self, value):
        if isinstance(value, dict):
            return {self.mapping.get(k, k): self.substitute(v) for k, v in value.items()}
        if isinstance(value, list):
            return [self.substitute(v) for v in value]
        if isinstance(value, str):
            return self.mapping.get(value, value)
        return value

    def name(self, text):
        for actual, expected in self.mapping.items():
            text = text.replace(actual, expected)
        return text

    def bind_names(self, actual_names, expected_names):
        """Pair file names differing only by a fresh UUID (lodge.recovery-*, snapshots/*)."""
        unmatched_actual = [n for n in actual_names if n not in expected_names]
        unmatched_expected = [n for n in expected_names if n not in actual_names]
        pattern = re.compile('[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}')
        for actual, expected in zip(unmatched_actual, unmatched_expected):
            a, e = pattern.findall(actual), pattern.findall(expected)
            self.case.assertEqual((pattern.sub('*', actual), len(a)), (pattern.sub('*', expected), len(e)), (actual, expected))
            for pair in zip(a, e):
                self.bind(*pair)
        self.case.assertEqual(len(unmatched_actual), len(unmatched_expected), (unmatched_actual, unmatched_expected))


def native(op, directory, args=None, env=None):
    environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
    environment.update(env or {})
    done = subprocess.run([DRIVER, op, str(directory)], input=json.dumps(args or {}).encode(),
                          capture_output=True, timeout=120, env=environment)
    if done.returncode != 0:
        raise AssertionError(f'driver failed: {done.returncode} {done.stderr!r} {done.stdout!r}')
    kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
    return kind, rest


class StoreOpsCase(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        os.environ.pop('C2_PROFILE_PROBE', None)
        self.base = Path(self.temp.name).resolve()
        self.store = Store(self.base / 'Lodge')
        self.twin = self.base / 'Twin'

    def make_twin(self):
        shutil.rmtree(self.twin, ignore_errors=True)
        if self.store.directory.exists():
            shutil.copytree(self.store.directory, self.twin, symlinks=True)
        else:
            self.twin.mkdir()

    def run_reference(self, reference):
        try:
            return 'ok', reference()
        except FrontendError as error:
            return 'frontend', str(error)
        except OSError as error:
            return 'oserror', error

    def compare_stores(self, volatile, encoding=transaction_bytes):
        """Twin must equal the reference store: names, manifest, backups, snapshots."""
        expected_names = sorted(p.relative_to(self.store.directory).as_posix() for p in self.store.directory.rglob('*'))
        volatile.bind_names(sorted(volatile.name(p.relative_to(self.twin).as_posix()) for p in self.twin.rglob('*')), expected_names)
        actual_paths = {volatile.name(p.relative_to(self.twin).as_posix()): p for p in self.twin.rglob('*')}
        self.assertEqual(sorted(actual_paths), expected_names)
        for name in expected_names:
            expected_path, actual_path = self.store.directory / name, actual_paths[name]
            self.assertEqual(actual_path.is_dir(), expected_path.is_dir(), name)
            if expected_path.is_dir():
                continue
            expected_bytes, actual_bytes = expected_path.read_bytes(), actual_path.read_bytes()
            if name == 'lodge.json' and expected_bytes != actual_bytes:
                # Exact reference encoding of the native content, then the
                # reconciled content itself (order preserved through dumps).
                actual_value = loads(actual_bytes)
                self.assertEqual(actual_bytes, encoding(actual_value), name)
                volatile.reconcile(actual_value, loads(expected_bytes), 'lodge.json')
                self.assertEqual(json.dumps(volatile.substitute(actual_value)), json.dumps(loads(expected_bytes)))
            else:
                self.assertEqual(actual_bytes, expected_bytes, name)
        self.assertFalse((self.twin / 'lodge.lock').exists())

    def both(self, op, args, reference, encoding=transaction_bytes, env=None):
        """Reference on the store, native on a twin; compare result and bytes."""
        self.make_twin()
        expected = self.run_reference(reference)
        before = datetime.now(timezone.utc)
        kind, rest = native(op, self.twin, args, env)
        after = datetime.now(timezone.utc)
        volatile = Volatile(self, (before, after))
        if expected[0] == 'ok':
            self.assertEqual(kind, 'ok', rest)
            actual = json.loads(rest)
            volatile.reconcile(actual, expected[1])
            self.assertEqual(json.dumps(volatile.substitute(actual)), json.dumps(expected[1]))
        elif expected[0] == 'oserror':
            self.assertEqual(kind, 'oserror', rest)
        else:
            # Refusal texts may name the store directory; the twin lives elsewhere.
            self.assertEqual((kind, rest.replace(str(self.twin), str(self.store.directory))), expected)
        self.compare_stores(volatile, encoding)
        return expected[1], volatile

    def hunter(self, action, identity=None, name=None):
        def reference():
            with self.store.transaction() as data:
                return hunter(data, action, identity, name)
        return self.both('hunter', {'action': action, 'id': identity, 'name': name}, reference)


class HunterTests(StoreOpsCase):
    def test_create_select_rename_archive_match_reference_bytes(self):
        created, volatile = self.hunter('create', name='Hunter Ω')
        self.assertTrue(is_uuid(created['id']))
        # Both stores now carry different identities; continue from the
        # reference store so later twins share exact bytes again.
        identity = created['id']
        second = self.hunter('create', name='Second')[0]['id']
        self.assertEqual(self.store.read()['active_hunter'], second)
        self.hunter('select', identity)
        self.assertEqual(self.store.read()['active_hunter'], identity)
        self.hunter('rename', identity, ' Renamed ')
        self.assertEqual(self.store.read()['hunters'][identity]['name'], ' Renamed ')
        self.hunter('archive', identity)
        self.assertIsNone(self.store.read()['active_hunter'])
        self.assertIn('archived_at', self.store.read()['hunters'][identity])
        # Archiving a hunter that is not active leaves the active one alone.
        self.hunter('select', second)
        third = self.hunter('create', name='Third')[0]['id']
        self.hunter('archive', second)
        self.assertEqual(self.store.read()['active_hunter'], third)

    def test_refusals_write_nothing(self):
        identity = self.hunter('create', name='A')[0]['id']
        self.hunter('archive', identity)
        before = self.store.path.read_bytes()
        for action, who, name in (('create', None, None), ('create', None, ' \t\n'), ('rename', identity, ''),
                                  ('rename', None, 'x'), ('select', None, None), ('select', 'not-a-uuid', None),
                                  ('archive', str(uuid.uuid4()), None), ('select', identity, None),
                                  ('list', identity, None), ('list', None, None), ('rename', str(uuid.uuid4()), 'x')):
            with self.subTest(action=action, who=who, name=name):
                expected = self.hunter(action, who, name)[0]
                self.assertIsInstance(expected, str)
        self.assertEqual(self.store.path.read_bytes(), before)

    def test_unchanged_transaction_writes_nothing(self):
        identity = self.hunter('create', name='A')[0]['id']
        stat = self.store.path.stat()
        self.hunter('rename', identity, 'A')
        self.assertEqual(self.store.path.stat().st_mtime_ns, stat.st_mtime_ns)
        self.assertFalse(self.store.path.with_suffix('.json.bak').exists())

    def test_lock_held_refuses_without_writing(self):
        identity = self.hunter('create', name='A')[0]['id']
        self.make_twin()
        (self.twin / 'lodge.lock').write_bytes(b'{"pid": 1}')
        before = (self.twin / 'lodge.json').read_bytes()
        kind, rest = native('hunter', self.twin, {'action': 'rename', 'id': identity, 'name': 'B'})
        self.assertEqual((kind, rest), ('frontend', LOCK_MESSAGE.format(self.twin / 'lodge.lock')))
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        self.assertEqual((self.twin / 'lodge.lock').read_bytes(), b'{"pid": 1}')

    def test_injected_write_failure_keeps_prior_manifest(self):
        identity = self.hunter('create', name='A')[0]['id']
        # Temporary phases name the .pending-* file, so they are unfiltered:
        # the first write inside a transaction is the lodge.json.bak copy.
        for phase, bak in (('temp_write', False), ('replace:lodge.json.bak', False), ('temp_fsync', False),
                           ('replace:lodge.json', True), ('directory_fsync', True)):
            with self.subTest(phase=phase):
                self.make_twin()
                before = sorted((p.name, p.read_bytes()) for p in self.twin.iterdir())
                manifest = (self.twin / 'lodge.json').read_bytes()
                kind, rest = native('hunter', self.twin, {'action': 'rename', 'id': identity, 'name': 'B'},
                                    {'C2_TEST_WRITE_FAILURE': phase})
                self.assertEqual(kind, 'oserror', rest)
                self.assertEqual((self.twin / 'lodge.json').read_bytes(), manifest)
                self.assertFalse(list(self.twin.glob('.pending-*')))
                self.assertFalse((self.twin / 'lodge.lock').exists())
                if bak:
                    # As in the reference, the backup copy precedes the failed replacement.
                    before.append(('lodge.json.bak', manifest))
                self.assertEqual(sorted((p.name, p.read_bytes()) for p in self.twin.iterdir()), sorted(before))


class HostSettingsTests(StoreOpsCase):
    def settings(self, text):
        def reference():
            with self.store.transaction() as data:
                value = json.loads(text)
                if not isinstance(value, dict) or set(value) - {'display', 'audio', 'input'} or any(not isinstance(v, dict) for v in value.values()):
                    raise FrontendError('settings must be objects keyed by display, audio and/or input')
                data['host_settings'].update(value)
                return data['host_settings']
        return self.both('host-settings', {'json': text}, reference)

    def test_update_merges_in_order_and_preserves_kinds(self):
        self.settings('{"display": {"width": 1024, "scale": 1.5, "vsync": true}}')
        self.settings('{"input": {"invert": false}, "display": {"width": 1280, "note": "é\\ud83d\\ude00"}}')
        self.settings('{"audio": {"volume": 0}, "display": {"nested": {"a": [1, 2.0, null]}}}')
        self.settings('{}')
        result = self.store.read()['host_settings']
        self.assertEqual(list(result), ['display', 'input', 'audio'])
        self.assertEqual(result['display'], {'nested': {'a': [1, 2.0, None]}})

    def test_refusals_write_nothing(self):
        self.settings('{"display": {}}')
        before = self.store.path.read_bytes()
        for text in ('[]', '"display"', '{"video": {}}', '{"display": 1}', '{"display": {}, "audio": []}', '{"display": null}'):
            with self.subTest(text=text):
                self.assertEqual(self.settings(text)[0], 'settings must be objects keyed by display, audio and/or input')
        self.assertEqual(self.store.path.read_bytes(), before)

    def test_undecodable_json_is_refused_natively(self):
        self.settings('{"display": {}}')
        self.make_twin()
        before = (self.twin / 'lodge.json').read_bytes()
        kind, rest = native('host-settings', self.twin, {'json': '{"display": '})
        self.assertEqual(kind, 'frontend', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        # Duplicate keys: the reference silently keeps the last; native refuses (documented).
        kind, rest = native('host-settings', self.twin, {'json': '{"display": {}, "display": {"a": 1}}'})
        self.assertEqual(kind, 'frontend', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)


class RecoverBackupTests(StoreOpsCase):
    def recover(self, env=None):
        def reference():
            self.store.restore_backup()
            return {'result': 'backup-restored'}
        return self.both('recover-backup', {}, reference, env=env)

    def test_damaged_and_missing_manifest_recovery(self):
        self.hunter('create', name='One')
        self.hunter('create', name='Two')
        self.store.path.write_text('{"schema_version":1,"schema_version":1}')
        self.recover()
        self.assertEqual(len(self.store.read()['hunters']), 1)
        self.assertEqual(len(list(self.store.directory.glob('lodge.recovery-*'))), 1)
        self.hunter('create', name='Three')
        self.store.path.unlink()
        self.recover()
        self.assertEqual(len(list(self.store.directory.glob('lodge.recovery-*'))), 1)
        # A valid current manifest is preserved as a recovery copy as well.
        self.recover()
        self.assertEqual(len(list(self.store.directory.glob('lodge.recovery-*'))), 2)

    def test_upgraded_stores_and_missing_backups_are_refused(self):
        self.hunter('create', name='One')
        message = 'managed-state backup restore requires explicit future recovery; no history rollback'
        (self.store.directory / UPGRADE_BACKUP).write_bytes(self.store.path.read_bytes())
        self.assertEqual(self.recover()[0], message)
        (self.store.directory / UPGRADE_BACKUP).unlink()
        self.hunter('create', name='Two')
        upgraded = self.store.read()
        upgraded.update(schema_version=2, state_upgrade={'from_version': 1, 'at': 'x', 'backup': UPGRADE_BACKUP, 'backup_sha256': '0' * 64})
        self.store.path.write_bytes(transaction_bytes(upgraded))
        self.assertEqual(self.recover()[0], message)
        self.store.path.with_suffix('.json.bak').write_bytes(transaction_bytes(upgraded))
        self.store.path.write_text('damaged')
        self.assertEqual(self.recover()[0], message)
        backup = self.store.path.with_suffix('.json.bak')
        backup.write_bytes(transaction_bytes({'schema_version': 3, 'hunters': {}, 'active_hunter': None, 'instances': {}, 'associations': {}, 'host_settings': {}}))
        self.assertEqual(self.recover()[0], 'unsupported manifest schema; explicit migration required')
        backup.unlink()
        # Missing backup: the reference wraps FileNotFoundError; native reproduces it.
        expected = self.recover()[0]
        self.assertTrue(expected.startswith('cannot read manifest: [Errno 2]'), expected)
        backup.write_text('not json')
        self.make_twin()
        kind, rest = native('recover-backup', self.twin)
        self.assertEqual(kind, 'frontend', rest)
        self.assertTrue(rest.startswith('cannot read manifest: '), rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), b'damaged')

    def test_lock_and_injected_failures(self):
        self.hunter('create', name='One')
        self.hunter('create', name='Two')
        self.make_twin()
        (self.twin / 'lodge.lock').write_bytes(b'held')
        before = sorted((p.name, p.read_bytes()) for p in self.twin.iterdir())
        kind, rest = native('recover-backup', self.twin)
        self.assertEqual((kind, rest), ('frontend', LOCK_MESSAGE.format(self.twin / 'lodge.lock')))
        self.assertEqual(sorted((p.name, p.read_bytes()) for p in self.twin.iterdir()), before)
        for phase in ('replace:lodge.json', 'temp_write'):
            with self.subTest(phase=phase):
                self.make_twin()
                manifest = (self.twin / 'lodge.json').read_bytes()
                kind, rest = native('recover-backup', self.twin, env={'C2_TEST_WRITE_FAILURE': phase})
                self.assertEqual(kind, 'oserror', rest)
                self.assertEqual((self.twin / 'lodge.json').read_bytes(), manifest)
                self.assertFalse((self.twin / 'lodge.lock').exists())
                self.assertFalse(list(self.twin.glob('.pending-*')))


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
