from copy import deepcopy
import os
from pathlib import Path
import tempfile
import unittest

from lodge.discovery import move_candidates, register, relocate
from lodge.store import FrontendError, Store, empty_manifest, validate
from support import game


class RelocationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.managed = self.directory / 'Expeditions'
        self.root = game(self.managed, 'Triassic')
        self.data = empty_manifest()

    def register_managed(self):
        return register(self.data, self.root, 'managed', managed_root=self.managed)

    def assert_relocation_rejected(self, instance, destination):
        before = deepcopy(self.data)
        with self.assertRaises(FrontendError):
            relocate(self.data, instance['id'], destination)
        self.assertEqual(self.data, before)

    def test_managed_root_survives_reload_and_rename(self):
        instance = self.register_managed()
        store = Store(self.directory / 'Lodge')
        with store.transaction() as data:
            data.update(self.data)
        self.data = store.read()
        instance = self.data['instances'][instance['id']]
        self.assertEqual(instance['managed_root'], {'path': str(self.managed), 'path_flavor': os.name})
        moved = self.managed / 'Triassic MEE'
        self.root.rename(moved)
        before = deepcopy(self.data)
        self.assertEqual(move_candidates(self.data, moved), [instance['id']])
        self.assertEqual(self.data, before)  # A suggestion is never a relocation.
        result = relocate(self.data, instance['id'], moved)
        self.assertEqual(result['mode'], 'managed')
        self.assertEqual(result['path'], str(moved))
        self.assertEqual(result['previous_locations'], [{'path': str(self.root), 'path_flavor': os.name}])
        self.assertEqual(validate(self.data), self.data)

    def test_managed_move_outside_and_back_does_not_reclaim_ownership(self):
        instance = self.register_managed()
        # Shares the root's textual prefix but is not within it.
        moved = self.directory / 'Expeditions-external'
        self.root.rename(moved)
        self.assertEqual(relocate(self.data, instance['id'], moved)['mode'], 'registered')
        self.assertEqual(instance['managed_root']['path'], str(self.managed))
        moved.rename(self.root)
        self.assertEqual(relocate(self.data, instance['id'], self.root)['mode'], 'registered')
        validate(self.data)

    def test_registered_rename_does_not_infer_management(self):
        instance = register(self.data, self.root)
        moved = self.managed / 'New name'
        self.root.rename(moved)
        self.assertEqual(relocate(self.data, instance['id'], moved)['mode'], 'registered')
        self.assertIsNone(instance['managed_root'])

    def test_legacy_managed_record_without_root_loads_but_cannot_relocate(self):
        instance = self.register_managed()
        del instance['managed_root']
        validate(self.data)  # No guessed migration of existing schema-1 records.
        moved = self.managed / 'New name'
        self.root.rename(moved)
        self.assert_relocation_rejected(instance, moved)

    def test_missing_managed_root_is_not_assumed_to_have_moved(self):
        instance = self.register_managed()
        moved = self.directory / 'Moved'
        self.root.rename(moved)
        self.managed.rmdir()
        self.assert_relocation_rejected(instance, moved)

    def test_foreign_managed_context_requires_reconciliation(self):
        instance = self.register_managed()
        instance['managed_root'] = ({'path': 'C:\\Expeditions', 'path_flavor': 'nt'}
                                    if os.name == 'posix' else {'path': '/Expeditions', 'path_flavor': 'posix'})
        instance.update(path=instance['managed_root']['path'] + '/Triassic',
                        path_flavor=instance['managed_root']['path_flavor'])
        validate(self.data)
        moved = self.managed / 'New name'
        self.root.rename(moved)
        self.assert_relocation_rejected(instance, moved)

    def test_managed_root_retargeted_by_symlink_is_ambiguous(self):
        instance = self.register_managed()
        moved = self.directory / 'Elsewhere'
        self.root.rename(moved)
        self.managed.rmdir()
        try:
            self.managed.symlink_to(self.directory, target_is_directory=True)
        except OSError:
            self.skipTest('symlinks unavailable')
        self.assert_relocation_rejected(instance, moved)

    def test_existing_old_path_or_claimed_destination_is_rejected(self):
        instance = self.register_managed()
        clone = game(self.managed, 'Clone')
        self.assert_relocation_rejected(instance, clone)
        moved = self.managed / 'Moved'
        self.root.rename(moved)
        register(self.data, moved)
        self.assert_relocation_rejected(instance, moved)

    def test_foreign_destination_is_not_reinterpreted_locally(self):
        instance = self.register_managed()
        self.root.rename(self.managed / 'Moved')
        foreign = 'C:\\Expeditions\\Triassic' if os.name == 'posix' else '/Expeditions/Triassic'
        self.assert_relocation_rejected(instance, foreign)

    def test_managed_root_validation_uses_recorded_path_flavor(self):
        instance = self.register_managed()
        for flavor, root, path in (('nt', 'C:\\Expeditions', 'c:\\expeditions\\Triassic'),
                                   ('nt', '\\\\host\\share\\Expeditions', '\\\\host\\share\\Expeditions\\Game'),
                                   ('posix', '/games/Expeditions', '/games/Expeditions/Game')):
            with self.subTest(flavor=flavor, root=root):
                instance.update(path=path, path_flavor=flavor,
                                managed_root={'path': root, 'path_flavor': flavor})
                validate(self.data)
                instance['path'] = root + '-outside/Game'
                with self.assertRaises(FrontendError):
                    validate(self.data)
        for bad in ({}, {'path': 'relative', 'path_flavor': os.name},
                    {'path': '/games/../Expeditions', 'path_flavor': 'posix'},
                    {'path': 'C:Expeditions', 'path_flavor': 'nt'}):
            instance['managed_root'] = bad
            with self.assertRaises(FrontendError):
                validate(self.data)


if __name__ == '__main__':
    unittest.main()
