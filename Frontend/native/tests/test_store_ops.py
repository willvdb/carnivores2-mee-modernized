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
import hashlib
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
from lodge.discovery import discover, get_instance, move_candidates, refresh_instance, register, relocate  # noqa: E402
from lodge.managed_state import UPGRADE_BACKUP, inspect_history, resolve_generation, upgrade_store  # noqa: E402
from lodge.profiles import associate  # noqa: E402
from lodge.session_io import encode  # noqa: E402
from lodge.store import FrontendError, Store, _unique_object, hunter, validate  # noqa: E402
from support import game  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

DRIVER, PROBE = sys.argv[1:3]
TIMESTAMP = re.compile(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{6})?\+00:00')
UUID = re.compile('[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}')
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
        elif UUID.search(expected):
            # Embedded identities (snapshots/<uuid>, lodge.recovery-<uuid>.json).
            a, e = UUID.findall(actual), UUID.findall(expected)
            self.case.assertEqual((UUID.sub('*', actual), len(a)), (UUID.sub('*', expected), len(e)), (actual, expected))
            for pair in zip(a, e):
                self.bind(*pair)
            return
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
            return {self.name(k): self.substitute(v) for k, v in value.items()}
        if isinstance(value, list):
            return [self.substitute(v) for v in value]
        if isinstance(value, str):
            return self.name(value)
        return value

    def name(self, text):
        for actual, expected in self.mapping.items():
            text = text.replace(actual, expected)
        return text

    def bind_names(self, actual_names, expected_names):
        """Pair file names differing only by a fresh UUID (lodge.recovery-*, snapshots/*)."""
        unmatched_actual = [n for n in actual_names if n not in expected_names]
        unmatched_expected = [n for n in expected_names if n not in actual_names]
        self.case.assertEqual(len(unmatched_actual), len(unmatched_expected), (unmatched_actual, unmatched_expected))
        for actual, expected in zip(unmatched_actual, unmatched_expected):
            self.bind(actual, expected)


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
            for twin, store in ((str(self.twin), str(self.store.directory)),
                                (repr(str(self.twin))[1:-1], repr(str(self.store.directory))[1:-1])):
                rest = rest.replace(twin, store)
            self.assertEqual((kind, rest), expected)
        self.compare_stores(volatile, encoding)
        return expected[1], volatile

    def hunter(self, action, identity=None, name=None):
        def reference():
            with self.store.transaction() as data:
                return hunter(data, action, identity, name)
        return self.both('hunter', {'action': action, 'id': identity, 'name': name}, reference)

    def register(self, path, mode='registered', dialect='unknown', family=None, release=None, managed_root=None):
        def reference():
            with self.store.transaction() as data:
                return register(data, path, mode, dialect, family, release, managed_root)
        args = {'path': str(path), 'mode': mode, 'dialect': dialect, 'family': family, 'release': release,
                'managed_root': None if managed_root is None else str(managed_root)}
        return self.both('register', args, reference)

    def relocate(self, identity, path):
        def reference():
            with self.store.transaction() as data:
                return relocate(data, identity, path)
        return self.both('relocate', {'id': identity, 'path': str(path)}, reference)

    def refresh(self, identity):
        def reference():
            with self.store.transaction() as data:
                return refresh_instance(get_instance(data, identity))
        return self.both('refresh', {'id': identity}, reference)

    def discover(self, path):
        def reference():
            data = self.store.read()
            results = discover(path)
            for result in results:
                if result['recognized']:
                    result['possible_moves'] = move_candidates(data, result['path'])
            return results
        return self.both('discover', {'path': str(path)}, reference)

    def discover_register(self, path):
        def reference():
            with self.store.transaction() as data:
                return [register(data, result['path'], 'managed', managed_root=path)
                        for result in discover(path) if result['recognized']]
        return self.both('discover-register', {'path': str(path)}, reference)

    def edit(self, mutate):
        """Author manifest state through the reference (twins are copied afterwards)."""
        with self.store.transaction() as data:
            return mutate(data)

    def associate(self, hunter_id, instance_id, state_key, origin='personal', ownership=None, probe=PROBE):
        def reference():
            with self.store.transaction() as data:
                return associate(self.store, data, hunter_id, instance_id, state_key, origin, ownership, probe)
        args = {'hunter': hunter_id, 'instance': instance_id, 'state_key': state_key, 'origin': origin,
                'ownership': ownership, 'probe': probe}
        return self.both('associate', args, reference)


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
        phases = [('temp_write', False), ('replace:lodge.json.bak', False), ('temp_fsync', False), ('replace:lodge.json', True)]
        if os.name == 'posix':
            phases.append(('directory_fsync', True))
        else:
            # Neither implementation fsyncs directories on Windows: the phase never fires.
            self.make_twin()
            kind, rest = native('hunter', self.twin, {'action': 'rename', 'id': identity, 'name': 'B'},
                                {'C2_TEST_WRITE_FAILURE': 'directory_fsync'})
            self.assertEqual(kind, 'ok', rest)
            self.assertEqual(loads((self.twin / 'lodge.json').read_bytes())['hunters'][identity]['name'], 'B')
        for phase, bak in phases:
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


FOREIGN = 'foreign or ambiguous path; supply an explicit native location'
if os.name == 'nt':
    # native_path on NT rejects a drive without a root and a root without a drive.
    FOREIGN_ABSOLUTE, FOREIGN_RELATIVE, FOREIGN_ROOT, FOREIGN_OTHER = '/Games/Foreign', 'C:Games\\Foreign', 'C:Games', '/Games'
    FOREIGN_FLAVOR, FOREIGN_LOCATOR, FOREIGN_MANAGED, FOREIGN_MANAGED_ROOT = 'posix', '/games/foreign', '/games/managed/foreign', '/games/managed'
else:
    FOREIGN_ABSOLUTE, FOREIGN_RELATIVE, FOREIGN_ROOT, FOREIGN_OTHER = 'C:\\Games\\Foreign', 'Games\\Foreign', 'C:\\Games', 'D:\\Games'
    FOREIGN_FLAVOR, FOREIGN_LOCATOR, FOREIGN_MANAGED, FOREIGN_MANAGED_ROOT = 'nt', 'C:\\Games\\Foreign', 'C:\\Games\\Managed\\Foreign', 'C:\\Games\\Managed'


class RegisterTests(StoreOpsCase):
    def setUp(self):
        super().setUp()
        self.expeditions = self.base / 'Expeditions'
        self.root = game(self.expeditions, 'Triassic')

    def test_registered_managed_and_idempotent_registration(self):
        instance = self.register(self.root, dialect='c2-classic', family='Carnivores 2', release='1.04')[0]
        self.assertEqual((instance['identity_evidence'], instance['managed_root']), ('user assertion', None))
        self.assertEqual(self.register(self.root)[0]['id'], instance['id'])
        # Same root spelled through a symlinked parent still resolves to the instance.
        alias = self.base / 'alias'
        alias.symlink_to(self.expeditions, target_is_directory=True)
        self.assertEqual(self.register(alias / 'Triassic', 'managed', managed_root=self.expeditions)[0]['id'], instance['id'])
        second = game(self.expeditions, 'Managed')
        managed = self.register(second, 'managed', 'mee-newer', managed_root=self.expeditions)[0]
        self.assertEqual(managed['managed_root'], {'path': str(self.expeditions), 'path_flavor': os.name})
        self.assertEqual(managed['identity_evidence'], 'user assertion')
        third = game(self.base, 'Plain')
        self.assertEqual(self.register(third, family='')[0]['identity_evidence'], 'unresolved')
        self.assertEqual(len(self.store.read()['instances']), 3)

    def test_refusals_write_nothing(self):
        self.register(self.root)
        before = self.store.path.read_bytes()
        incoherent = self.base / 'Incoherent'
        (incoherent / 'HUNTDAT').mkdir(parents=True)
        cases = [(dict(path=self.root, mode='owned'), 'invalid installation mode or dialect'),
                 (dict(path=self.root, dialect='mee'), 'invalid installation mode or dialect'),
                 (dict(path=self.root, mode='managed'), 'managed installation requires an explicit Expeditions directory'),
                 (dict(path=self.root, mode='managed', managed_root=self.root), 'managed installation must be below the existing explicit Expeditions directory'),
                 (dict(path=self.root, mode='managed', managed_root=self.base / 'absent'), 'managed installation must be below the existing explicit Expeditions directory'),
                 (dict(path=self.root, mode='managed', managed_root=self.base / 'Expeditions-other'), 'managed installation must be below the existing explicit Expeditions directory'),
                 (dict(path=self.base / 'missing'), None), (dict(path=incoherent), None),
                 (dict(path=Path(FOREIGN_ABSOLUTE)), FOREIGN), (dict(path=Path(FOREIGN_RELATIVE)), FOREIGN),
                 (dict(path=self.root, mode='managed', managed_root=Path(FOREIGN_ROOT)), FOREIGN)]
        (self.base / 'Expeditions-other').mkdir()
        for kwargs, message in cases:
            with self.subTest(**{k: str(v) for k, v in kwargs.items()}):
                expected = self.register(**kwargs)[0]
                self.assertIsInstance(expected, str)
                if message:
                    self.assertEqual(expected, message)
                else:
                    self.assertTrue(expected.startswith('not a coherent installation: [{'), expected)
        self.assertEqual(self.store.path.read_bytes(), before)

    def test_lock_and_injected_failure(self):
        self.register(self.root)
        second = game(self.base, 'Second')
        self.make_twin()
        before = (self.twin / 'lodge.json').read_bytes()
        (self.twin / 'lodge.lock').write_bytes(b'')
        kind, rest = native('register', self.twin, {'path': str(second)})
        self.assertEqual((kind, rest), ('frontend', LOCK_MESSAGE.format(self.twin / 'lodge.lock')))
        (self.twin / 'lodge.lock').unlink()
        kind, rest = native('register', self.twin, {'path': str(second)}, {'C2_TEST_WRITE_FAILURE': 'replace:lodge.json'})
        self.assertEqual(kind, 'oserror', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        self.assertFalse((self.twin / 'lodge.lock').exists())


class RelocateTests(StoreOpsCase):
    def setUp(self):
        super().setUp()
        self.expeditions = self.base / 'Expeditions'
        self.root = game(self.expeditions, 'Triassic')

    def test_managed_rename_outside_and_back(self):
        instance = self.register(self.root, 'managed', managed_root=self.expeditions)[0]
        moved = self.expeditions / 'Triassic MEE'
        self.root.rename(moved)
        result = self.relocate(instance['id'], moved)[0]
        self.assertEqual((result['mode'], result['path']), ('managed', str(moved)))
        self.assertEqual(result['previous_locations'], [{'path': str(self.root), 'path_flavor': os.name}])
        self.assertNotIn('engine_relocation_reviews', result)
        outside = self.base / 'Expeditions-external'
        moved.rename(outside)
        self.assertEqual(self.relocate(instance['id'], outside)[0]['mode'], 'registered')
        outside.rename(self.root)
        result = self.relocate(instance['id'], self.root)[0]
        self.assertEqual((result['mode'], len(result['previous_locations'])), ('registered', 3))
        self.assertEqual(result['managed_root']['path'], str(self.expeditions))

    def test_registered_rename_and_engine_review_history(self):
        instance = self.register(self.root)[0]
        moved = self.expeditions / 'Renamed'
        self.root.rename(moved)
        (moved / 'CARN2.EXE').write_bytes(b'patched launcher bytes')
        result = self.relocate(instance['id'], moved)[0]
        self.assertEqual((result['mode'], result['managed_root']), ('registered', None))
        review = result['engine_relocation_reviews'][0]
        self.assertEqual((review['status'], review['from']['path'], review['to']['path']), ('required', str(self.root), str(moved)))
        self.assertEqual(review['baseline_engine_evidence'], instance['engine_evidence'])
        self.assertNotEqual(review['destination_engine_evidence'], instance['engine_evidence'])
        observed = self.refresh(instance['id'])[0]
        self.assertTrue(observed['engine_review_required'])
        self.assertEqual(observed['diagnostics'][-1]['code'], 'engine-review-required')
        # Returning to the baseline bytes does not erase the pending review.
        moved.rename(self.root)
        (self.root / 'CARN2.EXE').write_bytes(b'synthetic executable evidence - never run')
        result = self.relocate(instance['id'], self.root)[0]
        self.assertEqual(len(result['engine_relocation_reviews']), 1)
        self.assertEqual(len(result['previous_locations']), 2)
        self.assertTrue(self.refresh(instance['id'])[0]['engine_review_required'])

    def test_refusals_write_nothing(self):
        managed = self.register(self.root, 'managed', managed_root=self.expeditions)[0]
        other = game(self.base, 'Other')
        registered = self.register(other)[0]
        legacy = self.edit(lambda d: register(d, game(self.expeditions, 'Legacy'), 'managed', managed_root=self.expeditions))
        self.edit(lambda d: d['instances'][legacy['id']].pop('managed_root'))
        foreign = copy.deepcopy(registered)
        foreign.update(id=str(uuid.uuid4()), path=FOREIGN_LOCATOR, path_flavor=FOREIGN_FLAVOR)
        foreign_managed = copy.deepcopy(foreign)
        foreign_managed.update(id=str(uuid.uuid4()), path=FOREIGN_MANAGED, mode='managed',
                               managed_root={'path': FOREIGN_MANAGED_ROOT, 'path_flavor': FOREIGN_FLAVOR})
        self.edit(lambda d: d['instances'].update({foreign['id']: foreign, foreign_managed['id']: foreign_managed}))
        (self.expeditions / 'Legacy').rename(self.base / 'Legacy moved')
        before = self.store.path.read_bytes()
        moved = self.expeditions / 'Moved'
        cases = [(str(uuid.uuid4()), moved, 'unknown instance ID'),
                 (managed['id'], moved, 'old installation still exists; this could be a clone, not a move'),
                 (managed['id'], Path(FOREIGN_OTHER), FOREIGN),
                 (foreign['id'], other, 'destination already registered'),
                 (foreign_managed['id'], moved, 'foreign managed root requires explicit ownership reconciliation'),
                 (legacy['id'], self.base / 'Legacy moved', 'managed root context missing; explicit ownership reconciliation required')]
        for identity, destination, message in cases:
            with self.subTest(message=message):
                self.assertEqual(self.relocate(identity, destination)[0], message)
        self.root.rename(moved)
        (moved / 'HUNTDAT/_RES.TXT').write_text('changed content')
        self.assertEqual(self.relocate(managed['id'], moved)[0], 'relocation requires a coherent root with matching content revision')
        shutil.rmtree(moved / 'HUNTDAT/MENU')
        self.assertEqual(self.relocate(managed['id'], moved)[0], 'relocation requires a coherent root with matching content revision')
        self.assertEqual(self.relocate(managed['id'], self.base / 'absent')[0], 'relocation requires a coherent root with matching content revision')
        self.assertEqual(self.store.path.read_bytes(), before)
        # Foreign instances can be relocated onto a native root.
        other.rename(self.base / 'Other moved')
        result = self.relocate(foreign['id'], self.base / 'Other moved')[0]
        self.assertEqual((result['path_flavor'], result['previous_locations']), (os.name, [{'path': FOREIGN_LOCATOR, 'path_flavor': FOREIGN_FLAVOR}]))

    def test_managed_root_missing_or_retargeted_is_ambiguous(self):
        instance = self.register(self.root, 'managed', managed_root=self.expeditions)[0]
        moved = self.base / 'Moved'
        self.root.rename(moved)
        message = 'managed root missing or ambiguous; explicit ownership reconciliation required'
        real = self.base / 'Real'
        self.expeditions.rename(real)
        self.assertEqual(self.relocate(instance['id'], moved)[0], message)
        self.expeditions.symlink_to(real, target_is_directory=True)
        self.assertEqual(self.relocate(instance['id'], moved)[0], message)
        self.assertEqual(self.relocate(instance['id'], self.expeditions)[0], message)
        self.expeditions.unlink()
        real.rename(self.expeditions)
        self.assertEqual(self.relocate(instance['id'], moved)[0]['mode'], 'registered')


class RefreshTests(StoreOpsCase):
    def test_unchanged_changed_engine_and_missing(self):
        root = game(self.base)
        instance = self.register(root, 'registered', 'c2-classic')[0]
        observed = self.refresh(instance['id'])[0]
        self.assertEqual((observed['recognized'], observed['revision_changed'], observed['engine_changed']), (True, False, False))
        self.assertIn('last_observation', self.store.read()['instances'][instance['id']])
        (root / 'HUNTDAT/AREAS/AREA2.MAP').write_bytes(b'new area')
        (root / 'HUNTDAT/AREAS/AREA2.RSC').write_bytes(b'new resources')
        observed = self.refresh(instance['id'])[0]
        self.assertTrue(observed['revision_changed'])
        current = self.store.read()['instances'][instance['id']]
        self.assertEqual((current['revision'], len(current['revisions'])), (observed['revision'], 2))
        # Returning to an earlier revision selects it without duplicating history.
        (root / 'HUNTDAT/AREAS/AREA2.MAP').unlink()
        (root / 'HUNTDAT/AREAS/AREA2.RSC').unlink()
        self.assertTrue(self.refresh(instance['id'])[0]['revision_changed'])
        self.assertEqual(len(self.store.read()['instances'][instance['id']]['revisions']), 2)
        (root / 'CARN2.EXE').write_bytes(b'different engine bytes')
        observed = self.refresh(instance['id'])[0]
        self.assertEqual((observed['engine_changed'], observed['engine_review_required']), (True, True))
        shutil.rmtree(root)
        observed = self.refresh(instance['id'])[0]
        self.assertEqual((observed['recognized'], observed['diagnostics'][0]['code']), (False, 'missing-installation'))
        self.assertEqual(self.refresh(str(uuid.uuid4()))[0], 'unknown instance ID')

    def test_foreign_instance_and_unchanged_observation(self):
        root = game(self.base)
        instance = self.register(root)[0]
        foreign = copy.deepcopy(instance)
        foreign.update(id=str(uuid.uuid4()), path=FOREIGN_LOCATOR, path_flavor=FOREIGN_FLAVOR)
        self.edit(lambda d: d['instances'].update({foreign['id']: foreign}))
        observed = self.refresh(foreign['id'])[0]
        self.assertEqual(observed['diagnostics'][0]['code'], 'foreign-path')
        self.refresh(instance['id'])
        stat = self.store.path.stat()
        self.refresh(instance['id'])
        self.assertEqual(self.store.path.stat().st_mtime_ns, stat.st_mtime_ns)


class DiscoverTests(StoreOpsCase):
    def setUp(self):
        super().setUp()
        self.expeditions = self.base / 'Expeditions'
        self.first = game(self.expeditions, 'First')
        self.second = game(self.expeditions / 'Nested', 'Second')
        (self.second / 'HUNTDAT/AREAS/AREA2.MAP').write_bytes(b'distinct content revision')
        (self.expeditions / 'Partial/HUNTDAT').mkdir(parents=True)

    def test_view_reports_possible_moves_only_for_recognized_roots(self):
        instance = self.register(self.first)[0]
        results = self.discover(self.expeditions)[0]
        self.assertEqual([r['path'] for r in results], [str(self.first), str(self.second), str(self.expeditions / 'Partial')])
        self.assertEqual([r.get('possible_moves') for r in results], [[], [], None])
        moved = self.expeditions / 'Moved'
        self.first.rename(moved)
        results = self.discover(self.expeditions)[0]
        self.assertEqual([r.get('possible_moves') for r in results], [[instance['id']], [], None])
        self.assertEqual(self.discover(self.base / 'absent')[0], [])

    def test_register_managed_registers_every_recognized_root_once(self):
        results = self.discover_register(self.expeditions)[0]
        self.assertEqual([r['path'] for r in results], [str(self.first), str(self.second)])
        self.assertEqual({r['mode'] for r in results}, {'managed'})
        self.assertEqual({r['managed_root']['path'] for r in results}, {str(self.expeditions)})
        stat = self.store.path.stat()
        again = self.discover_register(self.expeditions)[0]
        self.assertEqual([r['id'] for r in again], [r['id'] for r in results])
        self.assertEqual(self.store.path.stat().st_mtime_ns, stat.st_mtime_ns)
        self.assertEqual(self.discover_register(self.base / 'Empty')[0], [])

    def test_failure_in_the_middle_registers_nothing(self):
        self.register(self.first)
        before = self.store.path.read_bytes()
        (self.second / 'HUNTDAT/link.txt').symlink_to(self.second / 'HUNTDAT/_RES.TXT')
        self.make_twin()
        with self.assertRaises(FrontendError):
            with self.store.transaction() as data:
                [register(data, r['path'], 'managed', managed_root=self.expeditions) for r in discover(self.expeditions) if r['recognized']]
        kind, rest = native('discover-register', self.twin, {'path': str(self.expeditions)})
        self.assertEqual(kind, 'frontend', rest)
        self.assertTrue(rest.startswith('content symlink requires explicit policy: '), rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        self.assertEqual(self.store.path.read_bytes(), before)
        (self.second / 'HUNTDAT/link.txt').unlink()
        kind, rest = native('discover-register', self.twin, {'path': str(self.expeditions)}, {'C2_TEST_WRITE_FAILURE': 'replace:lodge.json'})
        self.assertEqual(kind, 'oserror', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)


class AssociateTests(StoreOpsCase):
    def setUp(self):
        super().setUp()
        self.expeditions = self.base / 'Expeditions'
        self.root = game(self.expeditions, 'Triassic')
        (self.root / 'trophy00.sav').write_bytes(save_bytes())
        (self.root / 'trophy00.sab').write_bytes(room_bytes())
        (self.root / 'trophy03.sav').write_bytes(save_bytes(slot=3, score=7))
        self.hunter_id = self.hunter('create', name='Hunter')[0]['id']
        self.instance = self.register(self.root, dialect='c2-classic')[0]

    def test_referenced_managed_copies_and_bytes(self):
        referenced = self.associate(self.hunter_id, self.instance['id'], 'trophy00')[0]
        self.assertEqual((referenced['ownership'], referenced['authority'], referenced['writable']), ('referenced', 'native-files', False))
        self.assertEqual([f['path'] for f in referenced['files']], ['trophy00.sab', 'trophy00.sav'])
        self.assertNotIn('decoded', referenced['files'][0])
        self.assertEqual(referenced['diagnostics'][-1]['code'], 'pair-coherence-unverified')
        self.assertEqual(self.associate(self.hunter_id, self.instance['id'], 'trophy00')[0],
                         'source already referenced; choose an explicit independent managed copy')
        managed = self.associate(self.hunter_id, self.instance['id'], 'trophy00', ownership='managed')[0]
        self.assertEqual((managed['authority'], managed['snapshot']), ('independent-snapshot', f"snapshots/{managed['id']}"))
        snapshot = self.store.directory / 'snapshots' / managed['id']
        self.assertEqual((snapshot / 'trophy00.sav').read_bytes(), save_bytes())
        self.assertEqual((snapshot / 'trophy00.sab').read_bytes(), room_bytes())
        self.assertEqual(sorted(p.name for p in snapshot.iterdir()), ['trophy00.sab', 'trophy00.sav'])
        # A second independent copy of the same source is permitted.
        self.associate(self.hunter_id, self.instance['id'], 'trophy00', 'bundled-example', 'managed')
        self.assertEqual(len(list((self.store.directory / 'snapshots').iterdir())), 2)
        single = self.associate(self.hunter_id, self.instance['id'], 'trophy03', 'unknown', 'managed')[0]
        self.assertEqual([f['path'] for f in single['files']], ['trophy03.sav'])
        self.assertEqual(len(self.store.read()['associations']), 4)

    def test_default_ownership_helperless_and_iceage(self):
        managed_root = game(self.expeditions, 'Managed')
        (managed_root / 'trophy01.sav').write_bytes(save_bytes(slot=1))
        instance = self.register(managed_root, 'managed', 'iceage-triassic', managed_root=self.expeditions)[0]
        result = self.associate(self.hunter_id, instance['id'], 'trophy01')[0]
        self.assertEqual((result['ownership'], result['snapshot']), ('managed', f"snapshots/{result['id']}"))
        self.assertEqual([d['code'] for d in result['diagnostics']][-3:], ['ownership-unproven', 'unreadable-layout', 'pair-coherence-unverified'])
        result = self.associate(self.hunter_id, self.instance['id'], 'trophy00', probe=None)[0]
        self.assertEqual([d['code'] for d in result['diagnostics']].count('unreadable-layout'), 2)
        self.assertEqual(self.associate(self.hunter_id, self.instance['id'], 'trophy03', ownership='referenced', probe=None)[0]['filename_slot'], 3)

    def test_refusals_in_reference_order_write_nothing(self):
        archived = self.hunter('create', name='Archived')[0]['id']
        self.hunter('archive', archived)
        foreign = copy.deepcopy(self.instance)
        foreign.update(id=str(uuid.uuid4()), path=FOREIGN_LOCATOR, path_flavor=FOREIGN_FLAVOR)
        self.edit(lambda d: d['instances'].update({foreign['id']: foreign}))
        (self.root / 'trophy02.sab').write_bytes(room_bytes())
        (self.root / 'trophy04.sav').write_bytes(save_bytes(slot=4))
        (self.root / 'trophy04.txt').write_bytes(b'companion notes')
        (self.root / 'trophy05.sav').write_bytes(save_bytes(slot=6))
        before = self.store.path.read_bytes()
        cases = [((str(uuid.uuid4()), self.instance['id'], 'trophy00'), {}, 'association requires an active hunter identity'),
                 ((archived, self.instance['id'], 'trophy00'), {}, 'association requires an active hunter identity'),
                 ((self.hunter_id, self.instance['id'], 'trophy00'), {'origin': 'mine'}, 'explicit source declaration required: personal, bundled-example, or unknown'),
                 ((self.hunter_id, str(uuid.uuid4()), 'trophy00'), {}, 'unknown instance ID'),
                 ((self.hunter_id, foreign['id'], 'trophy00'), {}, 'installation unavailable or revision changed; refresh and review before association'),
                 ((self.hunter_id, self.instance['id'], 'trophy00'), {'ownership': 'owned'}, 'invalid ownership mode'),
                 ((self.hunter_id, self.instance['id'], 'trophy09'), {}, 'selected state has no save; orphan rooms are never adopted as profiles'),
                 ((self.hunter_id, self.instance['id'], 'trophy02'), {}, 'selected state has no save; orphan rooms are never adopted as profiles'),
                 ((self.hunter_id, self.instance['id'], 'trophy04'), {'ownership': 'managed'}, 'unclassified companion files require review before managed import; reference-only inspection remains available'),
                 ((self.hunter_id, self.instance['id'], 'trophy05'), {}, 'registration mismatch requires explicit future reconciliation; source unchanged')]
        for args, kwargs, message in cases:
            with self.subTest(message=message):
                self.assertEqual(self.associate(*args, **kwargs)[0], message)
        self.assertEqual(self.store.path.read_bytes(), before)
        self.assertFalse((self.store.directory / 'snapshots').exists())
        # The companion only blocks the managed import; a reference remains available.
        self.assertEqual(self.associate(self.hunter_id, self.instance['id'], 'trophy04')[0]['unclassified_companions'], [{'path': 'trophy04.txt', 'size': 15}])
        (self.root / 'HUNTDAT/AREAS/AREA1.MAP').write_bytes(b'changed map')
        self.assertEqual(self.associate(self.hunter_id, self.instance['id'], 'trophy00')[0],
                         'installation unavailable or revision changed; refresh and review before association')

    def test_schema_two_import_initializes_history(self):
        upgrade_store(self.store)
        self.assertEqual(self.store.read()['schema_version'], 2)
        referenced = self.associate(self.hunter_id, self.instance['id'], 'trophy03')[0]
        self.assertNotIn('managed_state', referenced)
        managed = self.associate(self.hunter_id, self.instance['id'], 'trophy00', ownership='managed')[0]
        history = managed['managed_state']
        generation = history['generations'][history['current_generation']]
        self.assertEqual((managed['authority'], history['schema_version'], generation['sequence'], generation['kind']),
                         ('managed-state-history', 1, 0, 'original-import'))
        self.assertEqual(generation['snapshot'], f"snapshots/{managed['id']}")
        self.assertEqual([m['path'] for m in generation['members']], ['trophy00.sab', 'trophy00.sav'])
        self.assertEqual(generation['provenance']['revision'], self.instance['revision'])
        self.assertEqual(self.store.read()['associations'][managed['id']]['managed_state'], history)

    def test_lock_and_injected_failures(self):
        self.make_twin()
        before = (self.twin / 'lodge.json').read_bytes()
        args = {'hunter': self.hunter_id, 'instance': self.instance['id'], 'state_key': 'trophy00', 'origin': 'personal',
                'ownership': 'managed', 'probe': PROBE}
        (self.twin / 'lodge.lock').write_bytes(b'')
        kind, rest = native('associate', self.twin, args)
        self.assertEqual((kind, rest), ('frontend', LOCK_MESSAGE.format(self.twin / 'lodge.lock')))
        (self.twin / 'lodge.lock').unlink()
        self.assertFalse((self.twin / 'snapshots').exists())
        # A blob write failure leaves only the pending staging directory, never a published snapshot.
        kind, rest = native('associate', self.twin, args, {'C2_TEST_WRITE_FAILURE': 'replace:trophy00.sav'})
        self.assertEqual(kind, 'oserror', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        names = [p.name for p in (self.twin / 'snapshots').iterdir()]
        self.assertTrue(all(n.startswith('.pending-') for n in names), names)
        self.assertFalse((self.twin / 'lodge.lock').exists())
        # A manifest replacement failure retains the manifest; the published copy
        # remains as in the reference (no association references it).
        kind, rest = native('associate', self.twin, args, {'C2_TEST_WRITE_FAILURE': 'replace:lodge.json'})
        self.assertEqual(kind, 'oserror', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        published = [p for p in (self.twin / 'snapshots').iterdir() if is_uuid(p.name)]
        self.assertEqual(len(published), 1)
        self.assertEqual((published[0] / 'trophy00.sav').read_bytes(), save_bytes())


class UpgradeTests(StoreOpsCase):
    def setUp(self):
        super().setUp()
        self.root = game(self.base)
        (self.root / 'trophy00.sav').write_bytes(save_bytes())
        (self.root / 'trophy00.sab').write_bytes(room_bytes())
        (self.root / 'trophy01.sav').write_bytes(save_bytes(slot=1))
        self.hunter_id = self.hunter('create', name='Hunter')[0]['id']
        self.instance = self.register(self.root, dialect='c2-classic')[0]

    def upgrade(self, env=None):
        return self.both('upgrade', {}, lambda: upgrade_store(self.store), encoding=encode, env=env)

    def test_explicit_upgrade_backs_up_and_initializes_history(self):
        managed = self.associate(self.hunter_id, self.instance['id'], 'trophy00', ownership='managed')[0]
        referenced = self.associate(self.hunter_id, self.instance['id'], 'trophy01')[0]
        self.edit(lambda d: (d.update(future_extension={'opaque': [1, 2.5, None]}),
                             d['associations'][managed['id']].update(future_extension='preserve')))
        before = self.store.path.read_bytes()
        result = self.upgrade()[0]
        self.assertEqual((result['result'], result['schema_version'], result['backup']), ('upgraded', 2, UPGRADE_BACKUP))
        self.assertEqual(list(result['associations']), [managed['id']])
        data = self.store.read()
        self.assertEqual((self.store.directory / UPGRADE_BACKUP).read_bytes(), before)
        self.assertEqual(data['state_upgrade']['backup_sha256'], hashlib.sha256(before).hexdigest())
        self.assertEqual(data['future_extension'], {'opaque': [1, 2.5, None]})
        self.assertEqual(data['associations'][managed['id']]['future_extension'], 'preserve')
        self.assertEqual(data['associations'][referenced['id']]['authority'], 'native-files')
        self.assertEqual(data['associations'][managed['id']]['managed_state']['current_generation'], result['associations'][managed['id']])
        self.assertEqual(self.upgrade()[0], {'result': 'already-upgraded', 'schema_version': 2})
        self.assertEqual(self.both('recover-backup', {}, self.store.restore_backup)[0],
                         'managed-state backup restore requires explicit future recovery; no history rollback')

    def test_empty_store_upgrades_from_the_encoded_empty_manifest(self):
        self.store = Store(self.base / 'Empty')
        result = self.upgrade()[0]
        self.assertEqual(result['associations'], {})
        self.assertEqual((self.store.directory / UPGRADE_BACKUP).read_bytes(), encode({'schema_version': 1, 'hunters': {}, 'active_hunter': None, 'instances': {}, 'associations': {}, 'host_settings': {}}))
        self.assertEqual(self.store.read()['schema_version'], 2)

    def test_reserved_metadata_changed_snapshot_and_prior_backup_refuse(self):
        managed = self.associate(self.hunter_id, self.instance['id'], 'trophy00', ownership='managed')[0]
        clean = self.store.path.read_bytes()
        self.edit(lambda d: d.update(state_upgrade={'from_version': 1}))
        self.assertEqual(self.upgrade()[0], 'reserved upgrade metadata already exists')
        self.store.path.write_bytes(clean)
        self.edit(lambda d: d['associations'][managed['id']].update(managed_state={}))
        self.assertEqual(self.upgrade()[0], 'reserved managed_state metadata already exists')
        self.store.path.write_bytes(clean)
        snapshot = self.store.directory / 'snapshots' / managed['id']
        (snapshot / 'trophy00.sav').write_bytes(save_bytes(score=5))
        self.assertEqual(self.upgrade()[0], 'import snapshot differs from provenance; upgrade blocked')
        (snapshot / 'trophy00.sav').write_bytes(save_bytes())
        (snapshot / 'extra.txt').write_bytes(b'')
        self.assertEqual(self.upgrade()[0], 'import snapshot differs from provenance; upgrade blocked')
        (snapshot / 'extra.txt').unlink()
        (self.store.directory / UPGRADE_BACKUP).write_bytes(clean + b'\n')
        self.assertEqual(self.upgrade()[0], 'prior upgrade backup differs; retain for explicit review')
        (self.store.directory / UPGRADE_BACKUP).unlink()
        self.assertEqual(self.store.path.read_bytes(), clean)
        self.store.path.unlink()
        self.assertEqual(self.upgrade()[0], 'manifest missing with backup present; use explicit recovery')
        self.store.path.write_bytes(clean)
        # A prior backup with identical bytes is accepted and reused.
        (self.store.directory / UPGRADE_BACKUP).write_bytes(clean)
        self.assertEqual(self.upgrade()[0]['result'], 'upgraded')

    def test_lock_and_injected_failures_preserve_previous_authority(self):
        self.associate(self.hunter_id, self.instance['id'], 'trophy00', ownership='managed')
        self.make_twin()
        before = (self.twin / 'lodge.json').read_bytes()
        (self.twin / 'lodge.lock').write_bytes(b'')
        kind, rest = native('upgrade', self.twin)
        self.assertEqual((kind, rest), ('frontend', LOCK_MESSAGE.format(self.twin / 'lodge.lock')))
        (self.twin / 'lodge.lock').unlink()
        kind, rest = native('upgrade', self.twin, env={'C2_TEST_WRITE_FAILURE': f'replace:{UPGRADE_BACKUP}'})
        self.assertEqual(kind, 'oserror', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        self.assertFalse((self.twin / UPGRADE_BACKUP).exists())
        kind, rest = native('upgrade', self.twin, env={'C2_TEST_WRITE_FAILURE': 'replace:lodge.json'})
        self.assertEqual(kind, 'oserror', rest)
        self.assertEqual((self.twin / 'lodge.json').read_bytes(), before)
        self.assertEqual((self.twin / UPGRADE_BACKUP).read_bytes(), before)
        self.assertFalse(list(self.twin.glob('.pending-*')))
        self.assertFalse((self.twin / 'lodge.lock').exists())
        # The retained backup is reused by the retry.
        kind, rest = native('upgrade', self.twin)
        self.assertEqual(kind, 'ok', rest)
        self.assertEqual(Store(self.twin).read()['schema_version'], 2)


class NativeOnlySetupTests(unittest.TestCase):
    """A disposable empty store taken through create, register, import and upgrade
    using only native operations. Python authors the game fixture and checks the
    outcome against the reference reader; it never performs an operation."""

    def test_native_stack_builds_an_upgraded_managed_store(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp).resolve()
            root = game(base)
            (root / 'trophy00.sav').write_bytes(save_bytes())
            (root / 'trophy00.sab').write_bytes(room_bytes())
            store = base / 'Lodge'
            kind, rest = native('hunter', store, {'action': 'create', 'name': 'Native hunter'})
            self.assertEqual(kind, 'ok', rest)
            hunter_id = json.loads(rest)['id']
            kind, rest = native('register', store, {'path': str(root), 'dialect': 'c2-classic'})
            self.assertEqual(kind, 'ok', rest)
            instance_id = json.loads(rest)['id']
            kind, rest = native('associate', store, {'hunter': hunter_id, 'instance': instance_id, 'state_key': 'trophy00',
                                                     'origin': 'personal', 'ownership': 'managed', 'probe': PROBE})
            self.assertEqual(kind, 'ok', rest)
            association = json.loads(rest)
            self.assertEqual(association['authority'], 'independent-snapshot')
            kind, rest = native('upgrade', store)
            self.assertEqual(kind, 'ok', rest)
            result = json.loads(rest)
            self.assertEqual(result['result'], 'upgraded')
            reference = Store(store)
            data = reference.read()
            self.assertEqual(data['schema_version'], 2)
            self.assertEqual(data['active_hunter'], hunter_id)
            record = data['associations'][association['id']]
            self.assertEqual(record['authority'], 'managed-state-history')
            self.assertEqual(result['associations'], {association['id']: record['managed_state']['current_generation']})
            generation, snapshot_root, members, blobs = resolve_generation(reference, data, record)
            self.assertEqual(snapshot_root, store / 'snapshots' / association['id'])
            self.assertEqual(blobs, {'trophy00.sab': room_bytes(), 'trophy00.sav': save_bytes()})
            self.assertEqual(inspect_history(reference, association['id'])['current_generation'], generation['id'])
            self.assertEqual(reference.path.read_bytes(), encode(data))
            self.assertEqual((store / UPGRADE_BACKUP).read_bytes(), transaction_bytes(loads((store / UPGRADE_BACKUP).read_bytes())))
            self.assertEqual({p.name: p.read_bytes() for p in root.iterdir() if p.is_file()},
                             {'CARN2.EXE': b'synthetic executable evidence - never run', 'trophy00.sav': save_bytes(), 'trophy00.sab': room_bytes()})


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
