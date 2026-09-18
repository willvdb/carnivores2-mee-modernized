import hashlib
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from lodge.discovery import register
from lodge.profiles import (associate, codec_inspect, inspect_set, inventory,
                            refresh_association, stable_read)
from lodge.store import FrontendError, Store, hunter
from support import game


def save_bytes(slot=0, score=100):
    # Original synthetic bytes, including noncanonical options and opaque words.
    result = bytearray((i * 73 + 19) % 256 for i in range(1660))
    result[:128] = b'Test hunter\0' + bytes(range(116))
    struct.pack_into('<iii', result, 128, slot, score, 1000)
    return bytes(result)


def room_bytes():
    return bytes((i * 37) % 256 for i in range(7176))


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = game(self.temp.name)
        self.store = Store(Path(self.temp.name) / 'Frontend state')
        with self.store.transaction() as data:
            self.hunter = hunter(data, 'create', name='Universal hunter')['id']
            self.instance = register(data, self.root)['id']
        (self.root / 'trophy00.sav').write_bytes(save_bytes())
        (self.root / 'trophy00.sab').write_bytes(room_bytes())

    def test_paired_orphan_packaged_and_outlier_inventory(self):
        (self.root / 'trophy01.sab').write_bytes(room_bytes())
        (self.root / 'MODDAT').mkdir()
        (self.root / 'MODDAT/trophy00.sav').write_bytes(save_bytes())
        (self.root / 'trophy10.sav').write_bytes(save_bytes(10))
        states = {s['key']: s for s in inventory(self.root)}
        self.assertEqual(len(states['trophy00']['files']), 2)
        self.assertIn('orphan-companion', [d['code'] for d in states['trophy01']['diagnostics']])
        self.assertIn('non-root-state', [d['code'] for d in states['MODDAT/trophy00']['diagnostics']])
        self.assertIn('slot-outside-menu', [d['code'] for d in states['trophy10']['diagnostics']])
        self.assertTrue(all(s['ownership'] == 'unclaimed' for s in states.values()))

    @unittest.skipUnless(os.environ.get('C2_PROFILE_PROBE'), 'build codec helper and set C2_PROFILE_PROBE')
    def test_codec_lossless_inspection_mismatch_and_opaque(self):
        state = inventory(self.root)[0]
        before = {f['path']: (self.root / f['path']).read_bytes() for f in state['files']}
        result = inspect_set(self.root, state)
        save = next(f['decoded'] for f in result['files'] if f['kind'] == 'sav')
        self.assertEqual(save['score'], 100)
        self.assertEqual(save['rank'], 1000)
        self.assertEqual(len(save['items_raw']), 24)
        self.assertEqual(bytes.fromhex(save['name_hex']), before['trophy00.sav'][:128])
        for name, content in before.items():
            self.assertEqual((self.root / name).read_bytes(), content)
        (self.root / 'trophy00.sav').write_bytes(save_bytes(3))
        result = inspect_set(self.root, inventory(self.root)[0])
        self.assertIn('registration-mismatch', [d['code'] for d in result['diagnostics']])
        with self.assertRaises(FrontendError):
            with self.store.transaction() as data:
                associate(self.store, data, self.hunter, self.instance, 'trophy00', 'personal')
        self.assertEqual(codec_inspect(bytes(1664), 'sav')['layout'], 'unknown')
        self.assertEqual(codec_inspect(save_bytes(), 'sav', dialect='iceage-triassic')['layout'], 'unsupported-iceage-family')
        for size in (0, 139, 1515, 1659, 1661):
            self.assertEqual(codec_inspect(bytes(size), 'sav')['layout'], 'unknown')

    def test_managed_fork_and_referenced_authority(self):
        with self.store.transaction() as data:
            ref = associate(self.store, data, self.hunter, self.instance, 'trophy00', 'personal')
        self.assertEqual(ref['ownership'], 'referenced')
        with self.assertRaises(FrontendError):
            with self.store.transaction() as data:
                associate(self.store, data, self.hunter, self.instance, 'trophy00', 'personal')
        with self.store.transaction() as data:
            copy = associate(self.store, data, self.hunter, self.instance, 'trophy00', 'bundled-example', 'managed')
        self.assertNotEqual(ref['id'], copy['id'])
        self.assertFalse(copy['writable'])
        for entry in copy['files']:
            native = (self.root / entry['path']).read_bytes()
            self.assertEqual((self.store.directory / copy['snapshot'] / entry['path']).read_bytes(), native)
        (self.root / 'trophy00.sab').write_bytes(bytes(7176))
        with self.store.transaction() as data:
            self.assertEqual(refresh_association(self.store, data, ref['id'])['status'], 'changed-state')
            self.assertEqual(refresh_association(self.store, data, copy['id'])['status'], 'unchanged-state')
        # Refresh is observational: initial provenance remains intact.
        self.assertEqual(self.store.read()['associations'][ref['id']]['files'], ref['files'])

    def test_managed_install_default_and_missing_companion(self):
        with self.store.transaction() as data:
            data['instances'][self.instance]['mode'] = 'managed'
            (self.root / 'trophy00.sab').unlink()
            result = associate(self.store, data, self.hunter, self.instance, 'trophy00', 'unknown')
        self.assertEqual(result['ownership'], 'managed')
        self.assertEqual(len(result['files']), 1)
        self.assertFalse((self.root / 'trophy00.sab').exists())

    def test_unstable_pair_rejected(self):
        state = inventory(self.root)[0]
        with patch('lodge.profiles.read_set', side_effect=[{'trophy00.sav': b'a', 'trophy00.sab': b'b'}, {'trophy00.sav': b'a', 'trophy00.sab': b'c'}]):
            with self.assertRaises(FrontendError):
                stable_read(self.root, state)

    def test_orphan_refused_and_manifest_cannot_claim_writable_state(self):
        (self.root / 'trophy00.sav').unlink()
        with self.assertRaises(FrontendError):
            with self.store.transaction() as data:
                associate(self.store, data, self.hunter, self.instance, 'trophy00', 'personal')
        (self.root / 'trophy00.sav').write_bytes(save_bytes())
        with self.store.transaction() as data:
            result = associate(self.store, data, self.hunter, self.instance, 'trophy00', 'personal')
        with self.assertRaises(FrontendError):
            with self.store.transaction() as data:
                data['associations'][result['id']]['writable'] = True

    def test_manifest_native_path_traversal_rejected(self):
        with self.store.transaction() as data:
            result = associate(self.store, data, self.hunter, self.instance, 'trophy00', 'personal')
        with self.assertRaises(FrontendError):
            with self.store.transaction() as data:
                data['associations'][result['id']]['files'][0]['path'] = '../trophy00.sav'

    def test_case_collision_no_guess(self):
        (self.root / 'TROPHY00.SAV').write_bytes(save_bytes(3))
        if len(list(self.root.glob('*.[sS][aA][vV]'))) < 2:
            self.skipTest('case-insensitive filesystem')
        with self.assertRaises(FrontendError):
            inspect_set(self.root, inventory(self.root)[0])


if __name__ == '__main__':
    unittest.main()
