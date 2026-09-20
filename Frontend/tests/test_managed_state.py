"""Disposable metadata upgrade tests. No default store or native save migration."""
import copy
import hashlib
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from lodge.managed_state import (AUTHORITY, UPGRADE_BACKUP, inspect_history,
                                 resolve_generation, upgrade_store)
from lodge.session_io import capture
from lodge.sessions import prepare_session
from lodge.store import FrontendError, validate
import test_sessions


class ManagedStateTests(unittest.TestCase):
    def setUp(self):
        self.fixture=test_sessions.SessionTests(); self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.store,self.association=self.fixture.store,self.fixture.association
        self.addCleanup(self.unchanged_native)

    def unchanged_native(self):
        self.assertEqual(capture(self.fixture.source),self.fixture.original)
        self.assertEqual(test_sessions.capture_native(self.fixture.game),self.fixture.native)

    def test_explicit_upgrade_backups_unknown_metadata_and_exact_import(self):
        with self.store.transaction() as d:
            d['future_extension']={'opaque':[1,2]}
            d['associations'][self.association]['future_extension']='preserve'
        before=self.store.path.read_bytes(); original=self.store.read()['associations'][self.association]
        self.assertEqual(self.store.read()['schema_version'],1) # reading never upgrades
        result=upgrade_store(self.store)
        d=self.store.read(); a=d['associations'][self.association]
        self.assertEqual(result['schema_version'],2)
        self.assertEqual((self.store.directory/UPGRADE_BACKUP).read_bytes(),before)
        self.assertEqual(d['state_upgrade']['backup_sha256'],hashlib.sha256(before).hexdigest())
        self.assertEqual(d['future_extension'],{'opaque':[1,2]})
        self.assertEqual({k:v for k,v in a.items() if k not in ('authority','managed_state')},
                         {k:v for k,v in original.items() if k!='authority'})
        h=inspect_history(self.store,self.association)
        self.assertEqual(h['authority'],AUTHORITY)
        generation,root,members,blobs=resolve_generation(self.store,d,a)
        self.assertEqual(generation['sequence'],0)
        self.assertEqual(root,self.fixture.source)
        self.assertEqual((members,blobs),self.fixture.original)
        self.assertEqual(upgrade_store(self.store)['result'],'already-upgraded')
        with self.assertRaises(FrontendError): self.store.restore_backup()
        with self.assertRaises(FrontendError): prepare_session(self.store,self.association,'areas:0')

    def test_corrupt_versions_shapes_and_history_fail_closed(self):
        upgrade_store(self.store); valid=self.store.read()
        mutations=[lambda d:d.update(schema_version=3),lambda d:d.update(schema_version=True),
                   lambda d:d.update(state_upgrade={}),
                   lambda d:d['associations'][self.association]['managed_state'].update(schema_version=99),
                   lambda d:d['associations'][self.association]['managed_state'].update(current_generation='bogus'),
                   lambda d:d['associations'][self.association].update(files=None)]
        for mutate in mutations:
            d=copy.deepcopy(valid);mutate(d)
            with self.assertRaises(FrontendError):validate(d)
        a=valid['associations'][self.association]; gid=a['managed_state']['current_generation']
        for field,value in [('id','bad'),('sequence',True),('predecessor',gid),('source_session',gid),
                            ('members',[]),('provenance',{}),('snapshot','../escape')]:
            d=copy.deepcopy(valid);d['associations'][self.association]['managed_state']['generations'][gid][field]=value
            with self.subTest(field=field),self.assertRaises(FrontendError):validate(d)

    def test_upgrade_failures_preserve_previous_authority_and_backup(self):
        original=self.store.path.read_bytes()
        from lodge.managed_state import atomic_write
        for failure in ('backup','commit'):
            def fail(path,content):
                if (failure=='backup' and path.name==UPGRADE_BACKUP) or (failure=='commit' and path==self.store.path):
                    raise OSError('injected upgrade failure')
                return atomic_write(path,content)
            with patch('lodge.managed_state.atomic_write',side_effect=fail),self.assertRaises(OSError):
                upgrade_store(self.store)
            self.assertEqual(self.store.path.read_bytes(),original)
            self.assertEqual(self.store.read()['schema_version'],1)
        self.assertEqual((self.store.directory/UPGRADE_BACKUP).read_bytes(),original)
        self.assertEqual(upgrade_store(self.store)['result'],'upgraded')

    def test_changed_import_and_reserved_unknown_fields_cannot_upgrade(self):
        with self.store.transaction() as d:d['associations'][self.association]['managed_state']={'unknown':True}
        with self.assertRaises(FrontendError):upgrade_store(self.store)
        with self.store.transaction() as d:del d['associations'][self.association]['managed_state']
        source=self.fixture.source/'trophy00.sav'; before=source.read_bytes()
        try:
            source.write_bytes(before[:-1])
            with self.assertRaises(FrontendError):upgrade_store(self.store)
            self.assertFalse((self.store.directory/UPGRADE_BACKUP).exists())
        finally:source.write_bytes(before)

    def test_unknown_origin_is_preserved_without_granting_launch(self):
        with self.store.transaction() as d:d['associations'][self.association]['origin']='unknown'
        upgrade_store(self.store)
        self.assertEqual(inspect_history(self.store,self.association)['import_provenance']['origin'],'unknown')
        from lodge.sessions import snapshot_pins
        from test_genesis_hunt import selection
        with self.assertRaisesRegex(FrontendError,'managed personal'):
            snapshot_pins(self.store,self.association,selection(),None,managed=True,mode='hunt')

    def test_terminal_schema_one_and_two_inspection_survives_upgrade(self):
        from lodge.session_runner import run_session
        from lodge.reconciliation import reconcile_session
        from lodge.session_io import read_journal, session_root
        from lodge.native_observer import prepare_native, run_native
        import os
        synthetic=prepare_session(self.store,self.association,'areas:0')
        run_session(self.store,synthetic['id'])
        synthetic=reconcile_session(self.store,synthetic['id'])
        engine=Path(os.environ['C2_NATIVE_TEST_ENGINE']).resolve()
        digest=hashlib.sha256(engine.read_bytes()).hexdigest()
        with patch('lodge.native_observer.observer_policy',return_value={
                'adapter':'old-observer-fixture-only','candidate_argv':[
                    'reg=0','prj=huntdat/areas/area1','din=0','wep=0','dtm=1','-observ']}):
            observer=prepare_native(self.store,self.association,'areas:0',engine,digest,True)
            run_native(self.store,observer['id'],engine,digest,True)
            observer=reconcile_session(self.store,observer['id'])
        self.assertEqual(synthetic['state'],'candidate')
        self.assertEqual(observer['state'],'candidate')
        originals={j['id']:(session_root(self.store,j['id'])/'journal.json').read_bytes()
                   for j in (synthetic,observer)}
        upgrade_store(self.store)
        for j in (synthetic,observer):
            self.assertEqual(read_journal(self.store,j['id']),j)
            self.assertEqual(reconcile_session(self.store,j['id']),j)
            self.assertEqual((session_root(self.store,j['id'])/'journal.json').read_bytes(),originals[j['id']])

    def test_new_import_after_upgrade_gets_distinct_g0_with_unchanged_provenance(self):
        from lodge.profiles import associate
        upgrade_store(self.store)
        original=self.store.read()['associations'][self.association]
        with self.store.transaction() as d:
            new=associate(self.store,d,original['hunter_id'],original['instance_id'],
                          'trophy00','personal','managed')
        self.assertNotEqual(new['id'],original['id'])
        self.assertEqual(len(new['managed_state']['generations']),1)
        self.assertEqual(new['files'],original['files'])
        self.assertEqual(capture(self.store.directory/'snapshots'/new['id']),self.fixture.original)

    def test_missing_upgraded_manifest_never_becomes_an_empty_store(self):
        from lodge.store import Store
        empty=Store(self.store.directory.parent/'EmptyTaskStore')
        upgrade_store(empty)
        self.assertFalse(empty.path.with_suffix('.json.bak').exists())
        before=empty.path.read_bytes();empty.path.unlink()
        with self.assertRaises(FrontendError):empty.read()
        empty.path.write_bytes(before)
