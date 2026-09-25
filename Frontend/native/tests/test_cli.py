"""Native CLI parity with the unchanged frontend.py over twin disposable stores.

Each step runs the reference CLI on store A and c2-frontend-native on an exact
copy B, then compares exit status, stdout, the error envelope and every file
in both stores. Only generated UUIDs, timestamps, the store location and
explicitly registered per-side digests are normalized; everything else,
including native save bytes, must be identical.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
from lodge.discovery import register  # noqa: E402
from lodge.profiles import associate  # noqa: E402
from lodge.store import Store, hunter  # noqa: E402
from support import game  # noqa: E402
from test_launch import SCRIPT  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

NATIVE, FIXTURE, PROBE, ENGINE, CHILD = sys.argv[1:6]
del sys.argv[1:6]
REFERENCE = Path(__file__).with_name('reference_cli.py')
UUID = re.compile(r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}')
TIME = re.compile(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,6})?\+00:00')


class Side:
    def __init__(self, base, native):
        self.base, self.native = Path(base), native
        self.store = self.base / 'Lodge'
        self.ids, self.tokens = {}, {}

    def command(self, args, fixture, probe=True):
        common = ['--store', str(self.store), *(['--probe', PROBE] if probe else []), *args]
        if self.native:
            return [FIXTURE if fixture else NATIVE, *common]
        return [sys.executable, str(REFERENCE), *(['--fixture-policy'] if fixture else []), *common]

    def run(self, args, fixture=False, env=None, probe=True):
        environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
        environment.update(env or {})
        done = subprocess.run(self.command(args, fixture, probe), capture_output=True, timeout=300, env=environment)
        out, err = (s.decode('utf-8', 'surrogateescape') for s in (done.stdout, done.stderr))
        if os.name == 'nt' and not self.native:
            # Documented difference: frontend.py's text-mode print emits CRLF on
            # Windows; the native CLI writes LF on every host (binary stdout).
            out, err = out.replace('\r\n', '\n'), err.replace('\r\n', '\n')
        return done.returncode, out, err

    def normalize(self, text):
        for spelling in {str(self.base), json.dumps(str(self.base))[1:-1]}:
            text = text.replace(spelling, '<BASE>')
        for token, name in self.tokens.items():
            text = text.replace(token, name)
        text = UUID.sub(lambda m: self.ids.setdefault(m.group(), f'<ID{len(self.ids)}>'), text)
        return TIME.sub('<TIME>', text)

    def tree(self):
        result = {}
        if not self.store.exists():
            return result
        for path in sorted(self.store.rglob('*')):
            name = self.normalize(path.relative_to(self.store).as_posix())
            if path.is_dir():
                result[name] = 'directory'
            elif path.suffix in ('.json', '.log'):
                result[name] = self.normalize(path.read_bytes().decode('utf-8', 'surrogateescape'))
            else:
                result[name] = path.read_bytes()
        return result


def error_message(stderr):
    """The domain error text: frontend.py JSON envelope or its argparse usage line."""
    last = stderr.strip().splitlines()[-1] if stderr.strip() else ''
    try:
        return json.loads(last)['error']
    except (ValueError, KeyError, TypeError):
        return last.split('error: ', 1)[-1]


class Parity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        base = Path(self.temp.name).resolve()
        self.game = game(base)
        (self.game / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (self.game / 'trophy00.sav').write_bytes(save_bytes())
        (self.game / 'trophy00.sab').write_bytes(room_bytes())
        (self.game / 'trophy03.sav').write_bytes(save_bytes(slot=3, score=250))
        self.native_game = {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()}
        self.reference, self.candidate = Side(base / 'A', False), Side(base / 'B', True)
        self.populate(Store(self.reference.store))
        shutil.copytree(self.reference.store, self.candidate.store)

    def populate(self, store):
        with store.transaction() as data:
            self.hunter = hunter(data, 'create', name='Parity hunter')['id']
            self.instance = register(data, self.game, dialect='c2-classic')['id']
            self.referenced = associate(store, data, self.hunter, self.instance, 'trophy00', 'personal',
                                        'referenced', PROBE)['id']
            self.managed = associate(store, data, self.hunter, self.instance, 'trophy00', 'personal',
                                     'managed', PROBE)['id']

    def fill(self, args):
        names = {'{hunter}': self.hunter, '{instance}': self.instance, '{referenced}': self.referenced,
                 '{managed}': self.managed, '{game}': str(self.game)}
        return [names.get(a, a) for a in args]

    def step(self, *args, fixture=False, expect=None, env=None, probe=True):
        args = self.fill(list(args))
        results = []
        for side in (self.reference, self.candidate):
            code, out, err = side.run(args, fixture, env, probe)
            results.append((code, side.normalize(out), side.normalize(error_message(err)) if code else '', side))
        (rcode, rout, rerr, _), (ncode, nout, nerr, _) = results
        context = f'{args}: reference rc={rcode} err={rerr!r}; native rc={ncode} err={nerr!r}'
        self.assertEqual(ncode, rcode, context)
        self.assertEqual(nout, rout, context)
        if re.match(r'\[Errno \d+\] ', rerr) or re.match(r'\[WinError \d+\] ', rerr):
            # Documented difference: OSError text is the native filesystem_error
            # wording; the outcome (status, output, bytes) is still compared.
            self.assertTrue(nerr, context)
        else:
            self.assertEqual(nerr, rerr, context)
        if expect is not None:
            self.assertEqual(rcode, expect, context)
        self.assertEqual(self.candidate.tree(), self.reference.tree(), context)
        self.assertFalse((self.candidate.store / 'lodge.lock').exists())
        self.assertEqual({p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()}, self.native_game)
        return json.loads(rout) if rcode == 0 and rout else None

    def test_read_views_and_observations(self):
        for args in (['status'], ['hunter', 'list'], ['expedition', 'list'], ['host-settings'],
                     ['profiles', '{instance}'], ['catalog', '--instance', '{instance}'],
                     ['catalog', '--path', '{game}']):
            self.step(*args, expect=0)
        self.step('profiles', 'no-such-instance', expect=2)
        self.step('catalog', '--instance', 'no-such-instance', expect=2)
        self.step('managed-state', 'inspect', '{managed}', expect=2)

    def test_observation_writes(self):
        self.step('refresh-state', '{referenced}', expect=0)
        self.step('refresh-state', '{managed}', expect=0)
        self.step('refresh-state', 'no-such-association', expect=2)
        self.step('simulate-return', '{managed}', '--exit-code', '3', expect=0)
        self.step('simulate-return', '{referenced}', expect=0)
        self.step('simulate-return', 'no-such-association', expect=2)

    def test_planning_commands(self):
        self.step('launch-dry-run', '{referenced}', '--area', 'areas:0', '--license', 'licenses:0',
                  '--weapon', 'weapons:0', expect=0)
        self.step('launch-dry-run', '{managed}', '--area', 'areas:0', '--mode', 'observer', '--time', '2', expect=0)
        self.step('launch-dry-run', '{managed}', '--area', 'areas:9', '--equipment', 'equipment:0', expect=0)
        # Unmodified production Genesis policies refuse authored content.
        self.step('genesis-observer-plan', '{managed}', '--area', 'areas:0', expect=2)
        self.step('native-hunt', 'plan', '{managed}', '--area', 'areas:0', '--license', 'licenses:0',
                  '--weapon', 'weapons:0', expect=2)
        # The labelled asset-free policy double (test-only executable) succeeds.
        self.step('genesis-observer-plan', '{managed}', '--area', 'areas:0', '--engine', ENGINE,
                  fixture=True, expect=0)
        self.step('native-hunt', 'plan', '{managed}', '--area', 'areas:0', '--license', 'licenses:0',
                  '--weapon', 'weapons:0', '--time', '0', fixture=True, expect=0)
        self.step('native-hunt', 'plan', '{referenced}', '--area', 'areas:0', '--license', 'licenses:0',
                  '--weapon', 'weapons:0', fixture=True, expect=2)

    def test_usage_errors_touch_nothing(self):
        for args in (['hunter', 'bogus'], ['launch-dry-run', '{managed}', '--time', '7', '--area', 'a'],
                     ['session', 'prepare-synthetic', '{managed}', '--area', 'a', '--timeout', 'x'],
                     ['associate', 'a', 'b', 'c', '--origin', 'nobody'], ['catalog'],
                     ['native-hunt', 'plan', 'x', '--area', 'a'], ['status', 'extra']):
            self.step(*args, expect=2)


    def test_parsing_edge_cases(self):
        # Every occurrence of a repeated option is converted and checked.
        self.step('native-hunt', 'plan', '{managed}', '--area', 'areas:0', '--license', 'licenses:0',
                  '--weapon', 'weapons:0', '--time', '3', '--time', '1', fixture=True, expect=2)
        self.step('launch-dry-run', '{managed}', '--area', 'a', '--mode', 'bogus', '--mode', 'hunt', expect=2)
        self.step('simulate-return', '{managed}', '--exit-code', 'q', '--exit-code', '1', expect=2)
        # type=Path turns '' into '.', never an absent value.
        env = {'C2_PROFILE_PROBE': PROBE}
        self.step('--probe', '', 'refresh-state', '{managed}', probe=False, env=env, expect=2)
        self.step('--probe', '', 'profiles', '{instance}', probe=False, env=env, expect=2)
        self.step('genesis-observer-plan', '{managed}', '--area', 'areas:0', '--engine', '', fixture=True, expect=2)
        # The environment helper applies only without --probe.
        self.step('refresh-state', '{managed}', probe=False, env=env, expect=0)
        # frontend.py has no --version; an unconsumed '--' is unrecognized.
        self.step('--version', 'status', expect=2)
        for args in (['status', '--'], ['hunter', 'list', '--'], ['host-settings', '--json', '{}', '--']):
            self.step(*args, expect=2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
