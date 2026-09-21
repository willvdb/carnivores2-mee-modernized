"""Native journal component against lodge.session_io (authored journals, disposable stores)."""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import session_io  # noqa: E402
from lodge.store import FrontendError, Store  # noqa: E402

DRIVER = sys.argv[1]
IDENTITY = '6f1e4a1c-3b52-4c0e-9a57-0d8f5d2b7a11'
OTHER = '0a9b8c7d-1234-4abc-8def-001122334455'
KINDS = {1: 'controlled-synthetic', 2: 'experimental-native-observer-v1',
         3: 'experimental-native-hunt-v1', 4: 'managed-native-hunt-v1'}


def native(*arguments, text=''):
    done = subprocess.run([DRIVER, *arguments], input=text.encode(), capture_output=True, timeout=60, check=True)
    head, _, rest = done.stdout.partition(b'\n')
    return head.decode(), rest


def journal(version=1, state='prepared'):
    path = ['prepared', 'launching', 'running', 'returned', 'inspecting', 'candidate']
    other = {'failed': ['prepared', 'failed'], 'interrupted': ['prepared', 'launching', 'interrupted'],
             'quarantined': path[:5] + ['quarantined']}
    history = path[:path.index(state) + 1] if state in path else other[state]
    member = {'path': 'trophy03.sav', 'type': 'file', 'size': 1660, 'sha256': 'a' * 64}
    room = {'path': 'trophy03.sab', 'type': 'file', 'size': 7176, 'sha256': 'b' * 64}
    value = {'schema_version': version, 'id': IDENTITY, 'path_flavor': os.name, 'state': state,
             'transitions': [{'state': s, 'at': '2026-01-01T00:00:00+00:00'} for s in history],
             'pins': {'hunter_id': OTHER, 'instance_id': OTHER, 'association_id': OTHER, 'selection': {},
                      'codec': {}, 'revision': {}, 'instance': {}, 'association': {}, 'native_slot': 3,
                      'source_members': [member, room], 'unicode': 'café \U0001f996'},
             'execution': {'kind': KINDS[version]}, 'capabilities': {}, 'diagnostics': [],
             'baseline_members': [member], 'score': 1.5, 'big': 2 ** 80}
    if version >= 2:
        value.update(experimental_native_process_launch_allowed=True, process_launch_allowed=False,
                     synthetic_process_launch_allowed=False)
    if version == 4:
        value['pins'].update(generation_id=OTHER, generation={'id': OTHER})
    return value


def edit(value, path, replacement):
    value = copy.deepcopy(value)
    target = value
    for key in path[:-1]:
        target = target[key]
    if replacement is DELETE:
        del target[path[-1]]
    else:
        target[path[-1]] = replacement
    return value


DELETE = object()
MUTATIONS = [
    ((), None), (('schema_version',), 5), (('schema_version',), 0), (('schema_version',), True),
    (('schema_version',), 1.0), (('schema_version',), '1'), (('id',), OTHER), (('id',), None),
    (('path_flavor',), 'nt' if os.name == 'posix' else 'posix'), (('state',), 'done'), (('state',), 3),
    (('transitions',), []), (('transitions',), {}), (('transitions', 0), 'prepared'),
    (('transitions', 0, 'state'), 'launching'), (('transitions', 0, 'at'), 5), (('transitions', 0, 'at'), DELETE),
    (('transitions',), [{'state': 'prepared', 'at': 'x'}, {'state': 'running', 'at': 'x'}]),
    (('transitions',), [{'state': 'prepared', 'at': 'x'}, {'state': 'launching', 'at': 'x'}]),
    (('transitions',), [{'state': 'prepared', 'at': 'x'}, {'state': ['a'], 'at': 'x'}]),
    (('transitions',), [{'state': 'prepared', 'at': 'x'}, 7]),
    (('pins',), []), (('execution',), None), (('capabilities',), DELETE),
    (('execution', 'kind'), 'managed-native-hunt-v1'), (('execution', 'kind'), 'custom'),
    (('execution', 'kind'), ['controlled-synthetic']), (('execution', 'kind'), DELETE),
    (('experimental_native_process_launch_allowed',), 1), (('process_launch_allowed',), 0),
    (('process_launch_allowed',), True), (('synthetic_process_launch_allowed',), DELETE),
    (('diagnostics',), {}), (('pins', 'hunter_id'), 'x'), (('pins', 'instance_id'), OTHER.upper()),
    (('pins', 'association_id'), 9), (('pins', 'selection'), []), (('pins', 'codec'), DELETE),
    (('pins', 'generation_id'), 'x'), (('pins', 'generation'), []), (('pins', 'generation', 'id'), IDENTITY),
    (('pins', 'native_slot'), 8), (('pins', 'native_slot'), -1), (('pins', 'native_slot'), True),
    (('pins', 'native_slot'), 3.0), (('pins', 'native_slot'), 4), (('pins', 'source_members'), []),
    (('pins', 'source_members'), {}), (('baseline_members'), DELETE) if False else (('baseline_members',), DELETE),
    (('baseline_members',), [{'path': 'trophy03.sab', 'type': 'file', 'size': 1, 'sha256': 'c' * 64}]),
    (('baseline_members', 0, 'type'), 'directory'), (('baseline_members', 0, 'path'), 'trophy03.SAV'),
    (('baseline_members', 0, 'path'), ['trophy03.sav']), (('baseline_members', 0, 'size'), -1),
    (('baseline_members', 0, 'size'), 16 * 1024 * 1024), (('baseline_members', 0, 'size'), 16 * 1024 * 1024 + 1),
    (('baseline_members', 0, 'size'), True), (('baseline_members', 0, 'size'), 2 ** 70),
    (('baseline_members', 0, 'sha256'), 'A' * 64), (('baseline_members', 0, 'sha256'), 'a' * 63),
    (('baseline_members', 0, 'sha256'), 'a' * 64 + '\n'), (('baseline_members', 0), 'trophy03.sav'),
    (('pins', 'source_members', 1, 'path'), 'trophy03.sav'),
    (('pins', 'source_members'), [journal()['baseline_members'][0]] * 3),
]


class Journal(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name).resolve() / 'lodge'
        self.root = self.directory / 'sessions' / IDENTITY
        self.root.mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def reference(self, value, identity=IDENTITY):
        try:
            session_io.validate_journal(copy.deepcopy(value), identity)
            return 'ok'
        except FrontendError as error:
            return f'frontend {error}'

    def test_validation_matches_reference_for_every_version(self):
        refused = 0
        for version in (1, 2, 3, 4):
            for state in session_io.TRANSITIONS:
                value = journal(version, state)
                self.assertEqual(self.reference(value), 'ok')
                self.assertEqual(native('validate', IDENTITY, text=json.dumps(value))[0], 'ok')
            for path, replacement in MUTATIONS:
                try:
                    value = edit(journal(version), path, replacement) if path else replacement
                except (KeyError, IndexError, TypeError):
                    continue
                expected = self.reference(value)
                refused += expected != 'ok'
                self.assertEqual(native('validate', IDENTITY, text=json.dumps(value))[0:1], (expected,), (version, path, replacement))
        self.assertGreater(refused, 150)
        self.assertEqual(native('validate', 'not-a-uuid', text=json.dumps(edit(journal(), ('id',), 'not-a-uuid')))[0],
                         self.reference(edit(journal(), ('id',), 'not-a-uuid'), 'not-a-uuid'))

    def test_version_one_accepts_unlisted_kind(self):
        value = edit(journal(1), ('execution', 'kind'), 'custom')
        self.assertEqual(self.reference(value), 'ok')
        self.assertEqual(native('validate', IDENTITY, text=json.dumps(value))[0], 'ok')

    def test_encoding_is_byte_identical(self):
        for version in (1, 2, 3, 4):
            value = journal(version)
            head, rest = native('encode', text=json.dumps(value))
            self.assertEqual((head, rest), ('ok', session_io.encode(value)))

    def test_non_finite_refused_and_nothing_written(self):
        value = journal()
        value['score'] = float('nan')
        with self.assertRaises(ValueError):
            session_io.encode(value)
        self.assertEqual(native('persist', str(self.root), text=json.dumps(value))[0].split(' ')[0], 'frontend')
        self.assertEqual(list(self.root.iterdir()), [])

    def test_persist_then_both_readers_agree(self):
        value = journal(4, 'running')
        self.assertEqual(native('persist', str(self.root), text=json.dumps(value))[0], 'ok')
        self.assertEqual((self.root / 'journal.json').read_bytes(), session_io.encode(value))
        self.assertEqual([p.name for p in self.root.iterdir()], ['journal.json'])
        self.assertEqual(session_io.read_journal(Store(self.directory), IDENTITY), value)
        head, rest = native('read', str(self.directory), IDENTITY)
        self.assertEqual((head, rest), ('ok', session_io.encode(value)))

    def test_persist_refuses_foreign_root_and_links(self):
        foreign = self.directory / 'sessions' / OTHER
        foreign.mkdir()
        self.assertEqual(native('persist', str(foreign), text=json.dumps(journal()))[0],
                         'frontend invalid/foreign session journal; explicit review required')
        self.assertEqual(list(foreign.iterdir()), [])
        if os.name == 'posix':
            target = self.directory / 'elsewhere'
            target.write_text('keep')
            (self.root / 'journal.json').symlink_to(target)
            self.assertEqual(native('persist', str(self.root), text=json.dumps(journal()))[0].split(' ')[0], 'frontend')
            self.assertEqual(target.read_text(), 'keep')

    def test_read_refusals(self):
        self.assertTrue(native('read', str(self.directory), IDENTITY)[0].startswith('frontend cannot read session journal: '))
        self.assertEqual(native('read', str(self.directory), 'nope')[0], 'frontend invalid session UUID')
        path = self.root / 'journal.json'
        path.write_text('{"id": 1, "id": 2}')
        with self.assertRaises(FrontendError) as caught:
            session_io.read_journal(Store(self.directory), IDENTITY)
        self.assertTrue(str(caught.exception).startswith('duplicate JSON key'))
        self.assertTrue(native('read', str(self.directory), IDENTITY)[0].startswith('frontend duplicate JSON key'))
        path.write_text('{nope')
        self.assertTrue(native('read', str(self.directory), IDENTITY)[0].startswith('frontend cannot read session journal: '))
        path.write_bytes(b' ' * (4 * 1024 * 1024 + 1))
        self.assertEqual(native('read', str(self.directory), IDENTITY)[0], 'frontend session journal exceeds limit')
        path.write_text(json.dumps(edit(journal(), ('state',), 'running')))
        self.assertEqual(native('read', str(self.directory), IDENTITY)[0],
                         'frontend session state disagrees with transition history')

    def test_transitions_follow_the_reference_machine(self):
        for source, targets in session_io.TRANSITIONS.items():
            for target in list(session_io.TRANSITIONS) + ['unknown']:
                value = journal(2, source)
                head, rest = native('transition', str(self.root), target, '{}', text=json.dumps(value))
                if target in targets:
                    self.assertEqual(head, 'ok', (source, target))
                    written = json.loads(rest)
                    self.assertEqual(written, session_io.read_journal(Store(self.directory), IDENTITY))
                    self.assertEqual(written['state'], target)
                    self.assertEqual(written['transitions'][:-1], value['transitions'])
                    self.assertEqual(set(written['transitions'][-1]), {'state', 'at'})
                    del written['transitions'], written['state']
                    self.assertEqual(written, {k: v for k, v in value.items() if k not in ('transitions', 'state')})
                else:
                    self.assertEqual(head, f'frontend illegal session transition: {source} -> {target}')

    def test_transition_fields_and_failure_leave_caller_unchanged(self):
        value = journal(3)
        fields = {'diagnostics': [{'code': 'x'}], 'added': {'n': 1}}
        head, rest = native('transition', str(self.root), 'launching', json.dumps(fields), text=json.dumps(value))
        written = json.loads(rest)
        self.assertEqual((head, written['diagnostics'], written['added']), ('ok', fields['diagnostics'], fields['added']))
        before = (self.root / 'journal.json').read_bytes()
        head, _ = native('transition', str(self.root), 'running', '{}', 'fail', text=json.dumps(written))
        self.assertEqual(head, 'injected-unchanged')
        self.assertEqual((self.root / 'journal.json').read_bytes(), before)
        self.assertEqual([p.name for p in self.root.iterdir()], ['journal.json'])
        # A field that invalidates the journal persists nothing.
        head, _ = native('transition', str(self.root), 'running', '{"pins": 1}', text=json.dumps(written))
        self.assertEqual(head, 'frontend incomplete session journal')
        self.assertEqual((self.root / 'journal.json').read_bytes(), before)


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
