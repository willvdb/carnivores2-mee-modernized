"""Fixture-only adoption/continuation through real codecs and production I/O.

The sole edition policy double is explicit; no fixture content is certified as
Genesis. Production policy rejection is tested independently below. No real save,
installation or default store is selected by this suite.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import threading
import unittest
from unittest.mock import patch

from lodge.acceptance import (accept_candidate, candidate_digest, preview_acceptance,
                              recover_acceptance)
from lodge.genesis_hunt import POLICY_ID
from lodge.managed_state import inspect_history, resolve_generation, upgrade_store
from lodge.native_hunt import prepare_hunt, run_hunt
from lodge.reconciliation import reconcile_session
from lodge.session_io import capture, encode, persist, read_journal, session_root, transition
from lodge.session_runner import recover_session
from lodge.store import FrontendError, Store, new_id
import test_sessions
from test_genesis_hunt import selection
from test_profiles import save_bytes


class AcceptanceFixtureTests(unittest.TestCase):
    def setUp(self):
        self.fixture=test_sessions.SessionTests();self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.store,self.association=self.fixture.store,self.fixture.association
        self.engine=Path(os.environ['C2_NATIVE_TEST_ENGINE']).resolve()
        self.digest=hashlib.sha256(self.engine.read_bytes()).hexdigest()
        upgrade_store(self.store)
        self.g0=self.head()
        self.policy=patch('lodge.native_continuation.hunt_policy',side_effect=self.fixture_policy)
        self.policy.start();self.addCleanup(self.policy.stop)
        self.addCleanup(self.unchanged_native)

    @staticmethod
    def fixture_policy(revision,catalog,slot,chosen,score):
        return {'adapter':POLICY_ID,'fixture_only':'authored disposable state, NOT Genesis',
                'selection':copy.deepcopy(chosen),
                'candidate_argv':[f'reg={slot}','prj=huntdat/areas/area1','din=1','wep=1',
                                  f"dtm={chosen['time_of_day']}",'smod=0.85,0.70,0.80,1.0,1.25,1.0']}

    def unchanged_native(self):
        self.assertEqual(capture(self.fixture.source),self.fixture.original)
        self.assertEqual(test_sessions.capture_native(self.fixture.game),self.fixture.native)

    def head(self):return self.store.read()['associations'][self.association]['managed_state']['current_generation']

    def prepare(self):
        return prepare_hunt(self.store,self.association,selection(),self.engine,self.digest,True)

    def returned(self,j=None,scenario='changed'):
        j=j or self.prepare()
        with patch.dict(os.environ,{'C2_NATIVE_FIXTURE_BEHAVIOR':scenario}):
            result=run_hunt(self.store,j['id'],self.engine,self.digest,True)
        self.assertEqual(result['state'],'returned',result['diagnostics'])
        return result

    def candidate(self,j=None,scenario='changed'):
        j=self.returned(j,scenario);result=reconcile_session(self.store,j['id'])
        self.assertEqual(result['state'],'candidate',result['diagnostics'])
        return result

    def accept(self,j,expected=None):
        return accept_candidate(self.store,j['id'],expected or j['pins']['generation_id'],candidate_digest(j))

    def assert_blocked(self,j,expected=None):
        before=self.store.path.read_bytes()
        preview=preview_acceptance(self.store,j['id'],expected or self.head())
        self.assertFalse(preview['allowed'],preview)
        with self.assertRaises(FrontendError):self.accept(j,expected or self.head())
        self.assertEqual(self.store.path.read_bytes(),before)

    def test_s1_accept_g1_s2_actual_input_accept_g2_and_late_retry(self):
        s1=self.candidate(); r1=session_root(self.store,s1['id'])
        self.assertEqual(s1['schema_version'],4)
        self.assertEqual(capture(r1/'baseline'),self.fixture.original)
        self.unchanged_native()
        before_store=capture(self.store.directory)
        preview=preview_acceptance(self.store,s1['id'],self.g0)
        self.assertTrue(preview['allowed'],preview['diagnostics'])
        self.assertEqual(capture(self.store.directory),before_store) # preview creates no lock/evidence
        self.assertEqual(preview['candidate_sha256'],candidate_digest(s1))
        accepted=self.accept(s1);g1=accepted['current_generation']
        self.unchanged_native()
        self.assertEqual(capture(r1/'baseline'),self.fixture.original)
        self.assertNotEqual(g1,self.g0)
        self.assertEqual(read_journal(self.store,s1['id']),s1) # candidate evidence not rewritten
        d=self.store.read();a=d['associations'][self.association]
        _,g1root,g1members,g1bytes=resolve_generation(self.store,d,a)
        self.assertEqual((g1members,g1bytes),capture(r1/'returned'))
        self.assertNotEqual(g1bytes,self.fixture.original[1])
        self.assertNotEqual(g1root,r1/'returned')
        s2=self.prepare();r2=session_root(self.store,s2['id'])
        self.unchanged_native()
        self.assertEqual(capture(g1root),(g1members,g1bytes))
        self.assertEqual(s2['pins']['generation_id'],g1)
        self.assertEqual(s2['pins']['source_root'],str(g1root))
        self.assertIn('--session-source='+str(g1root),s2['execution']['argv'])
        self.assertEqual(capture(r2/'baseline'),(g1members,g1bytes))
        self.assertEqual(capture(r2/'work/state'),(g1members,g1bytes))
        s2=self.candidate(s2)
        self.unchanged_native()
        self.assertEqual(capture(g1root),(g1members,g1bytes))
        accepted2=self.accept(s2);g2=accepted2['current_generation']
        self.unchanged_native()
        self.assertNotIn(g2,(self.g0,g1))
        self.assertEqual(capture(g1root),(g1members,g1bytes))
        self.assertEqual(capture(r1/'baseline'),self.fixture.original)
        self.assertEqual(capture(r1/'returned'),(g1members,g1bytes))
        self.assertEqual(capture(r2/'baseline'),(g1members,g1bytes))
        d=self.store.read();a=d['associations'][self.association]
        self.assertEqual(len(a['managed_state']['generations']),3)
        retry=self.accept(s1,self.g0)
        self.assertEqual(retry['result'],'already-accepted');self.assertEqual(retry['receipt'],accepted['receipt'])
        self.assertEqual(retry['current_generation'],g2)
        self.assertEqual(self.head(),g2)
        self.assertEqual(self.accept(s2)['receipt'],accepted2['receipt'])
        with self.assertRaises(FrontendError):self.accept(s1,g1)

    def test_two_candidates_same_generation_and_stale_prepared_launch(self):
        s1=self.candidate(scenario='');s2=self.candidate(scenario='');prepared=self.prepare()
        self.assertEqual(s1['returned_members'],s2['returned_members']) # equality is not identity
        self.accept(s1)
        self.assert_blocked(s2,self.g0)
        with patch('lodge.session_runner.Popen') as spawn:
            failed=run_hunt(self.store,prepared['id'],self.engine,self.digest,True)
        spawn.assert_not_called();self.assertEqual(failed['state'],'failed')
        self.assertEqual(read_journal(self.store,s2['id'])['pins']['generation_id'],self.g0)

    def test_delayed_inspection_retains_actual_historical_baseline(self):
        delayed=self.returned();first=self.candidate();self.accept(first)
        later=reconcile_session(self.store,delayed['id'])
        self.assertEqual(later['state'],'candidate',later['diagnostics'])
        self.assertEqual(later['pins']['generation_id'],self.g0)
        self.assertEqual(capture(session_root(self.store,later['id'])/'baseline'),self.fixture.original)
        self.assert_blocked(later,self.g0)

    def test_production_policy_refuses_fixture_without_touching_content_pin(self):
        j=self.candidate();self.policy.stop()
        try:
            self.assert_blocked(j)
            with self.assertRaisesRegex(FrontendError,'unpinned content'):self.prepare()
        finally:self.policy.start()

    def test_preview_digest_pins_specific_candidate_and_lock_revalidation(self):
        j=self.candidate();preview=preview_acceptance(self.store,j['id'],self.g0)
        with self.store.lock(),self.assertRaises(FrontendError):self.accept(j)
        changed=copy.deepcopy(j);changed['diagnostics'].append({'code':'review'})
        persist(session_root(self.store,j['id']),changed)
        with self.assertRaisesRegex(FrontendError,'differs from explicit preview'):
            accept_candidate(self.store,j['id'],self.g0,preview['candidate_sha256'])
        self.assert_blocked(changed)

    def test_complete_evidence_and_all_identity_policy_pins_are_revalidated(self):
        original=self.candidate();root=session_root(self.store,original['id'])
        mutations=[lambda j:j['pins'].update(association_id=new_id()),
            lambda j:j['pins'].update(hunter_id=new_id()),lambda j:j['pins'].update(instance_id=new_id()),
            lambda j:j['pins'].update(native_slot=1),lambda j:j['pins']['revision'].update(sha256='0'*64),
            lambda j:j['pins'].update(adapter='other'),lambda j:j['pins']['hunt_policy'].update(adapter='other'),
            lambda j:j['pins'].update(generation_id=new_id()),
            lambda j:j['process'].update(exit_code=3),lambda j:j['process'].update(exit_code=False),
            lambda j:j['process'].update(stop_reason='cancelled'),lambda j:j['process'].update(pid=None),
            lambda j:j['process'].pop('returned_at'),
            lambda j:j['capabilities'].update(engine_process_executed=False),
            lambda j:j['capabilities'].update(returned_native_state_readable='unknown'),
            lambda j:j['reconciliation']['observation'].update(codec_inspection='unavailable'),
            lambda j:j['reconciliation'].update(changed_members=None),
            lambda j:j.update(returned_observation=None),lambda j:j.update(returned_members=[]),
            lambda j:j['returned_observation']['trophy00.sav'].update(registration=7),
            lambda j:j['execution']['argv'].append('-debug'),
            lambda j:j['execution']['contract'].update(version=True),
            lambda j:j['logs']['stdout'].update(error='lost log'),
            lambda j:j['logs']['stderr'].update(total_bytes=False),
            lambda j:j['logs']['stderr'].pop('error'),
            lambda j:j['logs']['stdout'].update(truncated=True)]
        for mutate in mutations:
            j=copy.deepcopy(original);mutate(j)
            (root/'journal.json').write_bytes(encode(j)) # intentionally malformed evidence retained
            with self.subTest(mutate=mutate):self.assert_blocked(j)
        (root/'journal.json').write_bytes(encode(original))
        self.assertTrue(preview_acceptance(self.store,original['id'],self.g0)['allowed'])

    def test_changed_missing_extra_corrupt_and_aliased_state(self):
        j=self.candidate();root=session_root(self.store,j['id'])
        for domain in ('baseline','returned','work/state'):
            file=root/domain/'trophy00.sav';blob=file.read_bytes()
            for corruption in ('change','missing','short','hardlink','symlink'):
                with self.subTest(domain=domain,corruption=corruption):
                    extra=root/domain/'other.sav'
                    try:
                        if corruption=='change':file.write_bytes(save_bytes(score=52))
                        elif corruption=='short':file.write_bytes(blob[:-1])
                        else:
                            file.unlink()
                            if corruption=='hardlink':os.link(self.fixture.source/'trophy00.sav',file)
                            elif corruption=='symlink':
                                # Windows junction coverage is retained by existing native tests;
                                # symlinks require privilege on Windows.
                                if os.name=='nt':continue
                                file.symlink_to(self.fixture.source/'trophy00.sav')
                        self.assert_blocked(j)
                    finally:
                        if file.exists() or file.is_symlink():file.unlink()
                        file.write_bytes(blob)
            extra=root/domain/'unexpected';extra.write_bytes(b'unknown')
            try:self.assert_blocked(j)
            finally:extra.unlink()

    def test_missing_or_corrupt_current_generation_never_falls_back(self):
        s1=self.candidate();self.accept(s1);d=self.store.read();a=d['associations'][self.association]
        _,root,_,_=resolve_generation(self.store,d,a);file=root/'trophy00.sav';before=file.read_bytes()
        for blob in (None,b'bad'):
            if blob is None:file.unlink()
            else:file.write_bytes(blob)
            with self.assertRaises(FrontendError):self.prepare()
            with self.assertRaises(FrontendError):inspect_history(self.store,self.association)
            with self.assertRaises(FrontendError):self.accept(s1,self.g0)
            self.assertNotEqual(self.head(),self.g0)
            file.write_bytes(before)

    def test_no_query_launch_signal_or_promotion_during_recovery(self):
        prepared=self.prepare();root=session_root(self.store,prepared['id'])
        transition(root,prepared,'launching')
        with patch('lodge.native_continuation.query_contract',side_effect=AssertionError('no query')), \
             patch('lodge.native_session.query_contract',side_effect=AssertionError('no query')), \
             patch('lodge.session_runner.Popen',side_effect=AssertionError('no launch')), \
             patch('os.kill',side_effect=AssertionError('no signaling')):
            interrupted=recover_session(self.store,prepared['id'])
            self.assertEqual(interrupted['state'],'interrupted')
            self.assertEqual(recover_acceptance(self.store,prepared['id'])['result'],'not-committed')
            self.assert_blocked(interrupted)
        j=self.candidate()
        with patch('lodge.native_continuation.query_contract',side_effect=AssertionError('no query')), \
             patch('lodge.session_runner.Popen',side_effect=AssertionError('no launch')):
            accepted=self.accept(j)
            self.assertEqual(recover_acceptance(self.store,j['id'])['receipt'],accepted['receipt'])

    def test_snapshot_copy_publication_and_manifest_failures_keep_old_authority(self):
        from lodge.acceptance import atomic_write
        j=self.candidate();before=self.store.path.read_bytes()
        for failure in ('copy','publication','metadata'):
            def fail_write(path,content):
                if path==self.store.path:raise OSError('injected metadata failure')
                return atomic_write(path,content)
            target={'copy':'lodge.acceptance.write_blobs','publication':'lodge.acceptance.os.rename',
                    'metadata':'lodge.acceptance.atomic_write'}[failure]
            action=fail_write if failure=='metadata' else OSError('injected '+failure)
            with patch(target,side_effect=action),self.assertRaises(OSError):self.accept(j)
            self.assertEqual(self.store.path.read_bytes(),before)
            self.assertEqual(self.head(),self.g0)
            self.assertEqual(recover_acceptance(self.store,j['id'])['result'],'not-committed')
        evidence=list((self.store.directory/'generations'/self.association).iterdir())
        self.assertEqual(len(evidence),3) # partial/unreferenced evidence retained
        accepted=self.accept(j);self.assertNotEqual(accepted['current_generation'],self.g0)
        self.assertTrue(all(p.exists() for p in evidence))

    def test_receipt_completion_and_post_replace_failure_keep_new_authority(self):
        j=self.candidate()
        with patch('lodge.acceptance.complete_receipt',side_effect=OSError('receipt failure')),self.assertRaises(OSError):
            self.accept(j)
        head=self.head();self.assertNotEqual(head,self.g0)
        self.assertFalse((session_root(self.store,j['id'])/'acceptance.json').exists())
        recovered=recover_acceptance(self.store,j['id'])
        self.assertEqual(recovered['current_generation'],head)
        self.assertEqual(self.accept(j)['receipt'],recovered['receipt'])
        j2=self.candidate()
        from lodge.acceptance import atomic_write
        def after_replace(path,content):
            atomic_write(path,content)
            if path==self.store.path:raise OSError('injected after commit/directory sync')
        with patch('lodge.acceptance.atomic_write',side_effect=after_replace),self.assertRaises(OSError):self.accept(j2)
        self.assertNotEqual(self.head(),head)
        self.assertEqual(recover_acceptance(self.store,j2['id'])['current_generation'],self.head())

    def test_quarantined_failed_and_old_session_kinds_are_ineligible(self):
        returned=self.returned(scenario='nonzero');j=reconcile_session(self.store,returned['id'])
        self.assertEqual(j['state'],'quarantined');self.assert_blocked(j)
        j=self.prepare();root=session_root(self.store,j['id']);transition(root,j,'failed')
        self.assert_blocked(j)
        candidate=self.candidate();root=session_root(self.store,candidate['id'])
        for version,kind in [(1,'controlled-synthetic'),(2,'experimental-native-observer-v1'),
                             (3,'experimental-native-hunt-v1'),(99,'unknown')]:
            j=copy.deepcopy(candidate);j.update(schema_version=version);j['execution']['kind']=kind
            (root/'journal.json').write_bytes(encode(j))
            self.assert_blocked(j)
        (root/'journal.json').write_bytes(encode(candidate))

    def test_snapshot_directory_alias_and_windows_reparse_attributes_fail_closed(self):
        j=self.candidate();root=session_root(self.store,j['id']);returned=root/'returned'
        moved=root/'saved-returned';returned.rename(moved)
        try:
            if os.name!='nt':
                returned.symlink_to(moved,target_is_directory=True)
                self.assert_blocked(j)
                returned.unlink()
            else:
                import subprocess
                result=subprocess.run(['cmd','/c','mklink','/J',str(returned),str(moved)],capture_output=True)
                self.assertEqual(result.returncode,0,result.stderr)
                self.assert_blocked(j)
                os.rmdir(returned)
        finally:
            if returned.is_symlink():returned.unlink()
            if not returned.exists():moved.rename(returned)
        from types import SimpleNamespace
        real_lstat=Path.lstat
        def lstat(path,*args,**kwargs):
            value=real_lstat(path,*args,**kwargs)
            if path==returned:
                return SimpleNamespace(st_mode=value.st_mode,st_nlink=value.st_nlink,
                    st_file_attributes=getattr(stat,'FILE_ATTRIBUTE_REPARSE_POINT',0x400))
            return value
        with patch.object(Path,'lstat',lstat):self.assert_blocked(j)

    def test_partial_copy_retained_and_receipt_copy_cannot_rewind_head(self):
        from lodge.session_io import write_blobs
        j=self.candidate()
        def partial(directory,blobs):
            name=next(iter(blobs));write_blobs(directory,{name:blobs[name]})
            raise OSError('partial snapshot copy')
        with patch('lodge.acceptance.write_blobs',side_effect=partial),self.assertRaises(OSError):self.accept(j)
        parent=self.store.directory/'generations'/self.association
        stage=next(parent.iterdir())
        self.assertEqual(len(list(stage.iterdir())),1)
        self.assertEqual(self.head(),self.g0)
        self.assertEqual(recover_acceptance(self.store,j['id'])['result'],'not-committed')
        accepted=self.accept(j);head=self.head()
        path=session_root(self.store,j['id'])/'acceptance.json';before=path.read_bytes()
        path.write_text('{}')
        with self.assertRaises(FrontendError):recover_acceptance(self.store,j['id'])
        self.assertEqual(self.head(),head)
        path.write_bytes(before)
        self.assertEqual(recover_acceptance(self.store,j['id'])['receipt'],accepted['receipt'])

    def test_acceptance_checks_baseline_registration_and_native_pair_without_reencoding(self):
        j=self.candidate();root=session_root(self.store,j['id'])
        blob=(root/'returned/trophy00.sav').read_bytes()
        bad=bytearray(blob);bad[128:132]=(7).to_bytes(4,'little')
        for domain in ('work/state','returned'):(root/domain/'trophy00.sav').write_bytes(bad)
        members=capture(root/'returned')[0]
        j.update(returned_members=members,return_capture=members)
        # Updating the hash claims cannot substitute for registration/codec checks.
        (root/'journal.json').write_bytes(encode(j))
        self.assert_blocked(j)

    def test_current_history_metadata_corruption_and_unknown_fields(self):
        j=self.candidate();accepted=self.accept(j);original=self.store.path.read_bytes()
        gid=accepted['current_generation'];valid=self.store.read()
        from lodge.store import validate
        for field,bad in [('predecessor',gid),('source_session',new_id()),('snapshot','snapshots/'+self.association),
                          ('execution',None),('policy','observer'),('members',[])]:
            data=copy.deepcopy(valid)
            data['associations'][self.association]['managed_state']['generations'][gid][field]=bad
            with self.assertRaises(FrontendError):validate(data)
        with self.store.transaction() as data:
            data['associations'][self.association]['managed_state']['future_note']={'safe':'opaque'}
        self.assertEqual(inspect_history(self.store,self.association)['history']['future_note'],{'safe':'opaque'})
        self.assertEqual(self.accept(j)['current_generation'],gid)

    def test_frontend_cli_inspect_preview_accept_and_history_contract(self):
        # In-process CLI parsing/dispatch uses the visibly scoped fixture policy.
        import frontend
        def command(*args):
            return frontend.execute(frontend.parser().parse_args(['--store',str(self.store.directory),*args]))
        self.assertEqual(command('managed-state','upgrade')['result'],'already-upgraded')
        j=self.candidate()
        self.assertEqual(command('native-hunt','inspect',j['id']),j)
        preview=command('managed-state','preview',j['id'],'--expected-generation',self.g0)
        self.assertTrue(preview['allowed'])
        result=command('managed-state','accept',j['id'],'--expected-generation',self.g0,
                       '--candidate-sha256',preview['candidate_sha256'])
        self.assertEqual(command('managed-state','inspect',self.association)['current_generation'],result['current_generation'])
        self.assertEqual(command('managed-state','recover-acceptance',j['id'])['receipt'],result['receipt'])
        next_plan=command('native-hunt','plan',self.association,'--area','areas:0',
                          '--license','licenses:0','--weapon','weapons:0')
        self.assertEqual(next_plan['pins']['generation_id'],result['current_generation'])

    def test_missing_or_corrupt_accepted_build_evidence_fails_metadata_validation(self):
        from lodge.store import validate
        j=self.candidate();accepted=self.accept(j);gid=accepted['current_generation'];valid=self.store.read()
        for bad in (None,{}, {'kind':'unknown'}):
            d=copy.deepcopy(valid);h=d['associations'][self.association]['managed_state']
            h['generations'][gid]['execution']=bad;h['receipts'][j['id']]['execution']=bad
            with self.assertRaises(FrontendError):validate(d)
        for field,bad in [('contract',{'version':99}),('executable',{}),('shell',True),('argv',['ignored']),
                          ('timeout_seconds',True),('config_sha256','0'*64)]:
            d=copy.deepcopy(valid);h=d['associations'][self.association]['managed_state']
            for record in (h['generations'][gid],h['receipts'][j['id']]):record['execution'][field]=bad
            with self.assertRaises(FrontendError):validate(d)

    def test_actual_cancelled_hunt_cannot_be_accepted(self):
        j=self.prepare();cancel=threading.Event();timer=threading.Timer(0.15,cancel.set);timer.start()
        try:
            with patch.dict(os.environ,{'C2_NATIVE_FIXTURE_BEHAVIOR':'hang'}):
                result=run_hunt(self.store,j['id'],self.engine,self.digest,True,cancel=cancel)
        finally:timer.cancel()
        self.assertEqual(result['process']['stop_reason'],'cancelled')
        quarantined=reconcile_session(self.store,j['id'])
        self.assertEqual(quarantined['state'],'quarantined');self.assert_blocked(quarantined)

    def test_actual_atomic_manifest_replace_failure_preserves_previous_head(self):
        j=self.candidate();before=self.store.path.read_bytes();replace=os.replace
        def fail(source,destination):
            if Path(destination)==self.store.path:raise OSError('injected atomic replace failure')
            return replace(source,destination)
        with patch('lodge.store.os.replace',side_effect=fail),self.assertRaises(OSError):self.accept(j)
        self.assertEqual(self.store.path.read_bytes(),before)
        self.assertEqual(self.head(),self.g0)
        self.assertEqual(recover_acceptance(self.store,j['id'])['result'],'not-committed')
