"""Native CLI parity with the unchanged frontend.py over twin disposable stores.

Each step runs the reference CLI on store A and c2-frontend-native on an exact
copy B, then compares exit status, stdout, the error envelope and every file
in both stores. Only generated UUIDs, timestamps, the store location and
explicitly registered per-side digests are normalized; everything else,
including native save bytes, must be identical.
"""
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

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
from lodge.discovery import register  # noqa: E402
from lodge.profiles import associate  # noqa: E402
from lodge.session_io import encode  # noqa: E402
from lodge.store import Store, hunter  # noqa: E402
from support import game  # noqa: E402
from test_launch import SCRIPT  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

NATIVE, FIXTURE, PROBE, ENGINE, CHILD = sys.argv[1:6]
del sys.argv[1:6]
REFERENCE = Path(__file__).with_name('reference_cli.py')
UUID = re.compile(r'[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}')
TOKEN = re.compile(r'<ID\d+>')
TIME = re.compile(r'\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,6})?\+00:00')


class Side:
    def __init__(self, base, native):
        self.base, self.native = Path(base), native
        self.store = self.base / 'Lodge'
        self.ids, self.tokens = {}, {}

    def command(self, args, fixture):
        if self.native:
            return [FIXTURE if fixture else NATIVE, '--store', str(self.store), '--probe', PROBE, *args]
        return [sys.executable, str(REFERENCE), *(['--fixture-policy'] if fixture else []),
                '--store', str(self.store), '--probe', PROBE, *args]

    def run(self, args, fixture=False, env=None):
        environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
        environment.update(env or {})
        done = subprocess.run(self.command(args, fixture), capture_output=True, timeout=300, env=environment)
        return done.returncode, done.stdout.decode('utf-8', 'surrogateescape'), done.stderr.decode('utf-8', 'surrogateescape')

    def normalize(self, text):
        for spelling in {str(self.base), json.dumps(str(self.base))[1:-1]}:
            text = text.replace(spelling, '<BASE>')
        for token, name in self.tokens.items():
            text = text.replace(token, name)
        text = UUID.sub(lambda m: self.ids.setdefault(m.group(), f'<ID{len(self.ids)}>'), text)
        return TIME.sub('<TIME>', text)

    def translate(self, args, reference):
        """Spell normalized <IDn> tokens and the reference side's generated identities as this side's own."""
        inverse = {token: raw for raw, token in self.ids.items()}
        def own(text):
            text = TOKEN.sub(lambda m: inverse.get(m.group(), m.group()), text)
            if self is reference:
                return text
            return UUID.sub(lambda m: inverse.get(reference.ids.get(m.group()), m.group()), text)
        return [own(a) for a in args]

    def document(self, raw):
        """Normalized text. The sorted encoding (JournalEvidenceV1) orders id-keyed
        tables by fresh UUIDs, and a schema-2 manifest keeps that order through
        later transactions, so those documents are checked byte for byte against
        their own canonical encoder and their normalized content compared
        key-sorted; every other document is compared as normalized text."""
        text = self.normalize(raw.decode('utf-8', 'surrogateescape'))
        try:
            value = json.loads(raw)
        except ValueError:
            return text
        transaction = (json.dumps(value, indent=2, ensure_ascii=True) + '\n').encode()
        upgraded = isinstance(value, dict) and value.get('schema_version') == 2 and 'state_upgrade' in value
        if raw == encode(value) or (upgraded and raw == transaction):
            return ('canonical-encoding', json.dumps(json.loads(text), sort_keys=True))
        return text

    def tree(self):
        result = {}
        if not self.store.exists():
            return result
        for path in sorted(self.store.rglob('*')):
            name = self.normalize(path.relative_to(self.store).as_posix())
            if path.is_dir():
                result[name] = 'directory'
            elif path.name.endswith(('.json', '.log', '.json.bak')):
                result[name] = self.document(path.read_bytes())
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

    def step(self, *args, fixture=False, expect=None, env=None):
        args = self.fill(list(args))
        results = []
        for side in (self.reference, self.candidate):
            code, out, err = side.run(side.translate(args, self.reference), fixture, env)
            results.append((code, side.normalize(out), side.normalize(error_message(err)) if code else '', side))
        (rcode, rout, rerr, _), (ncode, nout, nerr, _) = results
        context = f'{args}: reference rc={rcode} err={rerr!r}; native rc={ncode} err={nerr!r}'
        self.assertEqual(ncode, rcode, context)
        self.assertEqual(nout, rout, context)
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

    def test_store_operations(self):
        """Workstream B commands: identity, settings, registration, association, upgrade, recovery."""
        created = self.step('hunter', 'create', 'Second hunter', expect=0)
        second = created['id']
        self.step('hunter', 'select', '{hunter}', expect=0)
        self.step('hunter', 'rename', second, 'Renamed hunter', expect=0)
        self.step('hunter', 'archive', second, expect=0)
        self.step('hunter', 'select', second, expect=2)
        self.step('hunter', 'rename', '{hunter}', ' ', expect=2)
        self.step('hunter', 'archive', 'no-such-hunter', expect=2)
        self.step('host-settings', '--json', '{"display": {"width": 1024, "vsync": true}, "audio": {"volume": 0.5}}', expect=0)
        self.step('host-settings', '--json', '{"input": {"invert": false}, "display": {"width": 1280}}', expect=0)
        self.step('host-settings', '--json', '{"video": {}}', expect=2)
        self.step('host-settings', '--json', '["display"]', expect=2)
        self.step('host-settings', expect=0)
        base = self.game.parent
        expeditions = base / 'Expeditions'
        first, nested = game(expeditions, 'First'), game(expeditions / 'Nested', 'Second')
        (nested / 'HUNTDAT/AREAS/AREA2.MAP').write_bytes(b'distinct content revision')
        (expeditions / 'Partial/HUNTDAT').mkdir(parents=True)
        registered = self.step('expedition', 'register', str(first), '--dialect', 'mee-newer', '--family', 'Carnivores 2',
                               '--release', '1.04', expect=0)
        self.step('expedition', 'register', str(first), expect=0)
        self.step('expedition', 'register', str(expeditions / 'Partial'), expect=2)
        self.step('expedition', 'register', str(first), '--dialect', 'bogus', expect=2)
        self.step('expedition', 'register', 'C:\\Games\\Foreign', expect=2)
        self.step('expedition', 'discover', str(expeditions), expect=0)
        managed = self.step('expedition', 'discover', str(expeditions), '--register-managed', expect=0)
        self.assertEqual([m['path'] for m in managed], [str(first), str(nested)])
        self.step('expedition', 'discover', str(expeditions), '--register-managed', expect=0)
        self.step('expedition', 'discover', str(base / 'absent'), expect=0)
        self.step('expedition', 'refresh', registered['id'], expect=0)
        (nested / 'CARN2.EXE').write_bytes(b'patched launcher bytes')
        self.step('expedition', 'refresh', managed[1]['id'], expect=0)
        self.step('expedition', 'refresh', 'no-such-instance', expect=2)
        moved = expeditions / 'Moved'
        self.step('expedition', 'relocate', managed[1]['id'], str(moved), expect=2)
        nested.rename(moved)
        self.step('expedition', 'relocate', managed[1]['id'], str(moved), expect=0)
        self.step('expedition', 'discover', str(expeditions), expect=0)
        self.step('expedition', 'relocate', managed[1]['id'], str(moved), expect=2)
        outside = base / 'Expeditions-external'
        moved.rename(outside)
        self.step('expedition', 'relocate', managed[1]['id'], str(outside), expect=0)
        self.step('expedition', 'relocate', 'no-such-instance', str(outside), expect=2)
        (self.game / 'trophy01.sav').write_bytes(save_bytes(slot=1))
        (self.game / 'trophy02.sav').write_bytes(save_bytes(slot=2))
        (self.game / 'trophy02.txt').write_bytes(b'companion')
        (self.game / 'trophy05.sav').write_bytes(save_bytes(slot=6))
        # Authored fixture state, not a step: refresh the untouched-game baseline.
        self.native_game = {p.name: p.read_bytes() for p in self.game.iterdir() if p.is_file()}
        self.step('associate', '{hunter}', '{instance}', 'trophy01', '--origin', 'personal', expect=0)
        self.step('associate', '{hunter}', '{instance}', 'trophy01', '--origin', 'personal', expect=2)
        self.step('associate', '{hunter}', '{instance}', 'trophy01', '--origin', 'bundled-example', '--import-copy', expect=0)
        self.step('associate', '{hunter}', '{instance}', 'trophy03', '--origin', 'unknown', '--ownership', 'managed', expect=0)
        self.step('associate', '{hunter}', '{instance}', 'trophy02', '--origin', 'personal', '--import-copy', expect=2)
        self.step('associate', '{hunter}', '{instance}', 'trophy02', '--origin', 'personal', '--ownership', 'referenced', expect=0)
        self.step('associate', '{hunter}', '{instance}', 'trophy05', '--origin', 'personal', expect=2)
        self.step('associate', '{hunter}', '{instance}', 'trophy09', '--origin', 'personal', expect=2)
        self.step('associate', second, '{instance}', 'trophy01', '--origin', 'personal', expect=2)
        self.step('associate', '{hunter}', '{instance}', 'trophy01', '--origin', 'personal', '--ownership', 'referenced',
                  '--import-copy', expect=2)
        self.step('recover-backup', expect=0)
        self.step('status', expect=0)
        # state_upgrade.backup_sha256 digests each side's own pre-upgrade bytes.
        for side in (self.reference, self.candidate):
            side.tokens[hashlib.sha256((side.store / 'lodge.json').read_bytes()).hexdigest()] = '<SCHEMA1-BACKUP-SHA256>'
        self.step('managed-state', 'upgrade', expect=0)
        self.step('managed-state', 'upgrade', expect=0)
        self.step('managed-state', 'inspect', '{managed}', expect=0)
        self.step('associate', '{hunter}', '{instance}', 'trophy03', '--origin', 'personal', '--import-copy', expect=0)
        self.step('recover-backup', expect=2)
        self.step('hunter', 'create', 'After upgrade', expect=0)
        self.step('host-settings', expect=0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
