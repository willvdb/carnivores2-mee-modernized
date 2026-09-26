"""Native session lifecycle against the unchanged Python reference (disposable stores).

Both implementations run on the SAME store path, sequentially: the native driver
runs on the store, the result is moved aside, a pristine copy is restored and the
reference runs on it. Session ids, timestamps and child pids are substituted
after format checks; every other field, workspace byte and refusal message is
compared exactly. The accepted differences: the synthetic execution evidence
(the compiled child replaces the interpreter plus script of the reference spec)
and the text of OS-error-derived diagnostic messages.

usage: test_sessions_native.py DRIVER PROBE ENGINE CHILD HELPER [prepare|run|reconcile]
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
import threading
import time
import unittest
from unittest.mock import patch

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
from lodge.acceptance import accept_candidate, candidate_digest, preview_acceptance  # noqa: E402
from lodge.discovery import register  # noqa: E402
from lodge.managed_state import upgrade_store  # noqa: E402
from lodge.native_hunt import prepare_hunt, run_hunt  # noqa: E402
from lodge.native_observer import CAPABILITY, CONFIG, prepare_native, run_native  # noqa: E402
from lodge import native_continuation, native_hunt, native_observer, native_session  # noqa: E402
from lodge.profiles import associate  # noqa: E402
from lodge.reconciliation import reconcile_session  # noqa: E402
from lodge.session_io import capture, encode, persist, read_journal, session_root, transition, write_blobs  # noqa: E402
from lodge.session_runner import LOG_LIMIT, recover_session, run_session  # noqa: E402
from lodge.sessions import SCENARIOS, prepare_session  # noqa: E402
from lodge.store import FrontendError, Store, hunter, valid_id  # noqa: E402
from support import game  # noqa: E402
from test_genesis_hunt import selection as hunt_selection  # noqa: E402
from test_launch import SCRIPT  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

DRIVER, PROBE, ENGINE, CHILD, HELPER = (str(Path(a).resolve()) for a in sys.argv[1:6])
MODE = sys.argv[6] if len(sys.argv) > 6 else 'prepare'
SMOD = 'smod=0.85,0.70,0.80,1.0,1.25,1.0'
DEFAULT = object()
BEHAVIOR = 'C2_NATIVE_FIXTURE_BEHAVIOR'
ENGINE_MARKER = 'C2_NATIVE_FIXTURE_MARKER'


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


def native(command, *args, stdin=None, env=None, cwd=None):
    environment = {k: v for k, v in os.environ.items() if k not in ('C2_PROFILE_PROBE', BEHAVIOR, ENGINE_MARKER)}
    environment.update(env or {})
    done = subprocess.run([DRIVER, command, *args], input=json.dumps(stdin).encode() if stdin is not None else b'',
                          capture_output=True, timeout=120, env=environment, cwd=cwd)
    if done.returncode != 0:
        raise AssertionError(f'driver failed: {done}')
    kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
    if kind == 'ok' and rest[:1] in ('{', '['):
        rest = json.loads(rest)
    return kind, rest


def child_running(work):
    """Linux /proc scan: a live process whose cwd is the session's work directory (the
    synthetic child) or whose command line names it (the engine's --session-root)."""
    if not Path('/proc').is_dir():
        return None
    work = str(work)
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            if os.readlink(entry / 'cwd') == work:
                return True
            if work.encode() in (entry / 'cmdline').read_bytes():
                return True
        except OSError:
            continue
    return False


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        os.environ.pop('C2_PROFILE_PROBE', None)
        os.environ.pop(BEHAVIOR, None)
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
        self.engine_marker = self.base / 'engine-invocations'
        # The labelled doubles stay active for every direct reference call.
        self.patchers = [patch(target, side_effect=double) for target, double in PATCHES.items()]
        for patcher in self.patchers:
            patcher.start()
            self.addCleanup(patcher.stop)

    # Reference execution with the labelled doubles (or the production policies).
    def python(self, call, fixture=True, env=None):
        with ExitStack() as stack:
            if not fixture:
                for patcher in self.patchers:
                    patcher.stop()
                stack.callback(lambda: [patcher.start() for patcher in self.patchers])
            stack.enter_context(patch.dict(os.environ, env or {}))
            try:
                return 'ok', call()
            except FrontendError as error:
                return 'frontend', str(error)
            except OSError:
                return 'oserror', None

    def both(self, call, native_call, env=None, fixture=True, mutate=None):
        """Native first on the store, then the reference on a pristine copy at the same path."""
        store, aside, result = self.store.directory, self.base / 'aside', self.base / 'native-result'
        for path in (aside, result):
            shutil.rmtree(path, ignore_errors=True)
        shutil.copytree(store, aside, symlinks=True)
        if mutate:
            mutate()
        actual = native_call()
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
                elif key == 'pid' and type(value) is int and type(expected[key]) is int:
                    self.assertGreater(value, 0)
                    actual[key] = expected[key]
                else:
                    self.sync_times(value, expected[key])
        elif isinstance(actual, list) and isinstance(expected, list):
            for a, e in zip(actual, expected):
                self.sync_times(a, e)

    def normalized(self, actual, expected):
        self.assertTrue(valid_id(actual['id']))
        if actual['id'] != expected['id']:
            actual = json.loads(json.dumps(actual).replace(actual['id'], expected['id']))
        else:
            actual = copy.deepcopy(actual)
        self.sync_times(actual, expected)
        return actual, copy.deepcopy(expected)

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

    def assert_same_journal(self, expected, actual, synthetic=False, execution=True):
        actual, expected = self.normalized(actual, expected)
        if not execution:
            # A deliberately tampered spec was applied identically on both sides.
            actual.pop('execution'), expected.pop('execution')
        elif synthetic and actual['execution'] != expected['execution']:
            # A journal prepared by the other implementation keeps that spec; when each
            # side prepared its own, only the compiled child evidence may differ.
            e, a = expected.pop('execution'), actual.pop('execution')
            self.assertEqual(list(a), list(e))
            self.assertEqual(a['argv'][1:], e['argv'][3:])
            self.assertEqual(a['argv'][0], a['executable']['path'])
            self.assertEqual(a['fixture'], a['executable'])
            child = Path(CHILD).resolve()
            self.assertEqual(a['executable'], {'path': str(child), 'sha256': hashlib.sha256(child.read_bytes()).hexdigest()})
            # The reference resolves its interpreter (python.exe versus python3.exe on Windows).
            self.assertEqual(e['executable'], native_session.executable_evidence(sys.executable))
            self.assertEqual(e['argv'][:2], [e['executable']['path'], '-I'])
            for key in ('kind', 'cwd', 'timeout_seconds', 'scenario', 'shell'):
                self.assertEqual(a[key], e[key], key)
        if synthetic and 'logs' in actual and 'logs' in expected:
            if True:
                # The receipt line differs only by the pid's digit count.
                for stream in ('stdout', 'stderr'):
                    a_log, e_log = actual['logs'][stream], expected['logs'][stream]
                    self.assertEqual(list(a_log), list(e_log))
                    self.assertLessEqual(abs(a_log['total_bytes'] - e_log['total_bytes']), 4)
                    self.assertEqual(a_log['retained_bytes'], min(a_log['total_bytes'], LOG_LIMIT))
                    self.assertEqual(a_log['truncated'], a_log['total_bytes'] > LOG_LIMIT)
                    a_log['total_bytes'], a_log['retained_bytes'] = e_log['total_bytes'], e_log['retained_bytes']
        if 'diagnostics' in actual and 'diagnostics' in expected:
            self.assert_diagnostics(actual['diagnostics'], expected['diagnostics'])
            actual['diagnostics'] = expected['diagnostics']
        self.assertEqual(json.dumps(actual), json.dumps(expected))

    def assert_same_session_tree(self, expected, actual, expected_store, actual_store, synthetic=False):
        expected_tree = tree(expected_store / 'sessions' / expected['id'])
        actual_tree = tree(actual_store / 'sessions' / actual['id'])
        self.assertEqual(actual_tree.pop('journal.json'), encode(actual))
        self.assertEqual(expected_tree.pop('journal.json'), encode(expected))
        if synthetic and 'logs' in actual and 'logs/stdout.log' in actual_tree:
            a_receipt, e_receipt = actual_tree.pop('logs/stdout.log'), expected_tree.pop('logs/stdout.log')
            a_line, _, a_rest = a_receipt.partition(b'\n')
            e_line, _, e_rest = e_receipt.partition(b'\n')
            self.assertEqual(a_rest, e_rest)
            a_json = json.loads(a_line.decode().replace(actual['id'], expected['id']))
            e_json = json.loads(e_line)
            self.assertEqual(a_json['pid'], actual['process']['pid'])
            self.assertEqual(e_json['pid'], expected['process']['pid'])
            a_json.pop('pid'), e_json.pop('pid')
            self.assertEqual(a_json, e_json)
        self.assertEqual(actual_tree, expected_tree)

    def assert_same_store(self, expected, actual, expected_store, actual_store, synthetic=False, execution=True):
        """Identical journals (normalized), session bytes and the rest of the store."""
        self.assertEqual(actual[0], 'ok', actual)
        self.assertEqual(expected[0], 'ok', expected)
        expected, actual = expected[1], actual[1]
        self.assert_same_journal(expected, actual, synthetic, execution)
        self.assert_same_session_tree(expected, actual, expected_store, actual_store, synthetic)
        strip = lambda t, identity: {k: v for k, v in t.items() if not k.startswith(f'sessions/{identity}')}
        self.assertEqual(strip(tree(actual_store), actual['id']), strip(tree(expected_store), expected['id']))
        self.assertFalse((actual_store / 'lodge.lock').exists())
        return actual

    def marker_env(self):
        self.engine_marker.unlink(missing_ok=True)
        return {ENGINE_MARKER: str(self.engine_marker)}

    def assert_engine_queried(self, queried):
        """Both sides share the marker file: neither implementation may have executed the engine."""
        if queried:
            self.assertEqual(self.engine_marker.read_text().splitlines(), ['invoked', 'invoked'])
        else:
            self.assertFalse(self.engine_marker.exists(), 'the engine was executed')

    def assert_refused(self, expected, actual, expected_store, actual_store, message=None):
        self.assertEqual(actual[0], expected[0], (expected, actual))
        if expected[0] == 'frontend':
            self.assertEqual(actual[1], expected[1])
        if message is not None:
            self.assertEqual(expected[1], message)
        self.assertEqual(tree(actual_store), tree(expected_store))
        self.assertFalse((actual_store / 'lodge.lock').exists())
        return expected[1]

    # Reference/native operation pairs.
    def py_prepare_synthetic(self, scenario='unchanged', time_of_day=1, timeout=5, association=None, probe=PROBE, area='areas:0'):
        return prepare_session(self.store, association or self.association, area, scenario, time_of_day, timeout, probe)

    def nt_prepare_synthetic(self, scenario='unchanged', time_of_day=1, timeout=5, association=None, probe=PROBE, area='areas:0'):
        return native('prepare-synthetic', str(self.store.directory), association or self.association, area, scenario,
                      probe or '-', stdin={'time_of_day': time_of_day, 'timeout': timeout})

    def synthetic(self, **kwargs):
        return self.both(lambda: self.py_prepare_synthetic(**kwargs), lambda: self.nt_prepare_synthetic(**kwargs))

    def observer_selection(self, area='areas:0', time_of_day=1):
        return {'area': area, 'mode': 'observer', 'time_of_day': time_of_day, 'licenses': [], 'weapons': [], 'equipment': []}

    def py_prepare_native(self, adapter='observer', selection=None, engine=None, digest=DEFAULT, experimental=True,
                          timeout=900, probe=PROBE, association=None):
        association = association or self.association
        engine = str(engine or self.engine)
        digest = self.digest if digest is DEFAULT else digest
        if adapter == 'observer':
            selection = selection or self.observer_selection()
            return prepare_native(self.store, association, selection['area'], engine, digest, experimental,
                                  selection['time_of_day'], timeout, probe)
        return prepare_hunt(self.store, association, selection or hunt_selection(), engine, digest, experimental, timeout, probe)

    def nt_prepare_native(self, adapter='observer', selection=None, engine=None, digest=DEFAULT, experimental=True,
                          timeout=900, probe=PROBE, association=None, fixture=True, env=None):
        selection = selection or (self.observer_selection() if adapter == 'observer' else hunt_selection())
        stdin = {'selection': selection, 'engine': str(engine or self.engine),
                 'digest': self.digest if digest is DEFAULT else digest, 'experimental': experimental, 'timeout': timeout}
        return native('prepare-native', str(self.store.directory), adapter, association or self.association, probe or '-',
                      'fixture-policy' if fixture else 'production-policy', stdin=stdin, env=env)

    def native_prepare(self, fixture=True, env=None, mutate=None, **kwargs):
        return self.both(lambda: self.py_prepare_native(**kwargs),
                         lambda: self.nt_prepare_native(fixture=fixture, env=env, **kwargs), env=env, fixture=fixture, mutate=mutate)

    def py_run_native(self, adapter, identity, engine=None, digest=DEFAULT, experimental=True, probe=PROBE, cancel_after=None):
        engine = str(engine or self.engine)
        digest = self.digest if digest is DEFAULT else digest
        run = run_native if adapter == 'observer' else run_hunt
        cancel = threading.Event()
        timer = threading.Timer(cancel_after / 1000, cancel.set) if cancel_after else None
        if timer:
            timer.start()
        try:
            return run(self.store, identity, engine, digest, experimental, probe, cancel if cancel_after else None)
        finally:
            if timer:
                timer.cancel()

    def nt_run_native(self, adapter, identity, engine=None, digest=DEFAULT, experimental=True, probe=PROBE, cancel_after=None,
                      fixture=True, env=None):
        stdin = {'engine': str(engine or self.engine), 'digest': self.digest if digest is DEFAULT else digest}
        return native('run-native', str(self.store.directory), adapter, identity, probe or '-',
                      'fixture-policy' if fixture else 'production-policy', str(cancel_after) if cancel_after else '-',
                      '1' if experimental else '0', stdin=stdin, env=env)

    def py_reconcile(self, identity, probe=PROBE):
        return reconcile_session(self.store, identity, probe)

    def nt_reconcile(self, identity, probe=PROBE, fixture=True):
        return native('reconcile', str(self.store.directory), identity, probe or '-', 'fixture-policy' if fixture else 'production-policy')

    def py_recover(self, identity, probe=PROBE):
        return recover_session(self.store, identity, probe)

    def nt_recover(self, identity, probe=PROBE, fixture=True):
        return native('recover', str(self.store.directory), identity, probe or '-', 'fixture-policy' if fixture else 'production-policy')

    def nt_inspect(self, identity):
        return native('inspect', str(self.store.directory), identity)

    def root(self, journal):
        return session_root(self.store, journal['id'])


class Prepare(Base):
    def test_synthetic_fixture_is_the_compiled_child(self):
        self.assertEqual(native('synthetic-fixture'), ('ok', str(Path(CHILD).resolve())))

    def test_synthetic_prepare_matches_reference_for_every_scenario(self):
        for index, scenario in enumerate(SCENARIOS):
            timeout = (5, 0.15, 30, 0.05, 29.999)[index % 5]
            with self.subTest(scenario=scenario, timeout=timeout):
                expected, actual, result = self.synthetic(scenario=scenario, time_of_day=index % 3, timeout=timeout)
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
        expected, actual, result = self.native_prepare(adapter='hunt')
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
        expected, actual, result = self.native_prepare(adapter='hunt')
        self.assert_same_store(expected, actual, self.store.directory, result)
        journal = actual[1]
        self.assertEqual((journal['schema_version'], journal['execution']['kind']), (4, 'managed-native-hunt-v1'))
        self.assertEqual(journal['pins']['generation_id'], head)
        self.assertEqual(journal['pins']['generation']['id'], head)
        self.assertNotIn('managed_state', journal['pins']['association'])
        self.assertEqual(journal['reconciliation'], {'authority': 'managed-state-history', 'promotion': 'explicit-only', 'status': 'pending'})
        self.assertEqual(journal['pins']['source_root'], str(self.source))
        # The upgraded store refuses the legacy adapters.
        expected, actual, result = self.native_prepare(adapter='observer')
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
            (dict(env={BEHAVIOR: 'bad-contract'}), 'unsupported engine session contract'),
            (dict(fixture=False), 'Genesis policy refuses an unpinned content revision'),
            (dict(adapter='hunt', fixture=False), 'Genesis hunt refuses an unpinned content revision'),
            (dict(adapter='hunt', selection=self.observer_selection()), 'session policy permits observer with no loadout only'),
            (dict(selection=self.observer_selection(area='areas:999')), 'session requires an unambiguous advertised area'),
            (dict(association='0a9b8c7d-1234-4abc-8def-001122334455'), 'unknown association ID'),
            (dict(probe=None), 'session requires an explicit profile codec helper'),
        ]
        for kwargs, message in cases:
            with self.subTest(kwargs=kwargs):
                env = {**kwargs.pop('env', {}), **self.marker_env()}
                expected, actual, result = self.native_prepare(env=env, **kwargs)
                if message is None:
                    self.assertEqual(expected[0], 'oserror')
                self.assert_refused(expected, actual, self.store.directory, result, message)
                # The capability query precedes only the execution-spec (timeout) check.
                self.assert_engine_queried(message is not None and ('timeout' in message or 'contract' in message))

    def test_native_prepare_overlap_pair_and_lock_refusals(self):
        inside = self.store.directory / 'engine'
        inside.mkdir()
        shutil.copy2(self.engine, inside / 'engine')
        expected, actual, result = self.native_prepare(engine=inside / 'engine', env=self.marker_env())
        self.assert_refused(expected, actual, self.store.directory, result,
                            'native workspace overlaps protected source/content/engine')
        self.assert_engine_queried(False)
        shutil.rmtree(inside)
        lock = self.store.directory / 'lodge.lock'
        lock.write_bytes(b'foreign-lock')
        expected, actual, result = self.native_prepare(env=self.marker_env())
        self.assertEqual(actual[0], 'frontend')
        self.assertTrue(actual[1].startswith('frontend writer lock exists: '), actual)
        self.assertEqual(actual[1], expected[1])
        self.assertEqual((result / 'lodge.lock').read_bytes(), b'foreign-lock')
        self.assert_engine_queried(False)
        lock.unlink()
        (self.source / 'trophy00.sab').unlink()
        with self.store.transaction() as data:
            a = data['associations'][self.association]
            a['files'] = [f for f in a['files'] if f['kind'] == 'sav']
        expected, actual, result = self.native_prepare(env=self.marker_env())
        self.assert_refused(expected, actual, self.store.directory, result,
                            'engine contract v1 requires an existing complete SAV/SAB pair')
        self.assert_engine_queried(False)
        expected, actual, result = self.native_prepare(adapter='hunt', env=self.marker_env())
        self.assert_refused(expected, actual, self.store.directory, result,
                            'hunt contract requires an existing complete SAV/SAB pair')
        self.assert_engine_queried(False)
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
        self.assertEqual(native('query-contract', str(self.engine), env={BEHAVIOR: 'bad-contract'}),
                         ('frontend', 'unsupported engine session contract'))
        with patch.dict(os.environ, {BEHAVIOR: 'bad-contract'}):
            with self.assertRaisesRegex(FrontendError, 'unsupported engine session contract'):
                native_session.query_contract(evidence)
        # The codec helper answers the query with unrelated JSON; the refusal must match the reference.
        self.assertEqual(native('query-contract', str(PROBE)),
                         self.python(lambda: native_session.query_contract(native_session.executable_evidence(PROBE))))
        # A successful query invokes the engine exactly once per side; a refused trust never does.
        env = self.marker_env()
        self.assertEqual(native('query-contract', str(self.engine), env=env)[0], 'ok')
        with patch.dict(os.environ, env):
            native_session.query_contract(evidence)
        self.assert_engine_queried(True)

    def test_capability_query_runs_in_a_fresh_temporary_directory(self):
        """The trusted engine must never see the caller's working directory (review B1)."""
        if os.name != 'posix':
            self.assertTrue(True, 'the authored shell engine needs a POSIX shebang; Windows keeps the CreateProcess cwd path')
            return
        engine = self.base / 'engine.sh'
        engine.write_text('#!/bin/sh\necho touched > "$PWD/ENGINE_WROTE_HERE"\n'
                          "printf '%s' '" + json.dumps(CAPABILITY, separators=(',', ':')) + "'\n")
        engine.chmod(0o755)
        caller = self.base / 'caller-cwd'
        caller.mkdir()
        temp = Path(tempfile.gettempdir())
        leftovers = lambda: sorted(p.name for p in temp.iterdir() if p.name.startswith('c2-contract-'))
        before = leftovers()
        self.assertEqual(native('query-contract', str(engine), cwd=caller), ('ok', CAPABILITY))
        self.assertEqual(list(caller.iterdir()), [], 'the engine wrote into the caller working directory')
        self.assertEqual(leftovers(), before, 'the temporary query directory leaked')
        previous = os.getcwd()
        os.chdir(caller)
        try:
            self.assertEqual(native_session.query_contract(native_session.executable_evidence(str(engine))), CAPABILITY)
        finally:
            os.chdir(previous)
        self.assertEqual(list(caller.iterdir()), [])
        self.assertEqual(leftovers(), before)

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
        selection, expected = reference('hunt')
        self.assertEqual(expected[1], 'legacy sessions cannot select an import in an upgraded store; prepare a generation-pinned hunt')
        self.assertEqual(native('native-pins', str(self.store.directory), 'hunt', self.association, PROBE, 'fixture-policy',
                                stdin={'selection': selection}), expected)


class Run(Base):
    """Execution over the owned child supervisor: fresh preflight, durable transitions, cleanup."""

    def synthetic_pipeline(self, scenario='unchanged', timeout=5, probe=PROBE):
        def reference():
            return run_session(self.store, self.py_prepare_synthetic(scenario=scenario, timeout=timeout, probe=probe)['id'], probe)
        def candidate():
            kind, prepared = self.nt_prepare_synthetic(scenario=scenario, timeout=timeout, probe=probe)
            self.assertEqual(kind, 'ok', prepared)
            return native('run', str(self.store.directory), prepared['id'], probe or '-')
        return self.both(reference, candidate)

    def test_synthetic_run_matches_reference_for_every_scenario(self):
        outcomes = {'nonzero': (7, 'exited'), 'changed-nonzero': (7, 'exited'), 'hang': (None, 'timeout'),
                    'terminated': (None, 'exited')}
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario):
                expected, actual, result = self.synthetic_pipeline(scenario, timeout=0.2 if scenario == 'hang' else 5)
                journal = self.assert_same_store(expected, actual, self.store.directory, result, synthetic=True)
                self.assertEqual(journal['state'], 'returned')
                code, reason = outcomes.get(scenario, (0, 'exited'))
                if code is not None:
                    self.assertEqual(journal['process']['exit_code'], code)
                else:
                    self.assertNotEqual(journal['process']['exit_code'], 0)
                self.assertEqual(journal['process']['stop_reason'], reason)
                self.assertEqual([e['state'] for e in journal['transitions']], ['prepared', 'launching', 'running', 'returned'])
                self.assertEqual(journal['capabilities']['synthetic_child_lifecycle'], 'completed')
                self.assertFalse(journal['capabilities']['engine_process_executed'])
                root = result / 'sessions' / journal['id']
                receipt = json.loads((root / 'logs/stdout.log').read_bytes().partition(b'\n')[0])
                self.assertEqual(receipt['cwd'], str(self.store.directory / 'sessions' / journal['id'] / 'work'))
                self.assertEqual(receipt['argv'], journal['execution']['argv'][1:])
                self.assertEqual(receipt['pid'], journal['process']['pid'])
                if scenario == 'logs':
                    for log in journal['logs'].values():
                        self.assertTrue(log['truncated'])
                        self.assertEqual((root / log['path']).stat().st_size, LOG_LIMIT)
                self.assertFalse(child_running(str(root / 'work')))

    def test_synthetic_cancellation_stops_the_owned_child(self):
        # Both sides cancel a 60 s hang 150 ms after launch, once the receipt is out.
        def reference():
            cancel = threading.Event()
            timer = threading.Timer(0.15, cancel.set)
            prepared = self.py_prepare_synthetic(scenario='hang', timeout=20)
            timer.start()
            try:
                return run_session(self.store, prepared['id'], PROBE, cancel)
            finally:
                timer.cancel()
        def candidate():
            kind, prepared = self.nt_prepare_synthetic(scenario='hang', timeout=20)
            self.assertEqual(kind, 'ok', prepared)
            return native('run-cancel', str(self.store.directory), prepared['id'], PROBE, '150')
        expected, actual, result = self.both(reference, candidate)
        journal = self.assert_same_store(expected, actual, self.store.directory, result, synthetic=True)
        self.assertEqual(journal['process']['stop_reason'], 'cancelled')
        self.assertIsInstance(journal['process']['exit_code'], int)
        self.assertNotEqual(journal['process']['exit_code'], 0)
        self.assertFalse(child_running(str(result / 'sessions' / journal['id'] / 'work')))

    def test_python_prepared_synthetic_journal_is_refused_without_execution(self):
        prepared = self.py_prepare_synthetic(scenario='sav')
        expected, actual, result = self.both(lambda: run_session(self.store, prepared['id'], PROBE),
                                             lambda: native('run', str(self.store.directory), prepared['id'], PROBE))
        self.assertEqual(expected[1]['state'], 'returned')
        self.assertEqual(actual[0], 'ok', actual)
        failed = actual[1]
        self.assertEqual(failed['state'], 'failed')
        self.assertIsNone(failed['process'])
        self.assertEqual(failed['diagnostics'], [{'code': 'preflight-failed',
                         'message': 'execution evidence changed or command is not the fixed synthetic fixture'}])
        root = result / 'sessions' / prepared['id']
        self.assertEqual(list((root / 'logs').iterdir()), [])
        self.assertEqual(capture(root / 'work/state'), capture(root / 'baseline'))
        self.assertEqual(read_journal(Store(result), prepared['id']), failed)
        # The reverse direction refuses identically: the reference never runs the compiled child.
        kind, prepared = self.nt_prepare_synthetic(scenario='sav')
        self.assertEqual(kind, 'ok', prepared)
        expected, actual, result = self.both(lambda: run_session(self.store, prepared['id'], PROBE),
                                             lambda: native('run', str(self.store.directory), prepared['id'], PROBE))
        self.assertEqual(actual[1]['state'], 'returned')
        self.assertEqual(expected[1]['state'], 'failed')
        self.assertEqual(expected[1]['diagnostics'], failed['diagnostics'])

    def test_synthetic_preflight_refusals_match_reference(self):
        """Each side prepares its own journal (the spec check would otherwise refuse the
        other implementation's fixture first), then the same mutation is applied to it."""
        def mutate(target, j):
            root = self.root(j)
            if target in ('fixture', 'codec', 'selection', 'scenario', 'timeout', 'kind'):
                if target == 'fixture':
                    j['execution']['fixture']['sha256'] = '0' * 64
                elif target == 'codec':
                    j['pins']['codec']['sha256'] = '0' * 64
                elif target == 'selection':
                    j['pins']['selection']['equipment'] = ['equipment:0']
                elif target == 'scenario':
                    j['execution']['scenario'] = 'nope'
                elif target == 'timeout':
                    j['execution']['timeout_seconds'] = 300
                else:
                    j['execution'].update(kind='native-engine', argv=['/arbitrary/engine'])
                persist(root, j)
            elif target == 'hunter':
                with self.store.transaction() as data:
                    data['hunters'][j['pins']['hunter_id']]['name'] = 'Changed'
            elif target == 'logs':
                (root / 'logs/stdout.log').write_bytes(b'stale')
            elif target == 'extra-work':
                (root / 'work/extra').write_bytes(b'x')
            elif target == 'link':
                state = root / 'work/state/trophy00.sav'
                state.unlink()
                state.symlink_to(self.source / 'trophy00.sav')
            elif target != 'relaunch':
                path = {'content': self.game / 'HUNTDAT/AREAS/AREA1.MAP', 'engine': self.game / 'CARN2.EXE',
                        'snapshot': self.source / 'trophy00.sav', 'workspace': root / 'work/state/trophy00.sav',
                        'baseline': root / 'baseline/trophy00.sav'}[target]
                path.write_bytes(path.read_bytes() + b'changed')

        def restore(target, j):
            if target in ('content', 'engine'):
                for path in (self.game / 'HUNTDAT/AREAS/AREA1.MAP', self.game / 'CARN2.EXE'):
                    path.write_bytes(self.game_before[path.relative_to(self.game).as_posix()])
            if target == 'snapshot':
                (self.source / 'trophy00.sav').write_bytes(save_bytes())
            if target == 'hunter':
                with self.store.transaction() as data:
                    data['hunters'][j['pins']['hunter_id']]['name'] = 'Synthetic test hunter'

        for target in ('relaunch', 'content', 'engine', 'snapshot', 'workspace', 'baseline', 'fixture', 'codec',
                       'selection', 'hunter', 'scenario', 'timeout', 'kind', 'logs', 'extra-work', 'link'):
            with self.subTest(target=target):
                if target == 'link' and os.name != 'posix':
                    continue
                env = {}
                journals = []

                def reference():
                    j = self.py_prepare_synthetic()
                    if target == 'relaunch':
                        run_session(self.store, j['id'], PROBE)
                    mutate(target, j)
                    journals.append(j)
                    try:
                        return run_session(self.store, j['id'], PROBE)
                    finally:
                        restore(target, j)

                def candidate():
                    kind, j = self.nt_prepare_synthetic()
                    self.assertEqual(kind, 'ok', j)
                    if target == 'relaunch':
                        self.assertEqual(native('run', str(self.store.directory), j['id'], PROBE, env=env)[0], 'ok')
                    mutate(target, j)
                    journals.append(j)
                    try:
                        return native('run', str(self.store.directory), j['id'], PROBE, env=env)
                    finally:
                        restore(target, j)

                expected, actual, result = self.both(reference, candidate, env=env)
                if target == 'relaunch':
                    self.assertEqual(actual, ('frontend', 'only a prepared session can launch; recovery never relaunches'))
                    self.assertEqual(expected, actual)
                    continue
                journal = self.assert_same_store(expected, actual, self.store.directory, result, synthetic=True,
                                                 execution=target not in ('fixture', 'kind'))
                self.assertEqual(journal['state'], 'failed')
                self.assertIsNone(journal['process'])
                self.assertEqual(journal['diagnostics'][-1]['code'], 'preflight-failed')
                self.assertEqual([e['state'] for e in journal['transitions']], ['prepared', 'failed'])
                self.assertEqual(list((result / 'sessions' / journal['id'] / 'logs').iterdir()),
                                 [result / 'sessions' / journal['id'] / 'logs/stdout.log'] if target == 'logs' else [])
                self.assertFalse(child_running(str(result / 'sessions' / journal['id'] / 'work')))

    def test_codec_drift_is_refused_before_helper_execution(self):
        helper_evidence = native_session.executable_evidence(HELPER)
        j = self.py_prepare_synthetic(probe=HELPER)
        self.assertEqual(j['pins']['codec'], helper_evidence)
        j['pins']['codec']['sha256'] = '0' * 64
        persist(self.root(j), j)
        marker = self.base / 'codec-invocations'
        env = {'C2_TEST_PROBE_MARKER': str(marker)}
        expected, actual, result = self.both(lambda: run_session(self.store, j['id'], HELPER),
                                             lambda: native('run', str(self.store.directory), j['id'], HELPER, env=env), env=env)
        self.assertFalse(marker.exists())
        journal = self.assert_same_store(expected, actual, self.store.directory, result, synthetic=True)
        self.assertEqual(journal['state'], 'failed')
        self.assertEqual(journal['diagnostics'][-1]['message'], 'pinned codec evidence changed; helper not executed')

    def test_failed_running_journal_write_still_stops_the_child(self):
        kind, prepared = self.nt_prepare_synthetic(scenario='hang', timeout=20)
        self.assertEqual(kind, 'ok', prepared)
        root = self.root(prepared)
        before = time.monotonic()
        kind, rest = native('run', str(self.store.directory), prepared['id'], PROBE, 'fail-running')
        self.assertEqual(kind, 'oserror', rest)
        self.assertIn('simulated disk failure', rest)
        self.assertLess(time.monotonic() - before, 15)
        self.assertEqual(read_journal(self.store, prepared['id'])['state'], 'launching')
        self.assertFalse((self.store.directory / 'lodge.lock').exists())
        self.assertFalse(child_running(str(root / 'work')))
        for log in ('stdout.log', 'stderr.log'):
            self.assertTrue((root / 'logs' / log).exists())
        recovered = native('recover', str(self.store.directory), prepared['id'], PROBE, 'fixture-policy')
        self.assertEqual(recovered[0], 'ok', recovered)
        self.assertEqual(recovered[1]['state'], 'interrupted')
        self.assertEqual(recovered[1]['diagnostics'][-1]['code'], 'process-ownership-lost')
        self.assertEqual(recover_session(self.store, prepared['id'], PROBE), recovered[1])

    def test_child_detection_sees_a_live_synthetic_child(self):
        """The /proc probe used by the other tests must actually detect a hanging child."""
        if child_running('/nonexistent') is None:
            self.assertTrue(True, 'no /proc on this host; the detection helper reports None')
            return
        work = self.base / 'detect' / 'work'
        (work / 'state').mkdir(parents=True)
        (work / 'state/trophy00.sav').write_bytes(save_bytes())
        (work / 'state/trophy00.sab').write_bytes(room_bytes())
        child = subprocess.Popen([CHILD, '--scenario', 'hang', '--slot', '0', '--literal', 'x'], cwd=work,
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        try:
            child.stdout.readline()
            self.assertTrue(child_running(work))
        finally:
            child.kill()
            child.wait(timeout=10)
        self.assertFalse(child_running(work))

    def test_interrupt_before_launch_before_spawn_and_while_running(self):
        """The KeyboardInterrupt analogue of the reference CLI (review B2)."""
        kind, prepared = self.nt_prepare_synthetic(scenario='hang', timeout=20)
        self.assertEqual(kind, 'ok', prepared)
        root = self.root(prepared)
        self.assertEqual(native('run-interrupt', str(self.store.directory), prepared['id'], PROBE, 'before-launch'),
                         ('interrupted', 'interrupted before launch; session remains prepared'))
        self.assertEqual(read_journal(self.store, prepared['id']), prepared)
        self.assertEqual(list((root / 'logs').iterdir()), [])
        self.assertFalse((self.store.directory / 'lodge.lock').exists())
        self.assertFalse(child_running(root / 'work'))
        # A session interrupted before launch launches normally afterwards.
        kind, returned = native('run-cancel', str(self.store.directory), prepared['id'], PROBE, '150')
        self.assertEqual((kind, returned['state'], returned['process']['stop_reason']), ('ok', 'returned', 'cancelled'))
        kind, prepared = self.nt_prepare_synthetic(scenario='hang', timeout=20)
        root = self.root(prepared)
        kind, message = native('run-interrupt', str(self.store.directory), prepared['id'], PROBE, 'before-spawn')
        self.assertEqual(kind, 'interrupted', message)
        self.assertTrue(message.startswith('interrupted before spawn; session left launching'), message)
        journal = read_journal(self.store, prepared['id'])
        self.assertEqual(journal['state'], 'launching')
        self.assertEqual([e['state'] for e in journal['transitions']], ['prepared', 'launching'])
        self.assertIsNone(journal['process'])
        self.assertEqual(list((root / 'logs').iterdir()), [])
        self.assertFalse(child_running(root / 'work'))
        recovered = native('recover', str(self.store.directory), prepared['id'], PROBE, 'fixture-policy')
        self.assertEqual((recovered[0], recovered[1]['state'], recovered[1]['diagnostics'][-1]['code']),
                         ('ok', 'interrupted', 'process-ownership-lost'))
        self.assertEqual(recover_session(self.store, prepared['id'], PROBE), recovered[1])
        kind, prepared = self.nt_prepare_synthetic(scenario='hang', timeout=20)
        root = self.root(prepared)
        kind, journal = native('run-interrupt', str(self.store.directory), prepared['id'], PROBE, 'while-running')
        self.assertEqual(kind, 'ok', journal)
        self.assertEqual((journal['state'], journal['process']['stop_reason']), ('returned', 'cancelled'))
        self.assertEqual([e['state'] for e in journal['transitions']], ['prepared', 'launching', 'running', 'returned'])
        self.assertNotEqual(journal['process']['exit_code'], 0)
        self.assertFalse(child_running(root / 'work'))
        # A pre-set cancel flag still launches and then stops the child (reference Event semantics).
        kind, prepared = self.nt_prepare_synthetic(scenario='hang', timeout=20)
        kind, journal = native('run-cancel', str(self.store.directory), prepared['id'], PROBE, '0')
        self.assertEqual(kind, 'ok', journal)
        self.assertEqual((journal['state'], journal['process']['stop_reason']), ('returned', 'cancelled'))
        self.assertEqual([e['state'] for e in journal['transitions']], ['prepared', 'launching', 'running', 'returned'])
        # A pre-set interrupt on a native run never queries the engine and leaves the journal prepared.
        j = self.py_prepare_native('observer')
        env = self.marker_env()
        self.assertEqual(native('run-native-interrupt', str(self.store.directory), 'observer', j['id'], PROBE, 'fixture-policy',
                                stdin={'engine': str(self.engine), 'digest': self.digest}, env=env),
                         ('interrupted', 'interrupted before launch; session remains prepared'))
        self.assertFalse(self.engine_marker.exists())
        self.assertEqual(read_journal(self.store, j['id']), j)

    def native_pipeline(self, adapter='observer', behavior='', cancel_after=None, prepare_kwargs=None, run_kwargs=None):
        env = {BEHAVIOR: behavior} if behavior else {}
        prepare_kwargs, run_kwargs = prepare_kwargs or {}, run_kwargs or {}
        def reference():
            j = self.py_prepare_native(adapter, **prepare_kwargs)
            return self.py_run_native(adapter, j['id'], cancel_after=cancel_after, **run_kwargs)
        def candidate():
            kind, prepared = self.nt_prepare_native(adapter, **prepare_kwargs)
            self.assertEqual(kind, 'ok', prepared)
            return self.nt_run_native(adapter, prepared['id'], cancel_after=cancel_after, env=env, **run_kwargs)
        return self.both(reference, candidate, env=env)

    def test_native_run_matches_reference_for_every_adapter_and_behavior(self):
        for schema in (2, 3, 4):
            adapter = 'observer' if schema == 2 else 'hunt'
            if schema == 4:
                upgrade_store(self.store)
            for behavior in ('', 'changed', 'nonzero', 'corrupt', 'hang'):
                with self.subTest(schema=schema, behavior=behavior):
                    expected, actual, result = self.native_pipeline(adapter, behavior, cancel_after=200 if behavior == 'hang' else None)
                    journal = self.assert_same_store(expected, actual, self.store.directory, result)
                    self.assertEqual(journal['schema_version'], schema)
                    self.assertEqual(journal['state'], 'returned')
                    self.assertEqual(journal['process']['exit_code'], {'nonzero': 7, 'corrupt': 0}.get(behavior, 0) if behavior != 'hang' else journal['process']['exit_code'])
                    self.assertEqual(journal['process']['stop_reason'], 'cancelled' if behavior == 'hang' else 'exited')
                    self.assertTrue(journal['capabilities']['engine_process_executed'])
                    lifecycle = 'native_observer_lifecycle' if schema == 2 else 'native_hunt_lifecycle'
                    self.assertEqual(journal['capabilities'][lifecycle], 'completed')
                    self.assertEqual(journal['capabilities']['synthetic_child_lifecycle'], 'not-executed')
                    root = result / 'sessions' / journal['id']
                    if behavior in ('', 'changed'):
                        self.assertIn(b'not Genesis', (root / 'work/output/render.log').read_bytes())
                    self.assertFalse(child_running(str(root / 'work')))

    def test_native_run_refusals_match_reference(self):
        observer = self.py_prepare_native('observer')
        hunt = self.py_prepare_native('hunt')
        synthetic = self.py_prepare_synthetic()
        cases = [
            ('observer', hunt['id'], {}, 'native command does not match the prepared session kind'),
            ('hunt', observer['id'], {}, 'native-hunt run requires a normal-hunt journal'),
            ('observer', synthetic['id'], {}, 'unsupported native session kind/version'),
            ('hunt', synthetic['id'], {}, 'unsupported native session kind/version'),
            ('observer', observer['id'], dict(experimental=False), 'native execution requires the explicit experimental gate'),
            ('observer', observer['id'], dict(digest='0' * 64), 'selected engine does not match the explicitly trusted hash'),
            ('observer', observer['id'], dict(digest='x'), 'explicit trusted engine SHA-256 is required'),
            ('hunt', hunt['id'], dict(engine=self.base / 'absent'), None),
        ]
        for adapter, identity, kwargs, message in cases:
            with self.subTest(adapter=adapter, kwargs=kwargs, message=message):
                env = self.marker_env()
                expected, actual, result = self.both(lambda: self.py_run_native(adapter, identity, **kwargs),
                                                     lambda: self.nt_run_native(adapter, identity, env=env, **kwargs), env=env)
                if message is None:
                    self.assertEqual(expected[0], 'oserror')
                self.assert_refused(expected, actual, self.store.directory, result, message)
                self.assert_engine_queried(False)
        expected, actual, result = self.both(lambda: run_session(self.store, observer['id'], PROBE),
                                             lambda: native('run', str(self.store.directory), observer['id'], PROBE))
        self.assert_refused(expected, actual, self.store.directory, result,
                            'native observer requires the separate gated developer run command')

    def test_native_preflight_failures_match_reference_without_spawning(self):
        for mutation in ('argv', 'contract-type', 'shell', 'baseline', 'config', 'output', 'tampered-selection',
                         'engine-changed', 'bad-contract', 'logs', 'stale-generation'):
            with self.subTest(mutation=mutation):
                if mutation == 'stale-generation':
                    upgrade_store(self.store)
                j = self.py_prepare_native('hunt')
                root = self.root(j)
                engine, env = self.engine, {}
                if mutation == 'argv':
                    j['execution']['argv'].append('-debug'); persist(root, j)
                elif mutation == 'contract-type':
                    j['execution']['contract']['version'] = True; persist(root, j)
                elif mutation == 'shell':
                    j['execution']['shell'] = 0; persist(root, j)
                elif mutation == 'baseline':
                    (root / 'baseline/trophy00.sav').write_bytes(b'changed')
                elif mutation == 'config':
                    (root / 'work/config/config.cfg').write_bytes(b'fov 90')
                elif mutation == 'output':
                    (root / 'work/output/render.log').write_bytes(b'preexisting')
                elif mutation == 'tampered-selection':
                    j['pins']['selection']['licenses'] = ['licenses:8']; persist(root, j)
                elif mutation == 'engine-changed':
                    directory = self.base / 'selected-engine'
                    directory.mkdir(exist_ok=True)
                    engine = directory / self.engine.name
                    shutil.copy2(self.engine, engine)
                    with engine.open('ab') as stream:
                        stream.write(b'changed trusted image')
                elif mutation == 'bad-contract':
                    env = {BEHAVIOR: 'bad-contract'}
                elif mutation == 'logs':
                    (root / 'logs/stderr.log').write_bytes(b'')
                else:
                    with patch('lodge.native_continuation.hunt_policy', side_effect=hunt_double):
                        first = self.candidate_journal()
                        accept_candidate(self.store, first['id'], first['pins']['generation_id'], candidate_digest(first), PROBE)
                digest = hashlib.sha256(Path(engine).read_bytes()).hexdigest() if mutation == 'engine-changed' else DEFAULT
                env = {**env, **self.marker_env()}
                expected, actual, result = self.both(lambda: self.py_run_native('hunt', j['id'], engine=engine, digest=digest),
                                                     lambda: self.nt_run_native('hunt', j['id'], engine=engine, digest=digest, env=env), env=env)
                # Trust, evidence and pin checks precede the capability query; the rest follow it.
                self.assert_engine_queried(mutation not in ('engine-changed', 'tampered-selection', 'stale-generation'))
                journal = self.assert_same_store(expected, actual, self.store.directory, result)
                self.assertEqual(journal['state'], 'failed')
                self.assertIsNone(journal['process'])
                self.assertEqual(journal['diagnostics'][-1]['code'], 'preflight-failed')
                if mutation == 'engine-changed':
                    self.assertEqual(journal['diagnostics'][-1]['message'], 'selected engine differs from the prepared trusted engine')
                self.assertEqual(list((result / 'sessions' / journal['id'] / 'logs').iterdir()),
                                 [result / 'sessions' / journal['id'] / 'logs/stderr.log'] if mutation == 'logs' else [])
                self.assertFalse((result / 'sessions' / journal['id'] / 'work/output/render.log').exists() and mutation != 'output')

    def candidate_journal(self):
        j = self.py_prepare_native('hunt')
        with patch.dict(os.environ, {BEHAVIOR: 'changed'}):
            self.py_run_native('hunt', j['id'])
        result = reconcile_session(self.store, j['id'], PROBE)
        self.assertEqual(result['state'], 'candidate', result['diagnostics'])
        return result


class Reconcile(Base):
    """Reconciliation and recovery, including historical journals produced by the reference."""

    def reconciled(self, identity, fixture=True, mutate=None, recover=False):
        reference = (lambda: self.py_recover(identity)) if recover else (lambda: self.py_reconcile(identity))
        candidate = (lambda: self.nt_recover(identity, fixture=fixture)) if recover else (lambda: self.nt_reconcile(identity, fixture=fixture))
        return self.both(reference, candidate, fixture=fixture, mutate=mutate)

    def assert_same_reconciled(self, expected, actual, result, synthetic=False):
        journal = self.assert_same_store(expected, actual, self.store.directory, result, synthetic=synthetic)
        self.assertEqual(self.nt_inspect(journal['id'])[1] if False else read_journal(Store(result), journal['id']), actual[1])
        return journal

    def synthetic_returned(self, scenario='unchanged', timeout=5):
        return run_session(self.store, self.py_prepare_synthetic(scenario=scenario, timeout=timeout)['id'], PROBE)

    def test_clean_synthetic_candidates_match_reference_and_are_idempotent(self):
        for scenario, changed in (('unchanged', []), ('sav', ['trophy00.sav']), ('pair', ['trophy00.sab', 'trophy00.sav'])):
            with self.subTest(scenario=scenario):
                j = self.synthetic_returned(scenario)
                expected, actual, result = self.reconciled(j['id'])
                journal = self.assert_same_reconciled(expected, actual, result, synthetic=True)
                self.assertEqual(journal['state'], 'candidate')
                self.assertEqual(journal['reconciliation']['changed_members'], changed)
                self.assertEqual(journal['reconciliation']['comparison_status'], 'complete')
                self.assertEqual(journal['reconciliation']['observation'], {
                    'inventory': 'complete', 'byte_capture': 'complete', 'retained_capture': 'verified', 'codec_inspection': 'complete'})
                self.assertEqual(journal['reconciliation']['authority'], 'original-managed-snapshot')
                self.assertEqual(journal['capabilities']['returned_native_state_readable'], 'yes')
                root = result / 'sessions' / journal['id']
                self.assertEqual(capture(root / 'returned'), capture(root / 'work/state'))
                if scenario != 'unchanged':
                    self.assertEqual(journal['returned_observation']['trophy00.sav']['score'], 175)
                # A terminal journal is returned unchanged by reconcile and recover, without a rewrite.
                bytes_before = (self.root(j) / 'journal.json').read_bytes()
                terminal = read_journal(self.store, j['id'])
                for recover in (False, True):
                    expected, actual, result = self.reconciled(j['id'], recover=recover)
                    self.assertEqual(actual, ('ok', terminal))
                    self.assertEqual(expected, ('ok', terminal))
                    self.assertEqual((result / 'sessions' / j['id'] / 'journal.json').read_bytes(), bytes_before)
                self.assertEqual(self.nt_inspect(j['id']), ('ok', terminal))

    def test_failure_matrix_quarantines_with_exact_evidence(self):
        cases = {'nonzero': 'unclean-process-return', 'changed-nonzero': 'unclean-process-return',
                 'corrupt-sav': 'unreadable-state', 'corrupt-sab': 'unreadable-state',
                 'missing-sab': 'missing-state-member', 'deleted-sav': 'missing-state-member',
                 'extra': 'unexpected-state-member', 'registration': 'registration-mismatch',
                 'hang': 'unclean-process-return', 'terminated': 'unclean-process-return'}
        for scenario, diagnostic in cases.items():
            with self.subTest(scenario=scenario):
                j = self.synthetic_returned(scenario, timeout=0.2 if scenario == 'hang' else 5)
                expected, actual, result = self.reconciled(j['id'])
                journal = self.assert_same_reconciled(expected, actual, result, synthetic=True)
                self.assertEqual(journal['state'], 'quarantined')
                self.assertIn(diagnostic, [d['code'] for d in journal['diagnostics']])
                root = result / 'sessions' / journal['id']
                self.assertEqual(capture(root / 'returned'), capture(root / 'work/state'))

    def test_recovery_from_every_interruption_point(self):
        for stage in ('launching', 'running', 'returned', 'inspecting', 'partial-copy', 'partial-copy-altered',
                      'partial-copy-work-mutated', 'crash-before-copy-mutated'):
            with self.subTest(stage=stage):
                j = self.py_prepare_synthetic(scenario='pair')
                root = self.root(j)
                if stage in ('launching', 'running'):
                    transition(root, j, 'launching')
                    if stage == 'running':
                        transition(root, j, 'running', process={'pid': os.getpid()})
                else:
                    j = run_session(self.store, j['id'], PROBE)
                    if stage != 'returned':
                        transition(root, j, 'inspecting')
                    if stage.startswith('partial-copy') or stage == 'crash-before-copy-mutated':
                        entries, blobs = capture(root / 'work/state')
                        j['return_capture'] = entries
                        j['diagnostics'].append({'code': 'earlier-failure'})
                        persist(root, j)
                        if stage.startswith('partial-copy'):
                            saved = b'changed evidence' if stage == 'partial-copy-altered' else blobs['trophy00.sav']
                            write_blobs(root / 'returned', {'trophy00.sav': saved})
                        if stage.endswith('mutated'):
                            (root / 'work/state/trophy00.sav').write_bytes(save_bytes(score=999))
                expected, actual, result = self.reconciled(j['id'], recover=True)
                journal = self.assert_same_reconciled(expected, actual, result, synthetic=True)
                if stage in ('launching', 'running'):
                    self.assertEqual(journal['state'], 'interrupted')
                    self.assertEqual(journal['diagnostics'][-1], {'code': 'process-ownership-lost',
                        'message': 'Child may still exist. No signal, relaunch or state capture performed.'})
                    self.assertFalse((result / 'sessions' / j['id'] / 'returned').exists())
                    for recover in (True, False):
                        expected, actual, result = self.reconciled(j['id'], recover=recover)
                        if recover:
                            self.assertEqual(actual, expected)
                        else:
                            self.assert_refused(expected, actual, self.store.directory, result,
                                                'only a durably returned session may be reconciled')
                elif stage in ('returned', 'inspecting', 'partial-copy'):
                    self.assertEqual(journal['state'], 'candidate' if stage != 'partial-copy' else 'quarantined')
                    if stage == 'partial-copy':
                        self.assertEqual([d['code'] for d in journal['diagnostics']], ['earlier-failure'])
                else:
                    self.assertEqual(journal['state'], 'quarantined')
                    self.assertEqual(journal['return_capture'], entries)
                    if stage == 'partial-copy-altered':
                        self.assertEqual((result / 'sessions' / j['id'] / 'returned/trophy00.sav').read_bytes(), b'changed evidence')
                        self.assertIn('existing returned evidence changed; never overwritten', str(journal['diagnostics']))
                    else:
                        self.assertIn('returned state changed since durable capture; original capture retained', str(journal['diagnostics']))

    def test_returned_state_anomalies_match_reference(self):
        def link_extra(root):
            (root / 'work/state/outside').symlink_to(self.source)
        def hardlink_extra(root):
            os.link(root / 'work/state/trophy00.sav', root / 'work/state/trophy00.extra')
        def oversized_extra(root):
            with (root / 'work/state/trophy00.extra').open('wb') as stream:
                stream.truncate(16 * 1024 * 1024 + 1)
        def nested(root):
            (root / 'work/state/nested').mkdir()
            (root / 'work/state/nested/opaque').write_bytes(b'retain unexpected nested bytes')
        def added_room(root):
            (root / 'work/state/trophy00.sab').write_bytes(room_bytes())
        def source_drift(root):
            (self.source / 'trophy00.sav').write_bytes(save_bytes(score=999))
        def baseline_drift(root):
            (root / 'baseline/trophy00.sav').write_bytes(save_bytes(score=999))
        def work_extra(root):
            (root / 'work/extra').write_bytes(b'x')
        cases = [('nested', nested, 'unexpected-state-member'), ('hardlink', hardlink_extra, 'unsafe-state-entry'),
                 ('oversized', oversized_extra, 'unsafe-state-entry'), ('source', source_drift, 'source-review-required'),
                 ('baseline', baseline_drift, 'baseline-changed'), ('work-extra', work_extra, 'unexpected-workspace-entry')]
        if os.name == 'posix':
            cases.append(('link', link_extra, 'unsafe-state-entry'))
        for name, mutation, diagnostic in cases:
            with self.subTest(name=name):
                j = self.synthetic_returned('sav')
                root = self.root(j)
                # Hardlinks do not survive the pristine copy, so mutate each side in place.
                mutate = (lambda: mutation(root)) if name in ('hardlink', 'oversized', 'link') else None
                if not mutate:
                    mutation(root)
                try:
                    expected, actual, result = self.reconciled(j['id'], mutate=mutate)
                    journal = self.assert_same_reconciled(expected, actual, result, synthetic=True)
                finally:
                    if name == 'source':
                        (self.source / 'trophy00.sav').write_bytes(save_bytes())
                self.assertEqual(journal['state'], 'quarantined')
                self.assertIn(diagnostic, [d['code'] for d in journal['diagnostics']])
                if name == 'source':
                    self.assertIn('managed source membership/bytes differ or require review', str(journal['diagnostics']))
                if name == 'nested':
                    self.assertEqual((result / 'sessions' / j['id'] / 'returned/nested/opaque').read_bytes(), b'retain unexpected nested bytes')
                if name in ('hardlink', 'oversized', 'link'):
                    self.assertFalse((result / 'sessions' / j['id'] / 'returned/trophy00.extra').exists())
                    self.assertFalse((result / 'sessions' / j['id'] / 'returned/outside').exists())
        # Optional room absence, then a later addition, changes readability exactly as the reference says.
        (self.source / 'trophy00.sab').unlink()
        with self.store.transaction() as data:
            a = data['associations'][self.association]
            a['files'] = [f for f in a['files'] if f['kind'] == 'sav']
        j = self.synthetic_returned()
        expected, actual, result = self.reconciled(j['id'])
        self.assertEqual(self.assert_same_reconciled(expected, actual, result, synthetic=True)['state'], 'candidate')
        j = self.synthetic_returned()
        added_room(self.root(j))
        expected, actual, result = self.reconciled(j['id'])
        journal = self.assert_same_reconciled(expected, actual, result, synthetic=True)
        self.assertEqual(journal['state'], 'quarantined')
        self.assertEqual(journal['capabilities']['returned_native_state_readable'], 'no')
        self.assertEqual(journal['reconciliation']['changed_members'], ['trophy00.sab'])

    def test_reconcile_refusals_match_reference(self):
        prepared = self.py_prepare_synthetic()
        for identity, message in ((prepared['id'], 'only a durably returned session may be reconciled'),
                                  ('0a9b8c7d-1234-4abc-8def-001122334455', None), ('nope', 'invalid session UUID')):
            with self.subTest(identity=identity):
                expected, actual, result = self.reconciled(identity)
                if message:
                    self.assert_refused(expected, actual, self.store.directory, result, message)
                else:
                    self.assertEqual(actual[0], 'frontend')
                    self.assertTrue(actual[1].startswith('cannot read session journal: '), actual)
                    self.assertTrue(expected[1].startswith('cannot read session journal: '), expected)
        lock = self.store.directory / 'lodge.lock'
        lock.write_bytes(b'foreign-lock')
        expected, actual, result = self.reconciled(prepared['id'])
        self.assertEqual(actual[0], 'frontend')
        self.assertTrue(actual[1].startswith('frontend writer lock exists: '), actual)
        self.assertEqual(actual, expected)
        lock.unlink()

    def native_returned(self, adapter='hunt', behavior='', cancel_after=None):
        j = self.py_prepare_native(adapter)
        with patch.dict(os.environ, {BEHAVIOR: behavior} if behavior else {}):
            returned = self.py_run_native(adapter, j['id'], cancel_after=cancel_after)
        self.assertEqual(returned['state'], 'returned', returned['diagnostics'])
        return returned

    def test_native_reconcile_matches_reference_for_every_behavior(self):
        for schema in (2, 3, 4):
            adapter = 'observer' if schema == 2 else 'hunt'
            if schema == 4:
                upgrade_store(self.store)
            for behavior, state, code in (('', 'candidate', None), ('changed', 'candidate', None), ('nonzero', 'quarantined', 'unclean-process-return'),
                                          ('corrupt', 'quarantined', 'unreadable-state'), ('hang', 'quarantined', 'unclean-process-return')):
                with self.subTest(schema=schema, behavior=behavior):
                    j = self.native_returned(adapter, behavior, cancel_after=200 if behavior == 'hang' else None)
                    expected, actual, result = self.reconciled(j['id'])
                    journal = self.assert_same_reconciled(expected, actual, result)
                    self.assertEqual(journal['state'], state, journal['diagnostics'])
                    if code:
                        self.assertIn(code, [d['code'] for d in journal['diagnostics']])
                    self.assertEqual(journal['reconciliation']['authority'], 'managed-state-history' if schema == 4 else 'original-managed-snapshot')
                    self.assertEqual(journal['reconciliation']['promotion'], 'explicit-only' if schema == 4 else 'deferred')
                    if behavior == 'changed':
                        self.assertEqual(journal['reconciliation']['changed_members'], ['trophy00.sav'])
                        self.assertEqual(journal['returned_observation']['trophy00.sav']['score'],
                                         j['pins']['source_observation']['trophy00.sav']['score'] + 7)

    def test_native_return_anomalies_match_reference(self):
        screenshot = 'HUNT0001.BM' if os.name == 'nt' else 'HUNT0001.BMP'
        def mutation(name, root):
            if name == 'screenshot':
                (root / 'work/output' / screenshot).write_bytes(b'BM fixture')
            elif name == 'unclassified-screenshot':
                (root / 'work/output' / (screenshot + '.unclassified')).write_bytes(b'BM fixture')
            elif name == 'output-extra':
                (root / 'work/output/glperf-capture.csv').write_bytes(b'extra evidence')
            elif name == 'config-extra':
                (root / 'work/config/extra.cfg').write_bytes(b'extra evidence')
            elif name == 'state-extra':
                (root / 'work/state/extra.log').write_bytes(b'extra evidence')
            elif name == 'work-extra':
                (root / 'work/extra').write_bytes(b'extra evidence')
            elif name == 'config-and-state':
                (root / 'work/output/unexpected.txt').write_bytes(b'retained output anomaly')
                (root / 'work/state/trophy00.sav').write_bytes(save_bytes(score=175))
                (root / 'work/config/config.cfg').write_bytes(b'invalid config')
            elif name == 'truncated':
                (root / 'work/state/trophy00.sav').write_bytes(b'truncated')
            elif name == 'missing':
                (root / 'work/state/trophy00.sab').unlink()
            elif name == 'empty':
                (root / 'work/state/trophy00.sab').unlink()
                (root / 'work/state/trophy00.sav').unlink()
            elif name == 'oversized':
                with (root / 'work/state/trophy00.sav').open('wb') as stream:
                    stream.truncate(16 * 1024 * 1024 + 1)
            elif name == 'linked-state':
                (root / 'work/state').rename(root / 'retained-work')
                (root / 'work/state').symlink_to(root / 'retained-work', target_is_directory=True)
            elif name == 'linked-work':
                (root / 'work').rename(root / 'retained-work')
                (root / 'work').symlink_to(root / 'retained-work', target_is_directory=True)
        expectations = {
            'screenshot': ('candidate', None), 'unclassified-screenshot': ('quarantined', 'native-workspace-review-required'),
            'output-extra': ('quarantined', 'native-workspace-review-required'), 'config-extra': ('quarantined', 'native-workspace-review-required'),
            'state-extra': ('quarantined', 'unexpected-state-member'), 'work-extra': ('quarantined', 'native-workspace-review-required'),
            'config-and-state': ('quarantined', 'native-workspace-review-required'), 'truncated': ('quarantined', 'unreadable-state'),
            'missing': ('quarantined', 'missing-state-member'), 'empty': ('quarantined', 'missing-state-member'),
            'oversized': ('quarantined', 'unsafe-state-entry'),
        }
        if os.name == 'posix':
            expectations.update({'linked-state': ('quarantined', 'return-review-required'), 'linked-work': ('quarantined', 'return-review-required')})
        for name, (state, code) in expectations.items():
            with self.subTest(name=name):
                j = self.native_returned('observer')
                root = self.root(j)
                mutate = (lambda: mutation(name, root)) if name in ('oversized', 'linked-state', 'linked-work') else None
                if not mutate:
                    mutation(name, root)
                expected, actual, result = self.reconciled(j['id'], mutate=mutate)
                journal = self.assert_same_reconciled(expected, actual, result)
                self.assertEqual(journal['state'], state, journal['diagnostics'])
                if code:
                    self.assertIn(code, [d['code'] for d in journal['diagnostics']])
                if name.startswith('linked'):
                    self.assertEqual(journal['capabilities']['returned_native_state_readable'], 'unknown')
                    self.assertIsNone(journal['returned_members'])
                    self.assertEqual(journal['reconciliation']['comparison_status'], 'unavailable')
                    self.assertFalse((result / 'sessions' / j['id'] / 'returned').exists())
                if name == 'oversized':
                    self.assertEqual(journal['reconciliation']['observation']['byte_capture'], 'partial')
                    self.assertIsNone(journal['reconciliation']['changed_members'])
                    self.assertEqual(journal['capabilities']['returned_native_state_readable'], 'unknown')
                    self.assertEqual(list(journal['returned_observation']), ['trophy00.sab'])
                if name == 'config-and-state':
                    self.assertEqual(journal['returned_observation']['trophy00.sav']['score'], 175)
                    self.assertEqual(journal['reconciliation']['changed_members'], ['trophy00.sav'])

    def test_native_execution_evidence_drift_and_engine_change_on_return(self):
        directory = self.base / 'selected-engine'
        directory.mkdir()
        engine = directory / self.engine.name
        shutil.copy2(self.engine, engine)
        self.engine, self.digest = engine, hashlib.sha256(engine.read_bytes()).hexdigest()
        j = self.native_returned('observer')
        with engine.open('ab') as stream:
            stream.write(b'changed after return')
        expected, actual, result = self.reconciled(j['id'])
        journal = self.assert_same_reconciled(expected, actual, result)
        self.assertEqual(journal['state'], 'quarantined')
        self.assertIn('selected-engine-changed-on-return', [d['code'] for d in journal['diagnostics']])
        shutil.copy2(Path(ENGINE), engine)
        for field, value in (('contract', {**CAPABILITY, 'version': 99}), ('contract', {**CAPABILITY, 'version': True}),
                             ('shell', 0), ('argv', ['/unexpected']), ('cwd', '/unexpected'), ('config_sha256', '0' * 64),
                             ('timeout_seconds', 5)):
            code = 'source-review-required' if field == 'timeout_seconds' else 'native-execution-evidence-changed-on-return'
            with self.subTest(field=field, value=value):
                j = self.native_returned('observer')
                j['execution'][field] = value
                persist(self.root(j), j)
                expected, actual, result = self.reconciled(j['id'], recover=True)
                journal = self.assert_same_reconciled(expected, actual, result)
                self.assertEqual(journal['state'], 'quarantined')
                self.assertIn(code, [d['code'] for d in journal['diagnostics']])

    def test_native_recovery_never_signals_or_queries_and_keeps_findings_durable(self):
        for stage in ('launching', 'running'):
            with self.subTest(stage=stage):
                j = self.py_prepare_native('observer')
                root = self.root(j)
                transition(root, j, 'launching')
                if stage == 'running':
                    transition(root, j, 'running', process={'pid': os.getpid()})
                expected, actual, result = self.reconciled(j['id'], recover=True)
                journal = self.assert_same_reconciled(expected, actual, result)
                self.assertEqual(journal['state'], 'interrupted')
                self.assertFalse((result / 'sessions' / j['id'] / 'returned').exists())
                self.assertEqual(list((result / 'sessions' / j['id'] / 'work/output').iterdir()), [])
        for stage in ('returned', 'partial', 'divergent-returned', 'divergent-work'):
            with self.subTest(stage=stage):
                j = self.native_returned('observer')
                root = self.root(j)
                entries, blobs = capture(root / 'work/state')
                (root / 'work/output/extra.log').write_bytes(b'output anomaly')
                j['diagnostics'].append({'code': 'earlier-failure'})
                persist(root, j)
                if stage != 'returned':
                    transition(root, j, 'inspecting')
                    j['return_capture'] = entries
                    persist(root, j)
                    saved = b'divergent prior evidence' if stage == 'divergent-returned' else blobs['trophy00.sav']
                    write_blobs(root / 'returned', {'trophy00.sav': saved})
                    if stage == 'divergent-work':
                        (root / 'work/state/trophy00.sav').write_bytes(save_bytes(score=175))
                expected, actual, result = self.reconciled(j['id'], recover=True)
                journal = self.assert_same_reconciled(expected, actual, result)
                self.assertEqual(journal['state'], 'quarantined')
                self.assertEqual(journal['return_capture'], entries)
                self.assertIn('earlier-failure', [d['code'] for d in journal['diagnostics']])
                self.assertIn('unexpected native output', str(journal['diagnostics']))
                if stage.startswith('divergent'):
                    self.assertEqual((result / 'sessions' / j['id'] / 'returned/trophy00.sav').read_bytes(), saved)
                    self.assertIn('return-review-required', [d['code'] for d in journal['diagnostics']])
                    self.assertEqual(journal['reconciliation']['observation']['retained_capture'], 'unavailable')
                    self.assertEqual(journal['reconciliation']['changed_members'], ['trophy00.sav'] if stage == 'divergent-work' else [])

    def test_historical_terminal_journals_are_not_rewritten(self):
        for native_kind in (False, True):
            with self.subTest(native=native_kind):
                j = self.native_returned('observer') if native_kind else self.synthetic_returned('sav')
                result = reconcile_session(self.store, j['id'], PROBE)
                root = self.root(j)
                result['reconciliation'].pop('observation')
                result['reconciliation'].pop('comparison_status')
                persist(root, result)
                original = (root / 'journal.json').read_bytes()
                for recover in (True, False):
                    expected, actual, twin = self.reconciled(j['id'], recover=recover)
                    self.assertEqual(actual, ('ok', result))
                    self.assertEqual(expected, ('ok', result))
                    self.assertEqual((twin / 'sessions' / j['id'] / 'journal.json').read_bytes(), original)

    def test_native_schema_four_candidate_satisfies_reference_acceptance(self):
        """Hand-off contract for workstream C: a candidate produced entirely natively
        (prepare, run, reconcile) passes the unchanged reference validate_candidate."""
        head = upgrade_store(self.store)['associations'][self.association]
        def reference():
            j = self.py_prepare_native('hunt')
            self.py_run_native('hunt', j['id'])
            return reconcile_session(self.store, j['id'], PROBE)
        previews = []
        def candidate():
            kind, prepared = self.nt_prepare_native('hunt')
            self.assertEqual(kind, 'ok', prepared)
            kind, returned = self.nt_run_native('hunt', prepared['id'], env={BEHAVIOR: 'changed'})
            self.assertEqual(kind, 'ok', returned)
            self.assertEqual(returned['state'], 'returned', returned['diagnostics'])
            result = self.nt_reconcile(prepared['id'])
            # The unchanged reference preview (read-only) validates the NATIVE candidate in place.
            previews.append(preview_acceptance(self.store, prepared['id'], head, PROBE))
            return result
        expected, actual, result = self.both(reference, candidate, env={BEHAVIOR: 'changed'})
        journal = self.assert_same_store(expected, actual, self.store.directory, result)
        self.assertEqual((journal['schema_version'], journal['state']), (4, 'candidate'))
        self.assertEqual(journal['pins']['generation_id'], head)
        preview = previews[0]
        self.assertTrue(preview['allowed'], preview['diagnostics'])
        self.assertEqual(preview['status'], 'eligible')
        self.assertEqual(preview['candidate_sha256'], candidate_digest(actual[1]))
        # Then the reference accepts the native candidate at the path it was pinned to.
        self.store.directory.rename(self.base / 'python-result')
        result.rename(self.store.directory)
        accepted = accept_candidate(self.store, journal['id'], head, candidate_digest(actual[1]), PROBE)
        self.assertEqual(accepted['result'], 'accepted')
        self.assertNotEqual(accepted['current_generation'], head)
        self.assertEqual(read_journal(self.store, journal['id']), actual[1])


MODES = {'prepare': Prepare, 'run': Run, 'reconcile': Reconcile}


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
