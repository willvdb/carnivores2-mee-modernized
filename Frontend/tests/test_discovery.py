import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from lodge.discovery import (discover, fingerprint, inspect_instance, move_candidates,
                             recognize, refresh_instance, register, relocate, resolve_reference)
from lodge.store import FrontendError, empty_manifest
from support import game


class DiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = game(self.temp.name)
        self.data = empty_manifest()

    def test_coherent_and_nested_roots_not_overlays(self):
        other = game(self.temp.name, 'bundle/deep/- Game')
        overlay = Path(self.temp.name) / 'pack/weapon/HUNTDAT'
        overlay.mkdir(parents=True)
        (overlay / '_RES.TXT').write_text('weapons {}')
        results = discover(self.temp.name)
        self.assertEqual({r['path'] for r in results if r['recognized']}, {str(self.root), str(other)})
        self.assertFalse(recognize(overlay.parent)['recognized'])

    def test_stable_identity_move_clone_and_missing(self):
        instance = register(self.data, self.root)
        self.assertEqual(register(self.data, self.root / '.')['id'], instance['id'])
        with self.assertRaises(FrontendError):
            relocate(self.data, instance['id'], self.root)
        moved = self.root.with_name('Moved Ω')
        self.root.rename(moved)
        self.assertFalse(inspect_instance(instance)['recognized'])
        self.assertEqual(move_candidates(self.data, moved), [instance['id']])
        self.assertEqual(relocate(self.data, instance['id'], moved)['id'], instance['id'])
        clone = game(self.temp.name, 'Clone')
        self.assertNotEqual(register(self.data, clone)['id'], instance['id'])

    def test_fingerprint_ignores_mutable_and_detects_content(self):
        instance = register(self.data, self.root)
        old = fingerprint(self.root)
        for name in ('trophy00.sav', 'trace.log'):
            (self.root / 'HUNTDAT' / name).write_bytes(b'mutable')
        (self.root / 'config.cfg').write_text('root runtime settings')
        self.assertEqual(old, fingerprint(self.root))
        (self.root / 'HUNTDAT/_RES.TXT').write_text('weapons {}\ncharacters {}\n// changed content')
        self.assertTrue(inspect_instance(instance)['revision_changed'])
        self.assertEqual(instance['revision'], old)
        refresh_instance(instance)
        self.assertEqual(len(instance['revisions']), 2)

    def test_case_collision_traversal_and_symlink(self):
        self.assertEqual(resolve_reference(self.root, r'huntdat\areas\area1.map')['status'], 'found')
        self.assertEqual(resolve_reference(self.root, '../elsewhere')['status'], 'unsafe')
        self.assertEqual(resolve_reference(self.root, 'C:\\elsewhere')['status'], 'unsafe')
        try:
            (self.root / 'HUNTDAT/areas').mkdir()
        except FileExistsError:
            # Case-insensitive hosts cannot represent the collision; verify
            # their actual single-directory resolution and still test escapes.
            self.assertTrue((self.root / 'HUNTDAT/areas').samefile(self.root / 'HUNTDAT/AREAS'))
            self.assertEqual(resolve_reference(self.root, 'HUNTDAT/AREAS')['status'], 'found')
        else:
            self.assertEqual(resolve_reference(self.root, 'HUNTDAT/AREAS')['status'], 'ambiguous')
        try:
            (self.root / 'escape').symlink_to(Path(self.temp.name), target_is_directory=True)
        except OSError:
            self.skipTest('symlinks unavailable')
        self.assertEqual(resolve_reference(self.root, 'escape')['status'], 'unsafe')

    def test_managed_mode_requires_explicit_root(self):
        with self.assertRaises(FrontendError):
            register(self.data, self.root, 'managed')
        self.assertEqual(register(self.data, self.root, 'managed', managed_root=self.temp.name)['mode'], 'managed')

    def test_engine_revision_is_separate_from_content(self):
        instance = register(self.data, self.root)
        (self.root / 'CARN2.EXE').write_bytes(b'different engine')
        result = inspect_instance(instance)
        self.assertFalse(result['revision_changed'])
        self.assertTrue(result['engine_changed'])

    def test_engine_less_content_is_recognized_and_registerable(self):
        (self.root / 'CARN2.EXE').unlink()
        result = recognize(self.root)
        self.assertTrue(result['recognized'])
        self.assertEqual(result['capabilities'], {'content_recognized': 'yes',
                         'bundled_engine_evidence': 'none', 'modern_engine_compatibility': 'unknown'})
        self.assertIn('missing-engine-evidence', [d['code'] for d in result['diagnostics']])
        self.assertEqual(register(self.data, self.root)['engine_evidence'], [])

    def test_engine_less_partial_content_still_fails(self):
        (self.root / 'CARN2.EXE').unlink()
        (self.root / 'HUNTDAT/AREAS/AREA1.RSC').unlink()
        result = recognize(self.root)
        self.assertFalse(result['recognized'])
        self.assertEqual(result['capabilities']['bundled_engine_evidence'], 'none')
        self.assertIn('missing-map-pair', [d['code'] for d in result['diagnostics']])
        with self.assertRaises(FrontendError):
            register(self.data, self.root)

    def test_bundled_engine_is_evidence_not_content_identity(self):
        instance = register(self.data, self.root)
        result = recognize(self.root)
        self.assertTrue(result['recognized'])
        self.assertEqual(result['capabilities']['bundled_engine_evidence'], 'candidate-files-only')
        self.assertEqual([e['path'] for e in instance['engine_evidence']], ['CARN2.EXE'])
        old = instance['revision']
        (self.root / 'CARN2.EXE').write_bytes(b'updated bundled engine')
        self.assertEqual(fingerprint(self.root), old)
        (self.root / 'CARN2.EXE').unlink()
        self.assertEqual(fingerprint(self.root), old)
        self.assertTrue(inspect_instance(instance)['recognized'])
        self.assertTrue(inspect_instance(instance)['engine_changed'])

    def test_random_script_or_trophy_only_tree_is_not_coherent(self):
        (self.root / 'HUNTDAT/_RES.TXT').write_text('not a resource script')
        self.assertFalse(recognize(self.root)['recognized'])
        (self.root / 'HUNTDAT/_RES.TXT').write_text('weapons {}\ncharacters {}')
        for suffix in ('MAP', 'RSC'):
            (self.root / f'HUNTDAT/AREAS/AREA1.{suffix}').rename(self.root / f'HUNTDAT/AREAS/TROPHY.{suffix}')
        self.assertFalse(recognize(self.root)['recognized'])

    def test_fingerprint_refuses_content_aliases(self):
        path = self.root / 'HUNTDAT/AREAS/alias.map'
        try:
            path.symlink_to('AREA1.MAP')
        except OSError:
            self.skipTest('symlinks unavailable')
        with self.assertRaises(FrontendError):
            fingerprint(self.root)

    def test_fingerprint_detects_new_files_during_hashing(self):
        from lodge.discovery import hash_file
        def mutate(path):
            (self.root / 'HUNTDAT/new.cfg').write_text('gameplay content')
            return hash_file(path)
        with patch('lodge.discovery.hash_file', side_effect=mutate):
            with self.assertRaises(FrontendError):
                fingerprint(self.root)


if __name__ == '__main__':
    unittest.main()
