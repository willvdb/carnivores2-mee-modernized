"""Native session lifecycle against the unchanged Python reference (disposable stores).

Both implementations run on the SAME store path, sequentially: the native driver
runs on the store, the result is moved aside, a pristine copy is restored and the
reference runs on it. Session ids and timestamps are substituted after format
checks; every other field, workspace byte and refusal message is compared exactly.
The only accepted difference is the synthetic execution evidence: the compiled
child replaces the interpreter plus script of the reference spec.
"""
import copy
from contextlib import ExitStack
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
from lodge.discovery import register  # noqa: E402
from lodge.managed_state import upgrade_store  # noqa: E402
from lodge.native_hunt import prepare_hunt  # noqa: E402
from lodge.native_observer import CAPABILITY, CONFIG, prepare_native  # noqa: E402
from lodge import native_continuation, native_hunt, native_observer, native_session  # noqa: E402
from lodge.profiles import associate  # noqa: E402
from lodge.session_io import encode, session_root  # noqa: E402
from lodge.sessions import SCENARIOS, prepare_session  # noqa: E402
from lodge.store import FrontendError, Store, hunter, valid_id  # noqa: E402
from support import game  # noqa: E402
from test_genesis_hunt import selection as hunt_selection  # noqa: E402
from test_launch import SCRIPT  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

DRIVER, PROBE, ENGINE, CHILD = sys.argv[1:5]
MODE = sys.argv[5] if len(sys.argv) > 5 else 'prepare'
SMOD = 'smod=0.85,0.70,0.80,1.0,1.25,1.0'
DEFAULT = object()


def observer_double(revision, catalog, slot, selection, score):
    """Asset-free policy double, mirrored byte for byte by the native driver. Not Genesis."""
    return {'adapter': 'asset-free-policy-double',
            'candidate_argv': [f'reg={slot}', 'prj=huntdat/areas/area1', 'din=0', 'wep=0',
                               f"dtm={selection['time_of_day']}", '-observ', SMOD]}


def hunt_double(revision, catalog, slot, selection, score):
    return {'adapter': 'genesis-current-mee-hunt-v1', 'fixture_only': 'authored disposable state, NOT Genesis',
            'selection': copy.deepcopy(selection),
            'candidate_argv': [f'reg={slot}', 'prj=huntdat/areas/area1', 'din=1', 'wep=1',
                               f"dtm={selection['time_of_day']}", SMOD]}


PATCHES = {'lodge.native_observer.observer_policy': observer_double,
           'lodge.native_hunt.hunt_policy': hunt_double,
           'lodge.native_continuation.hunt_policy': hunt_double}


def tree(root):
    root = Path(root)
    if not root.exists():
        return None
    out = {}
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            out[relative] = ('link', os.readlink(path))
        elif path.is_dir():
            out[relative] = 'dir'
        else:
            out[relative] = path.read_bytes()
    return out


def native(command, *args, stdin=None, env=None):
    environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
    environment.update(env or {})
    done = subprocess.run([DRIVER, command, *args], input=json.dumps(stdin).encode() if stdin is not None else b'',
                          capture_output=True, timeout=120, env=environment)
    if done.returncode != 0:
        raise AssertionError(f'driver failed: {done}')
    kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
    if kind == 'ok' and rest[:1] in ('{', '['):
        rest = json.loads(rest)
    return kind, rest


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        os.environ.pop('C2_PROFILE_PROBE', None)
        os.environ.pop('C2_NATIVE_FIXTURE_BEHAVIOR', None)
        self.base = Path(self.temp.name).resolve()
        self.game = game(self.base)
        (self.game / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (self.game / 'trophy00.sav').write_bytes(save_bytes())
        (self.game / 'trophy00.sab').write_bytes(room_bytes())
        self.store = Store(self.base / 'Lodge')
        with self.store.transaction() as data:
            h = hunter(data, 'create', name='Synthetic test hunter')
            i = register(data, self.game, dialect='c2-classic')
            self.association = associate(self.store, data, h['id'], i['id'], 'trophy00', 'personal', 'managed', PROBE)['id']
        self.source = self.store.directory / 'snapshots' / self.association
        self.engine = Path(ENGINE).resolve()
        self.digest = hashlib.sha256(self.engine.read_bytes()).hexdigest()
        self.game_before = tree(self.game)
        self.addCleanup(lambda: self.assertEqual(tree(self.game), self.game_before))

    # Reference execution with the labelled doubles (or the production policies).
    def python(self, call, fixture=True, env=None):
        with ExitStack() as stack:
            if fixture:
                for target, double in PATCHES.items():
                    stack.enter_context(patch(target, side_effect=double))
            stack.enter_context(patch.dict(os.environ, env or {}))
            try:
                return 'ok', call()
            except FrontendError as error:
                return 'frontend', str(error)
            except OSError:
                return 'oserror', None

    def both(self, call, command, *args, stdin=None, env=None, fixture=True, mutate=None):
        """Native first on the store, then the reference on a pristine copy at the same path."""
        store, aside, result = self.store.directory, self.base / 'aside', self.base / 'native-result'
        for path in (aside, result):
            shutil.rmtree(path, ignore_errors=True)
        shutil.copytree(store, aside, symlinks=True)
        if mutate:
            mutate()
        actual = native(command, *args, stdin=stdin, env=env)
        store.rename(result)
        aside.rename(store)
        if mutate:
            mutate()
        expected = self.python(call, fixture, env)
        return expected, actual, result

    def sync_times(self, actual, expected):
        if isinstance(actual, dict) and isinstance(expected, dict):
            for key, value in actual.items():
                if key not in expected:
                    continue
                if (key == 'at' or key.endswith('_at')) and isinstance(value, str) and isinstance(expected[key], str):
                    datetime.fromisoformat(value)
                    actual[key] = expected[key]
                else:
                    self.sync_times(value, expected[key])
        elif isinstance(actual, list) and isinstance(expected, list):
            for a, e in zip(actual, expected):
                self.sync_times(a, e)

    def normalized(self, actual, expected):
        self.assertTrue(valid_id(actual['id']))
        self.assertNotEqual(actual['id'], expected['id'])
        actual = json.loads(json.dumps(actual).replace(actual['id'], expected['id']))
        self.sync_times(actual, expected)
        return actual, copy.deepcopy(expected)

    def assert_same_journal(self, expected, actual, synthetic=False):
        actual, expected = self.normalized(actual, expected)
        if synthetic:
            e, a = expected.pop('execution'), actual.pop('execution')
            self.assertEqual(list(a), list(e))
            self.assertEqual(a['argv'][1:], e['argv'][3:])
            self.assertEqual(a['argv'][0], a['executable']['path'])
            self.assertEqual(a['fixture'], a['executable'])
            child = Path(CHILD).resolve()
            self.assertEqual(a['executable'], {'path': str(child), 'sha256': hashlib.sha256(child.read_bytes()).hexdigest()})
            self.assertEqual(e['argv'][:2], [sys.executable, '-I'])
            for key in ('kind', 'cwd', 'timeout_seconds', 'scenario', 'shell'):
                self.assertEqual(a[key], e[key], key)
        self.assertEqual(json.dumps(actual), json.dumps(expected))

    def assert_same_store(self, expected, actual, expected_store, actual_store, synthetic=False):
        """Identical journals (normalized), session bytes and the rest of the store."""
        self.assertEqual(actual[0], 'ok', actual)
        self.assertEqual(expected[0], 'ok', expected)
        expected, actual = expected[1], actual[1]
        self.assert_same_journal(expected, actual, synthetic)
        expected_tree = tree(expected_store / 'sessions' / expected['id'])
        actual_tree = tree(actual_store / 'sessions' / actual['id'])
        self.assertEqual(actual_tree.pop('journal.json'), encode(actual))
        self.assertEqual(expected_tree.pop('journal.json'), encode(expected))
        self.assertEqual(actual_tree, expected_tree)
        strip = lambda t, identity: {k: v for k, v in t.items() if not k.startswith(f'sessions/{identity}')}
        self.assertEqual(strip(tree(actual_store), actual['id']), strip(tree(expected_store), expected['id']))
        self.assertFalse((actual_store / 'lodge.lock').exists())

    def assert_diagnostics(self, actual, expected):
        """Codes, domains, paths and reference-message texts must agree; the text of an
        OSError-derived message is the native OS error (documented difference)."""
        self.assertEqual(len(actual), len(expected), (actual, expected))
        for a, e in zip(actual, expected):
            if isinstance(e.get('message'), str) and e['message'].startswith('[Errno ') and isinstance(a.get('message'), str):
                self.assertTrue(a['message'])
                a = {**a, 'message': e['message']}
            self.assertEqual(a, e)
            self.assertEqual(list(a), list(e))

    def assert_refused(self, expected, actual, expected_store, actual_store, message=None):
        self.assertEqual(actual[0], expected[0], (expected, actual))
        if expected[0] == 'frontend':
            self.assertEqual(actual[1], expected[1])
        if message is not None:
            self.assertEqual(expected[1], message)
        self.assertEqual(tree(actual_store), tree(expected_store))
        self.assertFalse((actual_store / 'lodge.lock').exists())
        return expected[1]

    def synthetic(self, scenario='unchanged', time_of_day=1, timeout=5, association=None, probe=PROBE, area='areas:0'):
        association = association or self.association
        call = lambda: prepare_session(self.store, association, area, scenario, time_of_day, timeout, probe)
        return self.both(call, 'prepare-synthetic', str(self.store.directory), association, area, scenario,
                         probe or '-', stdin={'time_of_day': time_of_day, 'timeout': timeout})

    def observer_selection(self, area='areas:0', time_of_day=1):
        return {'area': area, 'mode': 'observer', 'time_of_day': time_of_day, 'licenses': [], 'weapons': [], 'equipment': []}

    def native_prepare(self, adapter='observer', selection=None, engine=None, digest=DEFAULT, experimental=True,
                       timeout=900, probe=PROBE, association=None, fixture=True, env=None, mutate=None):
        association = association or self.association
        engine = str(engine or self.engine)
        digest = self.digest if digest is DEFAULT else digest
        if adapter == 'observer':
            selection = selection or self.observer_selection()
            call = lambda: prepare_native(self.store, association, selection['area'], engine, digest, experimental,
                                          selection['time_of_day'], timeout, probe)
        else:
            selection = selection or hunt_selection()
            call = lambda: prepare_hunt(self.store, association, selection, engine, digest, experimental, timeout, probe)
        stdin = {'selection': selection, 'engine': engine, 'digest': digest, 'experimental': experimental, 'timeout': timeout}
        policy = 'fixture-policy' if fixture else 'production-policy'
        return self.both(call, 'prepare-native', str(self.store.directory), adapter, association, probe or '-', policy,
                         stdin=stdin, env=env, fixture=fixture, mutate=mutate)


class Prepare(Base):
    def test_synthetic_fixture_is_the_compiled_child(self):
        self.assertEqual(native('synthetic-fixture'), ('ok', str(Path(CHILD).resolve())))

    def test_synthetic_prepare_matches_reference_for_every_scenario(self):
        for index, scenario in enumerate(SCENARIOS):
            timeout = (5, 0.15, 30, 0.05, 29.999)[index % 5]
            with self.subTest(scenario=scenario, timeout=timeout):
                expected, actual, result = self.synthetic(scenario, time_of_day=index % 3, timeout=timeout)
                self.assert_same_store(expected, actual, self.store.directory, result, synthetic=True)
                journal = actual[1]
                self.assertEqual(journal['schema_version'], 1)
                self.assertEqual(journal['execution']['timeout_seconds'], timeout)
                self.assertEqual(journal['state'], 'prepared')
                self.assertFalse(journal['process_launch_allowed'])
                root = result / 'sessions' / journal['id']
                self.assertEqual(sorted(p.name for p in root.iterdir()), ['baseline', 'journal.json', 'logs', 'work'])
                self.assertEqual([p.name for p in (root / 'work').iterdir()], ['state'])
                self.assertEqual(list((root / 'logs').iterdir()), [])
                self.assertEqual({p.name: p.read_bytes() for p in (root / 'work/state').iterdir()},
                                 {'trophy00.sav': save_bytes(), 'trophy00.sab': room_bytes()})

    def test_synthetic_prepare_refusals_write_nothing(self):
        cases = [
            (dict(scenario='../evil'), 'unknown controlled synthetic scenario'),
            (dict(scenario='hang', timeout=float('nan')), 'synthetic timeout must be between 0.05 and 30 seconds'),
            (dict(timeout=300), 'synthetic timeout must be between 0.05 and 30 seconds'),
            (dict(timeout=0.04), 'synthetic timeout must be between 0.05 and 30 seconds'),
            (dict(timeout=True), 'synthetic timeout must be between 0.05 and 30 seconds'),
            (dict(timeout='5'), 'synthetic timeout must be between 0.05 and 30 seconds'),
            (dict(timeout=0), 'synthetic timeout must be between 0.05 and 30 seconds'),
            (dict(timeout=1), None), (dict(timeout=30.0), None),
            (dict(time_of_day=True), 'session policy permits observer with no loadout only'),
            (dict(time_of_day=3), 'session policy permits observer with no loadout only'),
            (dict(time_of_day='1'), 'session policy permits observer with no loadout only'),
            # Selection validation precedes the scenario/timeout checks.
            (dict(scenario='../evil', time_of_day=1.0), 'session policy permits observer with no loadout only'),
            (dict(area='areas:999'), 'session requires an unambiguous advertised area'),
            (dict(association='0a9b8c7d-1234-4abc-8def-001122334455'), 'unknown association ID'),
            (dict(probe=None), 'session requires an explicit profile codec helper'),
            (dict(probe=str(self.base / 'absent')), None),
        ]
        for kwargs, message in cases:
            with self.subTest(kwargs=kwargs):
                expected, actual, result = self.synthetic(**kwargs)
                if message is None and expected[0] == 'ok':
                    self.assert_same_store(expected, actual, self.store.directory, result, synthetic=True)
                elif message is None:
                    self.assertEqual(expected[0], 'oserror')
                    self.assert_refused(expected, actual, self.store.directory, result)
                else:
                    self.assert_refused(expected, actual, self.store.directory, result, message)

    def test_synthetic_prepare_store_state_refusals(self):
        with self.store.transaction() as data:
            h = next(iter(data['hunters']))
            i = next(iter(data['instances']))
            referenced = associate(self.store, data, h, i, 'trophy00', 'personal', 'referenced', PROBE)['id']
        expected, actual, result = self.synthetic(association=referenced)
        self.assert_refused(expected, actual, self.store.directory, result,
                            'sessions require managed personal state; referenced/bundled/unknown rejected')
        lock = self.store.directory / 'lodge.lock'
        lock.write_bytes(b'foreign-lock')
        expected, actual, result = self.synthetic()
        self.assertEqual(actual[0], 'frontend')
        self.assertTrue(actual[1].startswith('frontend writer lock exists: '), actual)
        self.assertEqual(actual[1], expected[1])
        self.assertEqual((result / 'lodge.lock').read_bytes(), b'foreign-lock')
        lock.unlink()
        (self.source / 'trophy00.sav').write_bytes(b'broken')
        expected, actual, result = self.synthetic()
        self.assert_refused(expected, actual, self.store.directory, result,
                            'managed source membership/bytes differ or require review')
        (self.source / 'trophy00.sav').write_bytes(save_bytes())
        with self.store.transaction() as data:
            data['hunters'][h]['archived_at'] = '2000-01-01T00:00:00Z'
            data['active_hunter'] = None
        expected, actual, result = self.synthetic()
        self.assert_refused(expected, actual, self.store.directory, result, 'archived hunter cannot start a session')

    def test_native_observer_prepare_matches_reference(self):
        for time_of_day, timeout in ((1, 900), (0, 30), (2, 3600.0)):
            with self.subTest(time_of_day=time_of_day, timeout=timeout):
                expected, actual, result = self.native_prepare(selection=self.observer_selection(time_of_day=time_of_day), timeout=timeout)
                self.assert_same_store(expected, actual, self.store.directory, result)
                journal = actual[1]
                self.assertEqual(journal['schema_version'], 2)
                self.assertEqual(journal['execution']['kind'], 'experimental-native-observer-v1')
                self.assertEqual(journal['execution']['timeout_seconds'], timeout)
                self.assertEqual(journal['execution']['cwd'], str(self.game))
                self.assertIn('--session-slot=0', journal['execution']['argv'])
                self.assertNotIn('reg=0', journal['execution']['argv'])
                self.assertEqual(journal['capabilities']['engine_contract'], CAPABILITY)
                self.assertEqual(journal['pins']['adapter'], 'genesis-current-mee-observer-v1')
                self.assertEqual(journal['pins']['observer_policy']['adapter'], 'asset-free-policy-double')
                self.assertEqual(journal['reconciliation'], {'authority': 'original-managed-snapshot', 'promotion': 'deferred', 'status': 'pending'})
                root = result / 'sessions' / journal['id']
                self.assertEqual({p.name for p in (root / 'work').iterdir()}, {'state', 'config', 'output'})
                self.assertEqual((root / 'work/config/config.cfg').read_bytes(), CONFIG)
                self.assertEqual(list((root / 'work/output').iterdir()), [])
                self.assertEqual(list((root / 'logs').iterdir()), [])

    def test_native_hunt_prepare_schema_three_then_four(self):
        expected, actual, result = self.native_prepare('hunt')
        self.assert_same_store(expected, actual, self.store.directory, result)
        journal = actual[1]
        self.assertEqual((journal['schema_version'], journal['execution']['kind']), (3, 'experimental-native-hunt-v1'))
        self.assertEqual(journal['pins']['adapter'], 'genesis-current-mee-hunt-v1')
        self.assertEqual(journal['pins']['hunt_policy']['adapter'], 'genesis-current-mee-hunt-v1')
        self.assertNotIn('generation_id', journal['pins'])
        self.assertEqual(journal['reconciliation']['promotion'], 'deferred')
        self.assertEqual(journal['capabilities']['native_hunt_lifecycle'], 'not-executed')
        upgraded = upgrade_store(self.store)
        self.assertEqual(upgraded['result'], 'upgraded')
        head = upgraded['associations'][self.association]
        expected, actual, result = self.native_prepare('hunt')
        self.assert_same_store(expected, actual, self.store.directory, result)
        journal = actual[1]
        self.assertEqual((journal['schema_version'], journal['execution']['kind']), (4, 'managed-native-hunt-v1'))
        self.assertEqual(journal['pins']['generation_id'], head)
        self.assertEqual(journal['pins']['generation']['id'], head)
        self.assertNotIn('managed_state', journal['pins']['association'])
        self.assertEqual(journal['reconciliation'], {'authority': 'managed-state-history', 'promotion': 'explicit-only', 'status': 'pending'})
        self.assertEqual(journal['pins']['source_root'], str(self.source))
        # The upgraded store refuses the legacy adapters.
        expected, actual, result = self.native_prepare('observer')
        self.assert_refused(expected, actual, self.store.directory, result,
                            'legacy sessions cannot select an import in an upgraded store; prepare a generation-pinned hunt')
        expected, actual, result = self.synthetic()
        self.assert_refused(expected, actual, self.store.directory, result,
                            'legacy sessions cannot select an import in an upgraded store; prepare a generation-pinned hunt')

    def test_native_prepare_refusals_order_and_no_writes(self):
        absent = str(self.base / 'absent-engine')
        cases = [
            (dict(experimental=False), 'native execution requires the explicit experimental gate'),
            (dict(experimental=False, digest='x'), 'native execution requires the explicit experimental gate'),
            (dict(digest='x'), 'explicit trusted engine SHA-256 is required'),
            (dict(digest=None), 'explicit trusted engine SHA-256 is required'),
            (dict(digest=self.digest.upper()), 'explicit trusted engine SHA-256 is required'),
            (dict(digest='0' * 64), 'selected engine does not match the explicitly trusted hash'),
            (dict(engine=absent), None),
            (dict(engine=self.game), 'explicit executable must be a regular file'),
            (dict(timeout=5), 'developer native validation timeout must be 30..3600 seconds'),
            (dict(timeout=3601), 'developer native validation timeout must be 30..3600 seconds'),
            (dict(timeout=True), 'developer native validation timeout must be 30..3600 seconds'),
            (dict(timeout=float('nan')), 'developer native validation timeout must be 30..3600 seconds'),
            (dict(timeout='900'), 'developer native validation timeout must be 30..3600 seconds'),
            (dict(env={'C2_NATIVE_FIXTURE_BEHAVIOR': 'bad-contract'}), 'unsupported engine session contract'),
            (dict(fixture=False), 'Genesis policy refuses an unpinned content revision'),
            (dict(adapter='hunt', fixture=False), 'Genesis hunt refuses an unpinned content revision'),
            (dict(adapter='hunt', selection=self.observer_selection()), 'session policy permits observer with no loadout only'),
            (dict(selection=self.observer_selection(area='areas:999')), 'session requires an unambiguous advertised area'),
            (dict(association='0a9b8c7d-1234-4abc-8def-001122334455'), 'unknown association ID'),
            (dict(probe=None), 'session requires an explicit profile codec helper'),
        ]
        for kwargs, message in cases:
            with self.subTest(kwargs=kwargs):
                expected, actual, result = self.native_prepare(**kwargs)
                if message is None:
                    self.assertEqual(expected[0], 'oserror')
                self.assert_refused(expected, actual, self.store.directory, result, message)

    def test_native_prepare_overlap_pair_and_lock_refusals(self):
        inside = self.store.directory / 'engine'
        inside.mkdir()
        shutil.copy2(self.engine, inside / 'engine')
        expected, actual, result = self.native_prepare(engine=inside / 'engine')
        self.assert_refused(expected, actual, self.store.directory, result,
                            'native workspace overlaps protected source/content/engine')
        shutil.rmtree(inside)
        lock = self.store.directory / 'lodge.lock'
        lock.write_bytes(b'foreign-lock')
        expected, actual, result = self.native_prepare()
        self.assertEqual(actual[0], 'frontend')
        self.assertTrue(actual[1].startswith('frontend writer lock exists: '), actual)
        self.assertEqual(actual[1], expected[1])
        self.assertEqual((result / 'lodge.lock').read_bytes(), b'foreign-lock')
        lock.unlink()
        (self.source / 'trophy00.sab').unlink()
        with self.store.transaction() as data:
            a = data['associations'][self.association]
            a['files'] = [f for f in a['files'] if f['kind'] == 'sav']
        expected, actual, result = self.native_prepare()
        self.assert_refused(expected, actual, self.store.directory, result,
                            'engine contract v1 requires an existing complete SAV/SAB pair')
        expected, actual, result = self.native_prepare('hunt')
        self.assert_refused(expected, actual, self.store.directory, result,
                            'hunt contract requires an existing complete SAV/SAB pair')
        expected, actual, result = self.synthetic()
        self.assert_same_store(expected, actual, self.store.directory, result, synthetic=True)

    def test_trusted_engine_and_capability_query(self):
        evidence = native_session.trusted_engine(self.engine, self.digest, True)
        self.assertEqual(native('trusted-engine', str(self.engine), '1', stdin={'digest': self.digest}), ('ok', evidence))
        self.assertEqual(native('trusted-engine', str(self.engine), '0', stdin={'digest': self.digest}),
                         ('frontend', 'native execution requires the explicit experimental gate'))
        self.assertEqual(native('trusted-engine', str(self.engine), '1', stdin={'digest': '0' * 64}),
                         ('frontend', 'selected engine does not match the explicitly trusted hash'))
        self.assertEqual(native('query-contract', str(self.engine)), ('ok', native_session.query_contract(evidence)))
        self.assertEqual(native('query-contract', str(self.engine))[1], CAPABILITY)
        self.assertEqual(native('query-contract', str(self.engine), env={'C2_NATIVE_FIXTURE_BEHAVIOR': 'bad-contract'}),
                         ('frontend', 'unsupported engine session contract'))
        with patch.dict(os.environ, {'C2_NATIVE_FIXTURE_BEHAVIOR': 'bad-contract'}):
            with self.assertRaisesRegex(FrontendError, 'unsupported engine session contract'):
                native_session.query_contract(evidence)
        # The codec helper answers the query with unrelated JSON; the refusal must match the reference.
        self.assertEqual(native('query-contract', str(PROBE)),
                         self.python(lambda: native_session.query_contract(native_session.executable_evidence(PROBE))))

    def test_workspace_findings_match_reference(self):
        expected, actual, result = self.native_prepare()
        self.assert_same_store(expected, actual, self.store.directory, result)
        root = session_root(self.store, expected[1]['id'])
        screenshot = 'HUNT0001.BM' if os.name == 'nt' else 'HUNT0001.BMP'
        long_screenshot = 'HUNT00001.BM' if os.name == 'nt' else 'HUNT00001.BMP'
        mutations = [
            [], [('work/output/render.log', b'log')], [('work/output/carnivor.log', b'')],
            [('work/output/' + screenshot, b'BM')], [('work/output/' + long_screenshot, b'BM')],
            [('work/output/' + screenshot + '.unclassified', b'BM')], [('work/output/hunt0001.bmp', b'BM')],
            [('work/output/HUNT001.BMP', b'BM')], [('work/output/nested/render.log', b'x')],
            [('work/config/config.cfg', b'fov 90')], [('work/config/extra.cfg', b'x')],
            [('work/extra', b'x')], [('work/output/render.log', b'x'), ('work/config/config.cfg', b'changed'), ('work/extra', b'x')],
        ]
        for mutation in mutations:
            for returning in (False, True):
                with self.subTest(mutation=mutation, returning=returning):
                    for relative, content in mutation:
                        (root / relative).parent.mkdir(parents=True, exist_ok=True)
                        (root / relative).write_bytes(content)
                    reference = native_session.workspace_findings(root, returning)
                    self.assertEqual(native('workspace-findings', str(root), '1' if returning else '0'), ('ok', reference))
                    for relative, _ in mutation:
                        (root / relative).unlink()
                    shutil.rmtree(root / 'work/output/nested', ignore_errors=True)
                    (root / 'work/config/config.cfg').write_bytes(CONFIG)
        shutil.rmtree(root / 'work/config')
        self.assertEqual(native('workspace-findings', str(root), '1'), ('ok', native_session.workspace_findings(root, True)))
        self.assertEqual([f['domain'] for f in native('workspace-findings', str(root), '1')[1]], ['workspace', 'config'])
        (root / 'work').rename(root / 'elsewhere')
        if os.name == 'posix':
            (root / 'work').symlink_to(root / 'elsewhere', target_is_directory=True)
            self.assertEqual(native('workspace-findings', str(root), '1'), ('frontend', 'session path contains an alias'))
            (root / 'work').unlink()
        kind, findings = native('workspace-findings', str(root), '1')
        self.assertEqual(kind, 'ok')
        self.assert_diagnostics(findings, native_session.workspace_findings(root, True))
        self.assertEqual([f['domain'] for f in findings], ['workspace', 'config', 'output'])

    def test_native_pins_match_reference_for_every_adapter(self):
        def reference(adapter, **kwargs):
            module = {'observer': native_observer, 'hunt': native_hunt, 'continuation': native_continuation}[adapter]
            selection = self.observer_selection() if adapter == 'observer' else hunt_selection()
            call = lambda: module.native_pins(self.store, self.association, selection, PROBE, **kwargs)[0]
            return selection, self.python(call)
        for adapter in ('observer', 'hunt'):
            with self.subTest(adapter=adapter):
                selection, expected = reference(adapter)
                self.assertEqual(expected[0], 'ok', expected)
                actual = native('native-pins', str(self.store.directory), adapter, self.association, PROBE, 'fixture-policy',
                                stdin={'selection': selection})
                self.assertEqual(actual, expected)
                self.assertEqual(json.dumps(actual[1]), json.dumps(expected[1]))
                mismatched = {**expected[1]['codec'], 'sha256': '0' * 64}
                self.assertEqual(native('native-pins', str(self.store.directory), adapter, self.association, PROBE, 'fixture-policy',
                                        stdin={'selection': selection, 'expected_codec': mismatched}),
                                 ('frontend', 'pinned codec evidence changed; helper not executed'))
        self.assertEqual(native('native-pins', str(self.store.directory), 'continuation', self.association, PROBE, 'fixture-policy',
                                stdin={'selection': hunt_selection()}),
                         ('frontend', 'explicit managed-state schema upgrade required'))
        head = upgrade_store(self.store)['associations'][self.association]
        selection, expected = reference('continuation')
        self.assertEqual(expected[0], 'ok', expected)
        actual = native('native-pins', str(self.store.directory), 'continuation', self.association, PROBE, 'fixture-policy',
                        stdin={'selection': selection})
        self.assertEqual(json.dumps(actual[1]), json.dumps(expected[1]))
        self.assertEqual(actual[1]['generation_id'], head)
        selection, expected = reference('continuation', generation=head)
        self.assertEqual(native('native-pins', str(self.store.directory), 'continuation', self.association, PROBE, 'fixture-policy',
                                stdin={'selection': selection, 'generation': head}), expected)
        self.assertEqual(native('native-pins', str(self.store.directory), 'continuation', self.association, PROBE, 'fixture-policy',
                                stdin={'selection': selection, 'generation': '0a9b8c7d-1234-4abc-8def-001122334455'}),
                         ('frontend', 'unknown managed-state generation'))
        selection, expected = reference('hunt', )
        self.assertEqual(expected[1], 'legacy sessions cannot select an import in an upgraded store; prepare a generation-pinned hunt')
        self.assertEqual(native('native-pins', str(self.store.directory), 'hunt', self.association, PROBE, 'fixture-policy',
                                stdin={'selection': selection}), expected)


MODES = {'prepare': Prepare}


if __name__ == '__main__':
    if MODE not in MODES:
        raise SystemExit(f'unknown test mode {MODE!r}; choose from {sorted(MODES)}')
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(MODES[MODE])
    if suite.countTestCases() == 0:
        raise SystemExit('no tests selected')
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.skipped:
        raise SystemExit(f'tests skipped: {result.skipped}')
    sys.exit(0 if result.wasSuccessful() else 1)
