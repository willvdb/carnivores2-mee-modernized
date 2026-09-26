"""Native-only end-to-end workflow (runtime-completion checkpoints 1-3).

Every product operation is performed by the native executables in BIN_DIR
(c2-frontend-native, c2-profile-probe, c2-frontend-synthetic-child) or by the
test-only c2-frontend-native-fixture dispatcher, which differs from production
only by the labelled asset-free policy double. This harness authors fixture
bytes, drives the binaries, and inspects results; it never calls lodge.*.

Python-free runtime gate: every invocation runs with PATH reduced to a
directory of `python`/`python3`/`py` shims that record any call and fail,
without PYTHON* variables, from a working directory outside the source tree.
With --sandbox (Linux, bubblewrap) each invocation additionally runs in a
filesystem namespace containing only the staged binaries, their shared
libraries, the engine fixture and the disposable test directory: no Python
interpreter exists there at all.

With --reference the identical workflow runs against the unchanged Python
frontend.py (reference_cli.py) instead, validating these expectations against
the oracle; the Python-free assertions are then not applicable.

usage: test_native_workflow.py BIN_DIR FIXTURE_CLI ENGINE [--sandbox | --reference]
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest

BIN, FIXTURE_CLI, ENGINE = (Path(a).resolve() for a in sys.argv[1:4])
SANDBOX = '--sandbox' in sys.argv[4:]
REFERENCE = '--reference' in sys.argv[4:]
BWRAP = shutil.which('bwrap')  # resolved before PATH is reduced to the shims
del sys.argv[1:]
EXE = '.exe' if os.name == 'nt' else ''
NATIVE, PROBE = BIN / ('c2-frontend-native' + EXE), BIN / ('c2-profile-probe' + EXE)
ENGINE_SHA = hashlib.sha256(ENGINE.read_bytes()).hexdigest()
SCRIPT = """weapons {
{
 name = 'Synthetic weapon'
}
}
characters {
{
 name = 'Synthetic group'
 ai = 10
}
}
prices {
 area = 5
 dino = 10
 weapon = 20
}
"""
RES = "weapons {\n}\ncharacters {\n{\n name = 'Synthetic animal'\n ai = 10\n}\n}\n"


def save_bytes(slot=0, score=100):
    result = bytearray((i * 73 + 19) % 256 for i in range(1660))
    result[:128] = b'Test hunter\0' + bytes(range(116))
    struct.pack_into('<iii', result, 128, slot, score, 1000)
    return bytes(result)


def score(content):
    return struct.unpack_from('<i', content, 132)[0]


def shared_libraries(paths):
    libraries = set()
    for path in paths:
        out = subprocess.run(['ldd', str(path)], capture_output=True, text=True).stdout
        for line in out.splitlines():
            parts = line.split()
            for part in parts:
                if part.startswith('/'):
                    libraries.add(Path(part).resolve())
                    libraries.add(Path(part))
    return sorted(libraries)


class Workflow(unittest.TestCase):
    maxDiff = None

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='c2-native-workflow-')
        cls.base = Path(cls.temp.name).resolve()
        cls.shims = cls.base / 'shims'
        cls.shims.mkdir()
        cls.marker = cls.base / 'python-invoked'
        for name in ('python', 'python3', 'python3.12', 'py'):
            if os.name == 'nt':
                (cls.shims / (name + '.bat')).write_text(f'@echo invoked > "{cls.marker}"\r\n@exit /b 97\r\n')
            else:
                shim = cls.shims / name
                shim.write_text(f'#!/bin/sh\necho "$0 $*" >> "{cls.marker}"\nexit 97\n')
                shim.chmod(0o755)
        cls.work = cls.base / 'cwd'
        cls.work.mkdir()
        cls.env = {k: v for k, v in os.environ.items()
                   if not k.upper().startswith('PYTHON') and k not in ('C2_PROFILE_PROBE', 'PATH', 'Path')}
        cls.env['PATH'] = str(cls.shims)
        if REFERENCE:
            cls.env = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
        if SANDBOX:
            if not BWRAP or subprocess.run([BWRAP, '--ro-bind', '/', '/', '/bin/true'],
                                           capture_output=True).returncode:
                print('bubblewrap unavailable or user namespaces blocked on this host')
                raise SystemExit(77)
            cls.libraries = shared_libraries([NATIVE, PROBE, BIN / 'c2-frontend-synthetic-child', FIXTURE_CLI, ENGINE])
        cls.game = cls.author_game(cls.base / 'Game')
        cls.game_bytes = cls.native_files(cls.game)
        cls.store = cls.base / 'Lodge'

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @staticmethod
    def author_game(root):
        for part in ('HUNTDAT/MENU/TXT', 'HUNTDAT/MENU/PICS', 'HUNTDAT/AREAS'):
            (root / part).mkdir(parents=True)
        (root / 'HUNTDAT/_RES.TXT').write_text(RES)
        (root / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (root / 'HUNTDAT/AREAS/AREA1.MAP').write_bytes(b'synthetic map evidence')
        (root / 'HUNTDAT/AREAS/AREA1.RSC').write_bytes(b'synthetic resource evidence')
        (root / 'CARN2.EXE').write_bytes(b'synthetic executable evidence - never run')
        (root / 'trophy00.sav').write_bytes(save_bytes())
        (root / 'trophy00.sab').write_bytes(bytes((i * 37) % 256 for i in range(7176)))
        return root

    @staticmethod
    def native_files(root):
        return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted(root.rglob('*')) if p.is_file()}

    def command(self, args, fixture):
        if REFERENCE:
            driver = Path(__file__).with_name('reference_cli.py')
            return [sys.executable, str(driver), *(['--fixture-policy'] if fixture else []),
                    '--store', str(self.store), '--probe', str(PROBE), *map(str, args)]
        program = FIXTURE_CLI if fixture else NATIVE
        argv = [str(program), '--store', str(self.store), '--probe', str(PROBE), *map(str, args)]
        if not SANDBOX:
            return argv
        # No pid namespace: journal PIDs stay host PIDs (the harness reaps an
        # orphaned test child by that PID). --die-with-parent keeps a killed
        # sandbox equivalent to a killed frontend.
        wrap = [BWRAP, '--die-with-parent', '--dev', '/dev', '--proc', '/proc',
                '--tmpfs', '/tmp', '--bind', str(self.base), str(self.base),
                '--ro-bind', str(BIN), str(BIN), '--ro-bind', str(FIXTURE_CLI), str(FIXTURE_CLI),
                '--ro-bind', str(ENGINE), str(ENGINE), '--chdir', str(self.work)]
        for library in self.libraries:
            wrap += ['--ro-bind', str(library), str(library)]
        return wrap + argv

    def spawn(self, args, fixture=False, env=None):
        environment = {**self.env, **(env or {})}
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0
        return subprocess.Popen(self.command(args, fixture), cwd=self.work, env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=flags)

    def cli(self, *args, fixture=False, env=None, expect=0, error=None, stale_lock=False):
        process = self.spawn(args, fixture, env)
        out, err = process.communicate(timeout=300)
        context = f'{args}: rc={process.returncode} stderr={err!r}'
        self.assertEqual(process.returncode, expect, context)
        self.assertFalse(self.marker.exists(), 'a Python interpreter was invoked')
        self.assertEqual((self.store / 'lodge.lock').exists(), stale_lock, context)
        if expect:
            self.assertEqual(out, b'', context)
            message = json.loads(err)['error']
            if error is not None:
                self.assertIn(error, message, context)
            return message
        self.assertEqual(err, b'', context)
        return json.loads(out)

    def manifest(self):
        return json.loads((self.store / 'lodge.json').read_bytes())

    def journal(self, identity):
        return json.loads((self.store / 'sessions' / identity / 'journal.json').read_bytes())

    def assert_game_untouched(self):
        self.assertEqual(self.native_files(self.game), self.game_bytes)

    def trust(self, flag):
        return ['--engine', ENGINE, '--trusted-engine-sha256', ENGINE_SHA, flag]

    def hunt(self, *extra):
        return ['--area', 'areas:0', '--license', 'licenses:0', '--weapon', 'weapons:0', *extra]

    def interrupt(self, process):
        """Ctrl-C to the frontend itself (inside the sandbox, bwrap's child)."""
        if os.name == 'nt':
            return process.send_signal(signal.CTRL_BREAK_EVENT)
        target = process.pid
        if SANDBOX:
            children = Path(f'/proc/{process.pid}/task/{process.pid}/children').read_text().split()
            self.assertEqual(len(children), 1, children)
            target = int(children[0])
            self.assertTrue(Path(f'/proc/{target}/exe').resolve().name.startswith('c2-frontend-native'))
        os.kill(target, signal.SIGINT)

    def wait_state(self, identity, state, timeout=60):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if self.journal(identity)['state'] == state:
                    return self.journal(identity)
            except (OSError, ValueError):
                pass
            time.sleep(0.05)
        self.fail(f'session {identity} never reached {state}')

    def only_new_session(self, before):
        sessions = set(p.name for p in (self.store / 'sessions').iterdir()) - before
        self.assertEqual(len(sessions), 1)
        return sessions.pop()

    def sessions(self):
        root = self.store / 'sessions'
        return set(p.name for p in root.iterdir()) if root.exists() else set()

    @unittest.skipUnless(SANDBOX, 'sandbox negative control')
    def test_sandbox_contains_no_python(self):
        # The host interpreter exists outside, yet cannot be executed inside the
        # identical namespace the product runs in.
        host = Path(sys.executable).resolve()
        self.assertTrue(host.is_file())
        wrap = self.command([], False)[:-len([str(NATIVE), '--store', str(self.store), '--probe', str(PROBE)])]
        for interpreter in (str(host), '/usr/bin/python3', '/bin/sh'):
            done = subprocess.run([*wrap, interpreter, '-c', 'print(1)'], capture_output=True, timeout=30)
            self.assertNotEqual(done.returncode, 0, interpreter)
            self.assertIn(b'No such file', done.stderr, interpreter)
        done = subprocess.run([*wrap, str(NATIVE), '--version'], capture_output=True, timeout=30)
        self.assertEqual(done.returncode, 0, done.stderr)

    # The checkpoints are one continuing workflow over a single store.
    def test_workflow(self):
        self.checkpoint_1_setup()
        self.checkpoint_2_synthetic_and_legacy_native()
        self.checkpoint_1_upgrade_and_prepare()
        self.checkpoint_2_managed_failures()
        self.checkpoint_3_acceptance_and_continuation()
        self.assert_game_untouched()

    def checkpoint_1_setup(self):
        self.assertFalse(self.store.exists())
        self.assertEqual(self.cli('status')['schema_version'], 1)
        self.assertFalse(self.store.exists(), 'read-only status must not create the store')
        hunter = self.cli('hunter', 'create', 'Native hunter')
        self.hunter = hunter['id']
        second = self.cli('hunter', 'create', 'Second hunter')['id']
        self.assertEqual(self.cli('hunter', 'rename', second, 'Renamed')['name'], 'Renamed')
        self.cli('hunter', 'archive', second)
        self.cli('hunter', 'select', second, expect=2, error='archived hunters cannot be selected')
        self.cli('hunter', 'select', self.hunter)
        self.assertEqual(self.cli('hunter', 'list')['active_hunter'], self.hunter)
        self.cli('host-settings', '--json', '{"display": {"mode": 0}}')
        self.assertEqual(self.cli('host-settings'), {'display': {'mode': 0}})
        discovered = self.cli('expedition', 'discover', self.base)
        self.assertTrue(any(r['recognized'] for r in discovered))
        instance = self.cli('expedition', 'register', self.game, '--dialect', 'c2-classic')
        self.instance = instance['id']
        self.assertEqual(self.cli('expedition', 'register', self.game)['id'], self.instance)  # idempotent
        self.assertEqual([i['id'] for i in self.cli('expedition', 'list')], [self.instance])
        self.cli('expedition', 'refresh', self.instance)
        profiles = self.cli('profiles', self.instance)
        self.assertEqual([p['key'] for p in profiles], ['trophy00'])
        catalog = self.cli('catalog', '--instance', self.instance)
        self.assertEqual(catalog['areas'][0]['id'], 'areas:0')
        association = self.cli('associate', self.hunter, self.instance, 'trophy00', '--origin', 'personal', '--import-copy')
        self.association = association['id']
        snapshot = self.store / 'snapshots' / self.association
        self.assertEqual(self.native_files(snapshot), {k: v for k, v in self.game_bytes.items() if k.startswith('trophy00')})
        self.cli('refresh-state', self.association)
        dry = self.cli('launch-dry-run', self.association, *self.hunt())
        self.assertFalse(dry['process_launch_allowed'])
        self.assert_game_untouched()

    def run_synthetic(self, scenario, *extra, expect_state):
        before = self.sessions()
        prepared = self.cli('session', 'prepare-synthetic', self.association, '--area', 'areas:0', '--scenario', scenario, *extra)
        self.assertEqual(prepared['state'], 'prepared')
        self.assertEqual(prepared['schema_version'], 1)
        identity = self.only_new_session(before)
        result = self.cli('session', 'run', identity)
        self.assertEqual(result['state'], expect_state, (scenario, result['diagnostics']))
        self.assertEqual(result, self.cli('session', 'inspect', identity))
        return identity, result

    def checkpoint_2_synthetic_and_legacy_native(self):
        identity, result = self.run_synthetic('unchanged', expect_state='candidate')
        self.assertEqual(result['reconciliation']['changed_members'], [])
        self.assertEqual(self.cli('session', 'reconcile', identity), result)
        self.assertEqual(self.cli('session', 'recover', identity), result)
        _, result = self.run_synthetic('sav', expect_state='candidate')
        self.assertEqual(result['reconciliation']['changed_members'], ['trophy00.sav'])
        _, result = self.run_synthetic('hang', '--timeout', '0.5', expect_state='quarantined')
        self.assertEqual(result['process']['stop_reason'], 'timeout')
        _, result = self.run_synthetic('logs', expect_state='candidate')
        for name in ('stdout', 'stderr'):
            log = result['logs'][name]
            self.assertTrue(log['truncated'] and log['retained_bytes'] == 64 * 1024 and log['total_bytes'] > 200000, log)
        for scenario, code in (('extra', 'unexpected-state-member'), ('missing-sab', 'missing-state-member'),
                               ('corrupt-sav', 'unreadable-state'), ('registration', 'registration-mismatch'),
                               ('nonzero', 'process-exited'), ('terminated', 'unclean-process-return')):
            _, result = self.run_synthetic(scenario, expect_state='quarantined')
            self.assertIn(code, [d['code'] for d in result['diagnostics']], scenario)
        # Legacy (schema-1 import) native observer and hunt: candidates are never adoptable.
        before = self.sessions()
        self.cli('native-observer', 'prepare', self.association, '--area', 'areas:0',
                 *self.trust('--experimental-native-observer'), fixture=True)
        observer = self.only_new_session(before)
        returned = self.cli('native-observer', 'run', observer, *self.trust('--experimental-native-observer'),
                            fixture=True, env={'C2_NATIVE_FIXTURE_BEHAVIOR': 'changed'})
        self.assertEqual((returned['schema_version'], returned['state']), (2, 'candidate'))
        self.assertEqual(returned['reconciliation']['promotion'], 'deferred')
        # Unmodified production policy refuses the authored content.
        self.cli('native-observer', 'prepare', self.association, '--area', 'areas:0',
                 *self.trust('--experimental-native-observer'), expect=2)
        self.assert_game_untouched()

    def checkpoint_1_upgrade_and_prepare(self):
        upgraded = self.cli('managed-state', 'upgrade')
        self.assertEqual(upgraded['result'], 'upgraded')
        self.assertEqual(self.cli('managed-state', 'upgrade')['result'], 'already-upgraded')
        self.g0 = upgraded['associations'][self.association]
        history = self.cli('managed-state', 'inspect', self.association)
        self.assertEqual(history['current_generation'], self.g0)
        self.cli('session', 'prepare-synthetic', self.association, '--area', 'areas:0', expect=2,
                 error='legacy sessions cannot select an import in an upgraded store')
        self.cli('native-hunt', 'plan', self.association, *self.hunt(), expect=2)
        plan = self.cli('native-hunt', 'plan', self.association, *self.hunt(), fixture=True)
        self.assertEqual(plan['pins']['generation_id'], self.g0)

    def prepare_hunt(self, **kwargs):
        before = self.sessions()
        journal = self.cli('native-hunt', 'prepare', self.association, *self.hunt(),
                           *self.trust('--experimental-native-hunt'), fixture=True, **kwargs)
        identity = self.only_new_session(before)
        self.assertEqual((journal['schema_version'], journal['state'], journal['id']), (4, 'prepared', identity))
        return identity, journal

    def run_hunt(self, identity, behavior=None, expect=0):
        env = {'C2_NATIVE_FIXTURE_BEHAVIOR': behavior} if behavior else None
        return self.cli('native-hunt', 'run', identity, *self.trust('--experimental-native-hunt'),
                        fixture=True, env=env, expect=expect)

    def checkpoint_2_managed_failures(self):
        manifest = (self.store / 'lodge.json').read_bytes()
        identity, journal = self.prepare_hunt()
        root = self.store / 'sessions' / identity
        self.assertEqual(journal['pins']['generation_id'], self.g0)
        self.assertEqual(self.native_files(root / 'baseline'), self.native_files(self.store / 'snapshots' / self.association))
        self.assertEqual(self.native_files(root / 'work/state'), self.native_files(root / 'baseline'))
        # Nonzero return and corrupt returned state are quarantined, never candidates.
        self.assertEqual(self.run_hunt(identity, 'nonzero')['state'], 'quarantined')
        identity, _ = self.prepare_hunt()
        corrupt = self.run_hunt(identity, 'corrupt')
        self.assertEqual(corrupt['state'], 'quarantined')
        self.assertEqual(corrupt['capabilities']['returned_native_state_readable'], 'no')
        # A capability mismatch refuses preparation after trust, before any workspace.
        before = self.sessions()
        self.cli('native-hunt', 'prepare', self.association, *self.hunt(), *self.trust('--experimental-native-hunt'),
                 fixture=True, env={'C2_NATIVE_FIXTURE_BEHAVIOR': 'bad-contract'}, expect=2, error='unsupported engine')
        self.assertEqual(self.sessions(), before)
        self.cli('native-hunt', 'prepare', self.association, *self.hunt(), '--engine', ENGINE,
                 '--trusted-engine-sha256', '0' * 64, '--experimental-native-hunt', fixture=True, expect=2,
                 error='explicitly trusted hash')
        # Cancellation (Ctrl-C) stops only the owned child and quarantines.
        identity, _ = self.prepare_hunt()
        process = self.spawn(['native-hunt', 'run', identity, *self.trust('--experimental-native-hunt')],
                             fixture=True, env={'C2_NATIVE_FIXTURE_BEHAVIOR': 'hang'})
        running = self.wait_state(identity, 'running')
        self.interrupt(process)
        out, err = process.communicate(timeout=60)
        if REFERENCE and os.name == 'nt':
            # CPython has no handler for CTRL_BREAK (only CTRL_C raises KeyboardInterrupt), so
            # the reference dies with STATUS_CONTROL_C_EXIT like a killed supervisor. It cannot
            # represent cancellation here; the native run of this workflow asserts it instead.
            self.assertEqual(process.returncode, 0xC000013A, err)
            os.kill(running['process']['pid'], signal.SIGTERM)  # TerminateProcess the orphaned child
            (self.store / 'lodge.lock').unlink()
            self.assertEqual(self.cli('session', 'recover', identity)['state'], 'interrupted')
        else:
            self.assertEqual(process.returncode, 0, err)
            cancelled = json.loads(out)
            self.assertEqual((cancelled['state'], cancelled['process']['stop_reason']), ('quarantined', 'cancelled'))
        # Interrupted supervisor: the journal stays running; recovery never signals it.
        identity, _ = self.prepare_hunt()
        process = self.spawn(['native-hunt', 'run', identity, *self.trust('--experimental-native-hunt')],
                             fixture=True, env={'C2_NATIVE_FIXTURE_BEHAVIOR': 'hang'})
        running = self.wait_state(identity, 'running')
        process.kill()
        process.communicate(timeout=30)
        child = running['process']['pid']
        if os.name != 'nt':
            os.kill(child, signal.SIGKILL)  # harness cleanup of the orphaned test child
        lock = self.store / 'lodge.lock'
        self.assertTrue(lock.exists(), 'a killed supervisor leaves its writer lock for manual review')
        self.cli('status', stale_lock=True)  # reads stay available
        self.cli('refresh-state', self.association, expect=2, error='writer lock exists', stale_lock=True)
        lock.unlink()  # the documented manual recovery after verifying the owner is gone
        recovered = self.cli('session', 'recover', identity)
        self.assertEqual(recovered['state'], 'interrupted')
        self.assertIn('process-ownership-lost', [d['code'] for d in recovered['diagnostics']])
        self.cli('session', 'reconcile', identity, expect=2, error='only a durably returned session')
        self.cli('native-hunt', 'run', identity, *self.trust('--experimental-native-hunt'), fixture=True,
                 expect=2, error='only a prepared session can launch')
        # A changed engine binary fails preflight durably; nothing executes.
        identity, _ = self.prepare_hunt()
        copy = self.base / ('engine-copy' + EXE)
        shutil.copy2(ENGINE, copy)
        failed = self.cli('native-hunt', 'run', identity, '--engine', copy, '--trusted-engine-sha256', ENGINE_SHA,
                          '--experimental-native-hunt', fixture=True)
        self.assertEqual(failed['state'], 'failed')
        self.assertIn('preflight-failed', [d['code'] for d in failed['diagnostics']])
        self.assertEqual((self.store / 'lodge.json').read_bytes(), manifest, 'sessions never change authority')

    def accept(self, identity, predecessor):
        preview = self.cli('managed-state', 'preview', identity, '--expected-generation', predecessor, fixture=True)
        self.assertEqual((preview['status'], preview['allowed']), ('eligible', True), preview['diagnostics'])
        self.assertEqual(self.manifest()['associations'][self.association]['managed_state']['current_generation'],
                         predecessor, 'preview alone never accepts')
        accepted = self.cli('managed-state', 'accept', identity, '--expected-generation', predecessor,
                            '--candidate-sha256', preview['candidate_sha256'], fixture=True)
        self.assertEqual(accepted['result'], 'accepted')
        return accepted['current_generation'], preview['candidate_sha256']

    def generation_score(self, generation):
        history = self.manifest()['associations'][self.association]['managed_state']
        snapshot = self.store / history['generations'][generation]['snapshot']
        return score((snapshot / 'trophy00.sav').read_bytes())

    def checkpoint_3_acceptance_and_continuation(self):
        s1, _ = self.prepare_hunt()
        self.assertEqual(self.run_hunt(s1, 'changed')['state'], 'candidate')
        self.cli('managed-state', 'accept', s1, '--expected-generation', self.g0, '--candidate-sha256', '0' * 64,
                 fixture=True, expect=2, error='candidate differs from explicit preview')
        g1, d1 = self.accept(s1, self.g0)
        self.assertEqual((self.generation_score(self.g0), self.generation_score(g1)), (100, 107))
        s2, j2 = self.prepare_hunt()
        s3, _ = self.prepare_hunt()
        self.assertEqual(j2['pins']['generation_id'], g1)
        self.assertEqual(score((self.store / 'sessions' / s2 / 'baseline/trophy00.sav').read_bytes()), 107)
        self.assertEqual(self.run_hunt(s2, 'changed')['state'], 'candidate')
        self.assertEqual(self.run_hunt(s3, 'changed')['state'], 'candidate')  # equal-byte competitor
        g2, d2 = self.accept(s2, g1)
        self.assertEqual(self.generation_score(g2), 114)
        stale = self.cli('managed-state', 'preview', s3, '--expected-generation', g1, fixture=True)
        self.assertEqual(stale['status'], 'blocked')
        self.assertIn('stale', stale['diagnostics'][0]['message'])
        digest3 = stale['candidate_sha256']
        self.cli('managed-state', 'accept', s3, '--expected-generation', g1, '--candidate-sha256', digest3,
                 fixture=True, expect=2, error='stale')
        self.cli('managed-state', 'accept', s3, '--expected-generation', g2, '--candidate-sha256', digest3,
                 fixture=True, expect=2, error='stale')
        # Retrying the old G1 acceptance never rewinds the head.
        retry = self.cli('managed-state', 'accept', s1, '--expected-generation', self.g0, '--candidate-sha256', d1, fixture=True)
        self.assertEqual((retry['result'], retry['current_generation']), ('already-accepted', g2))
        self.assertEqual(self.cli('managed-state', 'preview', s1, '--expected-generation', self.g0, fixture=True)['status'],
                         'already-accepted')
        # A lost convenience receipt copy is completed without touching authority.
        receipt = self.store / 'sessions' / s2 / 'acceptance.json'
        content = receipt.read_bytes()
        receipt.unlink()
        manifest = (self.store / 'lodge.json').read_bytes()
        self.assertEqual(self.cli('managed-state', 'recover-acceptance', s2)['result'], 'already-accepted')
        self.assertEqual(receipt.read_bytes(), content)
        self.assertEqual((self.store / 'lodge.json').read_bytes(), manifest)
        self.assertEqual(self.cli('managed-state', 'recover-acceptance', s3)['result'], 'not-committed')
        # The next session starts from the accepted head G2 (score 114).
        s4, j4 = self.prepare_hunt()
        self.assertEqual(j4['pins']['generation_id'], g2)
        self.assertEqual(score((self.store / 'sessions' / s4 / 'baseline/trophy00.sav').read_bytes()), 114)
        history = self.cli('managed-state', 'inspect', self.association)
        self.assertEqual(history['current_generation'], g2)
        self.assertEqual(sorted(history['history']['receipts']), sorted([s1, s2]))
        # Invalid current authority never falls back to an older generation.
        snapshot = self.store / 'generations' / self.association / g2 / 'trophy00.sav'
        original = snapshot.read_bytes()
        snapshot.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        self.cli('managed-state', 'inspect', self.association, expect=2, error='no fallback')
        self.cli('native-hunt', 'prepare', self.association, *self.hunt(), *self.trust('--experimental-native-hunt'),
                 fixture=True, expect=2, error='no fallback')
        snapshot.write_bytes(original)
        self.assertEqual(self.cli('managed-state', 'inspect', self.association)['current_generation'], g2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
