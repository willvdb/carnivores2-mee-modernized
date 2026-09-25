"""Native refresh-state against lodge.profiles.refresh_association (disposable stores)."""
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
from lodge.discovery import register  # noqa: E402
from lodge.launch import prepare  # noqa: E402
from lodge.profiles import associate, refresh_association  # noqa: E402
from lodge.store import FrontendError, Store, hunter  # noqa: E402
from support import game  # noqa: E402
from test_profiles import room_bytes, save_bytes  # noqa: E402

DRIVER, PROBE, HELPER = sys.argv[1:4]
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


def native(directory, identity, probe=PROBE):
    environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
    done = subprocess.run([DRIVER, 'refresh-state', str(directory), identity, probe or '-'],
                          capture_output=True, timeout=60, env=environment, check=True)
    kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
    return kind, rest


class RefreshState(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        os.environ.pop('C2_PROFILE_PROBE', None)
        self.base = Path(self.temp.name).resolve()
        self.root = game(self.base)
        (self.root / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT)
        (self.root / 'trophy00.sav').write_bytes(save_bytes())
        (self.root / 'trophy00.sab').write_bytes(room_bytes())
        self.store = Store(self.base / 'Lodge')
        with self.store.transaction() as data:
            hunter_id = hunter(data, 'create', name='Hunter')['id']
            instance = register(data, self.root, dialect='c2-classic')
            self.referenced = associate(self.store, data, hunter_id, instance['id'], 'trophy00', 'personal',
                                        'referenced', PROBE)['id']

    def both(self, identity, probe=PROBE):
        """Run the reference on one copy of the store and native on another."""
        twin = self.base / 'Twin'
        shutil.rmtree(twin, ignore_errors=True)
        shutil.copytree(self.store.directory, twin)
        try:
            with self.store.transaction() as data:
                expected = ('ok', refresh_association(self.store, data, identity, probe))
        except FrontendError as error:
            expected = ('frontend', str(error))
        kind, rest = native(twin, identity, probe)
        if expected[0] == 'ok':
            self.assertEqual(kind, 'ok', rest)
            self.assertEqual(json.dumps(json.loads(rest)), json.dumps(expected[1]))
        else:
            self.assertEqual((kind, rest), expected)
        self.assertEqual((twin / 'lodge.json').read_bytes(), (self.store.directory / 'lodge.json').read_bytes())
        self.assertEqual(sorted(p.name for p in twin.iterdir()), sorted(p.name for p in self.store.directory.iterdir()))
        return expected

    def test_unchanged_changed_missing_and_helperless(self):
        self.assertEqual(self.both(self.referenced)[1]['status'], 'unchanged-state')
        self.assertEqual(self.both(self.referenced, None)[1]['status'], 'unchanged-state')
        (self.root / 'trophy00.sav').write_bytes(save_bytes()[:-1] + b'\x7f')
        result = self.both(self.referenced)[1]
        self.assertEqual((result['status'], result['diagnostics'][-1]['code']), ('changed-state', 'state-drift'))
        (self.root / 'trophy00.sav').unlink()
        (self.root / 'trophy00.sab').unlink()
        self.assertEqual(self.both(self.referenced)[1]['status'], 'missing-state')

    def test_managed_copy(self):
        with self.store.transaction() as data:
            hunter_id = next(iter(data['hunters']))
            instance_id = next(iter(data['instances']))
            managed = associate(self.store, data, hunter_id, instance_id, 'trophy00', 'personal', 'managed', PROBE)
        self.assertEqual(managed['authority'], 'independent-snapshot')
        self.assertEqual(self.store.read()['schema_version'], 1)
        self.assertEqual(self.both(managed['id'])[1]['status'], 'unchanged-state')
        # The managed copy is observed, not the installation.
        (self.root / 'trophy00.sav').write_bytes(save_bytes()[:-1] + b'\x7f')
        self.assertEqual(self.both(managed['id'])[1]['status'], 'unchanged-state')

    def launch(self, identity, area, licenses, weapons, mode='hunt', time_of_day=1):
        twin = self.base / 'Twin'
        shutil.rmtree(twin, ignore_errors=True)
        shutil.copytree(self.store.directory, twin)
        try:
            with self.store.transaction() as data:
                expected = prepare(self.store, data, identity, area, licenses, weapons, (), mode, time_of_day, PROBE)
        except FrontendError as error:
            expected = str(error)
        environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
        done = subprocess.run([DRIVER, 'launch-dry-run', str(twin), identity, PROBE, area, ','.join(licenses),
                               ','.join(weapons), mode, str(time_of_day)], capture_output=True, timeout=60,
                              env=environment, check=True)
        kind, _, rest = done.stdout.decode().partition(' ')
        if isinstance(expected, str):
            self.assertEqual((kind, rest.rstrip('\n')), ('frontend', expected))
        else:
            self.assertEqual(kind, 'ok', rest)
            actual = json.loads(rest)
            for volatile in ('id', 'created_at'):
                self.assertIsInstance(actual.pop(volatile), str)
                expected.pop(volatile)
            self.assertEqual(json.dumps(actual), json.dumps(expected))
        self.assertEqual((twin / 'lodge.json').read_bytes(), (self.store.directory / 'lodge.json').read_bytes())
        self.assertFalse((twin / 'lodge.lock').exists())
        return expected

    def test_launch_dry_run_matches_reference_and_writes_observation(self):
        result = self.launch(self.referenced, 'areas:0', ['licenses:0'], ['weapons:0'])
        self.assertEqual(result['candidate_argv'], ['reg=0', 'prj=huntdat/areas/area1', 'din=1', 'wep=1', 'dtm=1'])
        self.assertFalse(result['process_launch_allowed'])
        self.assertIn('last_observation', self.store.read()['associations'][self.referenced])
        self.assertEqual(self.launch(self.referenced, 'areas:99', [], [])['selection_status'], 'blocked')
        (self.root / 'trophy00.sav').write_bytes(save_bytes()[:-1] + b'\x7f')
        drifted = self.launch(self.referenced, 'areas:0', ['licenses:0'], ['weapons:0'])
        self.assertIn('native-state-review-required', [d['code'] for d in drifted['diagnostics']])
        self.assertEqual(drifted['candidate_argv'], [])
        self.launch('0a9b8c7d-1234-4abc-8def-001122334455', 'areas:0', [], [])

    def managed(self):
        with self.store.transaction() as data:
            a = associate(self.store, data, next(iter(data['hunters'])), next(iter(data['instances'])),
                          'trophy00', 'personal', 'managed', PROBE)
        return a['id']

    def history(self):
        # Reuse the established authored history/receipt corpus, with real
        # discovery and native bytes. Three distinct scores prove head choice.
        sys.path.insert(0, str(FRONTEND / 'tools'))
        from generate_schema_fixtures import base, A, I
        from lodge.managed_state import provenance
        from lodge.session_io import capture
        data = base(2, 2)
        instance = copy.deepcopy(next(iter(self.store.read()['instances'].values())))
        data['instances'][I] = {**instance, 'id': I}
        a = data['associations'][A]
        a.update(state_key='trophy00', revision=instance['revision'], future={'values': [None, True, 1, 1.0]})
        for index, g in enumerate(a['managed_state']['generations'].values()):
            target = self.store.directory / g['snapshot']; target.mkdir(parents=True)
            content = bytearray(save_bytes()); content[132:136] = (1000 + index).to_bytes(4, 'little', signed=True)
            (target / 'trophy00.sav').write_bytes(content)
            (target / 'trophy00.sab').write_bytes(room_bytes())
            g['members'], _ = capture(target)
            g['provenance'] = provenance(a)
            if index == 0:
                a['files'] = [{**{k: f[k] for k in ('path', 'size', 'sha256')}, 'kind': f['path'][-3:]}
                              for f in g['members']]
            else:
                g['revision'] = instance['revision']
                a['managed_state']['receipts'][g['source_session']].update(members=g['members'], revision=instance['revision'])
        with self.store.transaction() as original:
            original.clear(); original.update(data)
        self.assertEqual(a['authority'], 'managed-state-history')
        return A, a

    def pins(self, identity, selection=None, probe=PROBE, managed=False, generation=None, expected_codec=None, mode='observer', env=None):
        from lodge.sessions import snapshot_pins
        from lodge.session_io import capture
        chosen = selection if selection is not None else {
            'area': 'areas:0', 'mode': mode, 'time_of_day': 1, 'licenses': [], 'weapons': [], 'equipment': []}
        before = {str(p): p.read_bytes() for p in self.base.rglob('*') if p.is_file()}
        try:
            pins, blobs = snapshot_pins(self.store, identity, chosen, probe, expected_codec,
                                       mode=mode, managed=managed, generation=generation)
            expected = ('ok', {'pins': pins, 'blobs': {k: v.hex() for k, v in blobs.items()}})
        except FrontendError as error:
            expected = ('frontend', str(error))
        except OSError:
            expected = ('oserror', None)
        args = {'selection': chosen}
        if expected_codec is not None: args['expected_codec'] = expected_codec
        environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
        environment.update(env or {})
        done = subprocess.run([DRIVER, 'snapshot', str(self.store.directory), identity, probe or '-', mode,
                               'managed' if managed else 'legacy', generation or '-'], input=json.dumps(args).encode(),
                              capture_output=True, timeout=60, env=environment, check=True)
        kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
        self.assertEqual(kind, expected[0], rest)
        if kind == 'ok':
            self.assertEqual(json.dumps(json.loads(rest)), json.dumps(expected[1]))
        elif kind != 'oserror':
            self.assertEqual(rest, expected[1])
        # Includes original native files, every generation, and manifest bytes.
        for path, content in before.items(): self.assertEqual(Path(path).read_bytes(), content, path)
        self.assertFalse((self.store.directory / 'sessions').exists())
        self.assertFalse((self.store.directory / 'lodge.lock').exists())
        return expected

    def test_snapshot_legacy_success_and_ownership_refusal(self):
        identity = self.managed()
        result = self.pins(identity)[1]
        self.assertNotIn('generation', result['pins'])
        self.assertEqual(set(result['blobs']), {'trophy00.sav', 'trophy00.sab'})
        self.assertEqual(self.pins(self.referenced)[0], 'frontend')
        self.assertEqual(self.pins('00000000-0000-0000-0000-000000000000')[1], 'unknown association ID')
        self.assertEqual(self.pins(identity, {'area': 'areas:0'})[1], 'session policy permits observer with no loadout only')
        for time in (True, 1.0, -1, 3, '1'):
            chosen = {'area':'areas:0', 'mode':'observer', 'time_of_day':time, 'licenses':[], 'weapons':[], 'equipment':[]}
            self.assertEqual(self.pins(identity, chosen)[0], 'frontend')

    def test_snapshot_current_head_and_no_fallback(self):
        identity, a = self.history()
        head = a['managed_state']['current_generation']
        result = self.pins(identity, managed=True, mode='hunt')[1]
        self.assertEqual(result['pins']['generation_id'], head)
        self.assertTrue(result['pins']['source_root'].endswith(head))
        self.assertNotIn('managed_state', result['pins']['association'])
        self.assertEqual(result['pins']['source_observation']['trophy00.sav']['score'], 1002)
        self.assertEqual(self.both(identity)[1]['status'], 'unchanged-state')
        self.launch(identity, 'areas:0', ['licenses:0'], ['weapons:0'])
        root = self.store.directory / a['managed_state']['generations'][head]['snapshot']
        (root / 'trophy00.sav').write_bytes(b'corrupt')
        self.assertIn('no fallback', self.pins(identity, managed=True, mode='hunt')[1])
        self.assertIn('no fallback', self.both(identity)[1])
        shutil.rmtree(root)
        self.assertEqual(self.pins(identity, managed=True, mode='hunt')[1], 'state directory missing')
        self.assertEqual(self.both(identity)[1], 'state directory missing')

    def test_snapshot_explicit_history_and_legacy_upgrade_refusal(self):
        identity, a = self.history()
        g0 = next(iter(a['managed_state']['generations']))
        self.assertEqual(self.pins(identity, managed=True, generation=g0, mode='hunt')[1]['pins']['generation_id'], g0)
        self.assertIn('legacy sessions cannot select', self.pins(identity)[1])
        self.assertEqual(self.pins(identity, managed=True, generation='bad', mode='hunt')[1], 'unknown managed-state generation')

    def test_codec_pin_refused_before_invocation(self):
        from lodge.sessions import codec_evidence
        identity = self.managed()
        marker = self.base / 'codec-invocations'
        evidence = codec_evidence(HELPER)
        env = {'C2_TEST_PROBE_MARKER': str(marker)}
        mismatched = {**evidence, 'sha256': '0' * 64}
        self.assertIn('helper not executed', self.pins(identity, probe=HELPER, expected_codec=mismatched, env=env)[1])
        self.assertFalse(marker.exists())
        self.assertEqual(self.pins(identity, probe=str(self.base / 'absent'), expected_codec=evidence, env=env)[0], 'oserror')
        self.assertFalse(marker.exists())
        self.assertEqual(self.pins(identity, probe=HELPER, expected_codec=evidence, env=env)[0], 'ok')
        self.assertEqual(marker.read_text().splitlines(), ['invoked', 'invoked'])

    def test_snapshot_refusal_order_content_and_state(self):
        identity = self.managed()
        source = self.store.directory / 'snapshots' / identity
        (source / 'trophy00.sav').write_bytes(b'changed')
        self.assertEqual(self.pins(identity, probe=str(self.base / 'absent'))[1], 'managed source membership/bytes differ or require review')
        (self.root / 'HUNTDAT/_MENU.TXT').write_text(SCRIPT + '\n// drift')
        self.assertEqual(self.pins(identity)[1], 'session requires exact unchanged content revision')
        shutil.rmtree(self.root)
        self.assertEqual(self.pins(identity)[1], 'session requires exact unchanged content revision')

    def test_snapshot_opaque_native_bytes_refused_without_change(self):
        (self.root / 'trophy00.sav').write_bytes(b'opaque')
        identity = self.managed()
        self.assertEqual(self.pins(identity)[1], 'managed source is unreadable or has a registration mismatch')

    def test_unknown_association_writes_nothing(self):
        before = (self.store.directory / 'lodge.json').read_bytes()
        self.assertEqual(self.both('0a9b8c7d-1234-4abc-8def-001122334455'), ('frontend', 'unknown association ID'))
        self.assertEqual((self.store.directory / 'lodge.json').read_bytes(), before)
        self.assertFalse((self.store.directory / 'lodge.lock').exists())


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
