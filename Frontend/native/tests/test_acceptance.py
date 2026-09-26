"""Native acceptance against unchanged lodge.acceptance over one disposable store.

Candidate journals are produced by the Python reference (prepare_hunt/run_hunt/
reconcile with the fixture hunt policy patched, the compiled engine fixture and
a schema-2 store). Journals pin absolute store paths, so instead of a twin
directory each mutating operation runs on the same path twice: the store is
copied aside, the reference runs and its results/bytes are recorded, the copy
is restored, then the native driver runs and everything is compared with only
the native-generated generation UUID and timestamp substituted (format checked).
The native outcome is kept, so the reference then continues (prepares the next
generation, reads receipts) on natively written authority.
"""
import copy
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

FRONTEND = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(FRONTEND), str(FRONTEND / 'tests')]
DRIVER, PROBE, ENGINE = (str(Path(a).resolve()) for a in sys.argv[1:4])
os.environ['C2_PROFILE_PROBE'] = PROBE
os.environ['C2_NATIVE_TEST_ENGINE'] = ENGINE
import test_sessions  # noqa: E402
from lodge.acceptance import (accept_candidate, candidate_digest, preview_acceptance,  # noqa: E402
                              recover_acceptance)
from lodge.genesis_hunt import POLICY_ID  # noqa: E402
from lodge.managed_state import resolve_generation, upgrade_store  # noqa: E402
from lodge.native_hunt import prepare_hunt, run_hunt  # noqa: E402
from lodge.reconciliation import reconcile_session  # noqa: E402
from lodge.session_io import capture, encode, read_journal, session_root, transition  # noqa: E402
from lodge.session_runner import recover_session  # noqa: E402
from lodge.store import FrontendError, new_id, valid_id  # noqa: E402
from test_genesis_hunt import selection  # noqa: E402
from test_profiles import save_bytes  # noqa: E402

ISO = re.compile(r'^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d{6})?\+00:00$')
POSIX = os.name == 'posix'


def fixture_policy(revision, catalog, slot, chosen, score):
    return {'adapter': POLICY_ID, 'fixture_only': 'authored disposable state, NOT Genesis',
            'selection': copy.deepcopy(chosen),
            'candidate_argv': [f'reg={slot}', 'prj=huntdat/areas/area1', 'din=1', 'wep=1',
                               f"dtm={chosen['time_of_day']}", 'smod=0.85,0.70,0.80,1.0,1.25,1.0']}


def ordered(value):
    """Order-sensitive JSON text: key order is part of the compared result."""
    return json.dumps(value)


class Acceptance(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.fixture = test_sessions.SessionTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.store, self.association = self.fixture.store, self.fixture.association
        self.digest = __import__('hashlib').sha256(Path(ENGINE).read_bytes()).hexdigest()
        upgrade_store(self.store)
        self.g0 = self.head()
        self.policy = patch('lodge.native_continuation.hunt_policy', side_effect=fixture_policy)
        self.policy.start()
        self.addCleanup(self.policy.stop)
        self.addCleanup(self.unchanged_native)
        self.backups = 0

    # -- fixture pipeline (unchanged reference) --------------------------------
    def unchanged_native(self):
        self.assertEqual(capture(self.fixture.source), self.fixture.original)
        self.assertEqual(test_sessions.capture_native(self.fixture.game), self.fixture.native)

    def head(self):
        return self.store.read()['associations'][self.association]['managed_state']['current_generation']

    def generations(self):
        return self.store.read()['associations'][self.association]['managed_state']['generations']

    def prepare(self):
        return prepare_hunt(self.store, self.association, selection(), Path(ENGINE), self.digest, True)

    def candidate(self, j=None, scenario='changed'):
        j = j or self.prepare()
        with patch.dict(os.environ, {'C2_NATIVE_FIXTURE_BEHAVIOR': scenario}):
            result = run_hunt(self.store, j['id'], Path(ENGINE), self.digest, True)
        self.assertEqual(result['state'], 'returned', result['diagnostics'])
        result = reconcile_session(self.store, j['id'])
        self.assertEqual(result['state'], 'candidate', result['diagnostics'])
        return result

    # -- native driver ---------------------------------------------------------
    def native(self, command, *args, policy='fixture', failure=None):
        environment = {k: v for k, v in os.environ.items() if k != 'C2_PROFILE_PROBE'}
        done = subprocess.run([DRIVER, command, str(self.store.directory), PROBE, policy, failure or '-', *args],
                              capture_output=True, timeout=120, env=environment, check=True)
        kind, _, rest = done.stdout.decode().rstrip('\n').partition(' ')
        return kind, (json.loads(rest) if kind == 'ok' and command != 'digest' else rest)

    def reference(self, function, *args):
        try:
            return 'ok', function(self.store, *args)
        except FrontendError as error:
            return 'frontend', str(error)
        except OSError as error:
            return 'oserror', error

    # -- store snapshots and evidence ------------------------------------------
    def backup(self):
        self.backups += 1
        target = self.store.directory.parent / f'backup-{self.backups}'
        shutil.copytree(self.store.directory, target)
        return target

    def restore(self, backup):
        shutil.rmtree(self.store.directory)
        shutil.copytree(backup, self.store.directory)

    def evidence(self, session_id):
        root = self.store.directory
        generations = root / 'generations' / self.association
        listing = sorted(p.name for p in generations.iterdir()) if generations.exists() else None
        acceptance = session_root(self.store, session_id) / 'acceptance.json'
        try:
            journal = read_journal(self.store, session_id)
        except FrontendError as error:
            journal = str(error)
        return {'manifest': root.joinpath('lodge.json').read_bytes(),
                'backup': root.joinpath('lodge.json.bak').read_bytes() if (root / 'lodge.json.bak').exists() else None,
                'generations': listing,
                'snapshots': {name: capture(generations / name) for name in listing or []},
                'receipt': acceptance.read_bytes() if acceptance.exists() else None,
                'journal': journal}

    def normalize(self, value, substitutions):
        text = ordered(value) if not isinstance(value, (bytes, str)) else value
        for old, new in substitutions:
            text = text.replace(old, new) if isinstance(text, str) else text.replace(old.encode(), new.encode())
        return json.loads(text) if not isinstance(value, (bytes, str)) else text

    def assert_same_evidence(self, got, expected, substitutions):
        # Sorted-key encoding places a different UUID at a different position,
        # so byte identity is asserted as: equal decoded values after the narrow
        # substitution, and each side's bytes exactly the canonical encoder output.
        for key in ('manifest', 'receipt'):
            if got[key] is None or expected[key] is None:
                self.assertEqual(got[key], expected[key], key)
                continue
            decoded, reference = json.loads(got[key]), json.loads(expected[key])
            self.assertEqual(encode(decoded), got[key], key)
            self.assertEqual(encode(reference), expected[key], key)
            self.assertEqual(self.normalize(decoded, substitutions), reference, key)
        self.assertEqual(got['backup'], expected['backup'], 'backup')
        listing = None if got['generations'] is None else sorted(self.normalize(n, substitutions) for n in got['generations'])
        self.assertEqual(listing, None if expected['generations'] is None else sorted(expected['generations']))
        self.assertEqual({self.normalize(k, substitutions): v for k, v in got['snapshots'].items()}, expected['snapshots'])
        self.assertEqual(got['journal'], expected['journal'])

    # -- differential operations -----------------------------------------------
    def preview_both(self, j, expected, policy='fixture'):
        before = capture(self.store.directory)
        kind, got = self.native('preview', j['id'], expected, policy=policy)
        self.assertEqual(kind, 'ok', got)
        self.assertEqual(capture(self.store.directory), before)  # read-only: no lock, no evidence
        reference = preview_acceptance(self.store, j['id'], expected)
        self.assertEqual(ordered(got), ordered(reference))
        return got

    def accept_both(self, j, expected, digest=None):
        """Reference on the current state, restored, then native; native state is kept."""
        digest = digest or candidate_digest(j)
        saved = self.backup()
        expected_kind, expected_value = self.reference(accept_candidate, j['id'], expected, digest)
        expected_evidence = self.evidence(j['id'])
        self.restore(saved)
        kind, got = self.native('accept', j['id'], expected, digest)
        got_evidence = self.evidence(j['id'])
        self.assertEqual(kind, expected_kind, got)
        substitutions = []
        if kind == 'ok':
            if got['result'] == 'accepted':
                gid, stamp = got['receipt']['generation_id'], got['receipt']['accepted_at']
                self.assertTrue(valid_id(gid) and gid != expected_value['receipt']['generation_id'])
                self.assertRegex(stamp, ISO)
                self.assertEqual(got['current_generation'], gid)
                substitutions = [(gid, expected_value['receipt']['generation_id']), (stamp, expected_value['receipt']['accepted_at'])]
            self.assertEqual(ordered(self.normalize(got, substitutions)), ordered(expected_value))
        elif kind == 'frontend':
            self.assertEqual(got, expected_value)
        self.assert_same_evidence(got_evidence, expected_evidence, substitutions)
        return kind, got

    def recover_both(self, session_id):
        saved = self.backup()
        expected_kind, expected_value = self.reference(recover_acceptance, session_id)
        expected_evidence = self.evidence(session_id) if valid_id(session_id) else None
        self.restore(saved)
        kind, got = self.native('recover', session_id)
        self.assertEqual((kind, got if kind != 'ok' else ordered(got)),
                         (expected_kind, expected_value if kind != 'ok' else ordered(expected_value)))
        if expected_evidence is not None:
            self.assert_same_evidence(self.evidence(session_id), expected_evidence, [])
        return kind, got

    def assert_blocked(self, j, expected, message=None, policy='fixture'):
        before = self.store.path.read_bytes()
        preview = self.preview_both(j, expected, policy)
        self.assertEqual(preview['status'], 'blocked', preview)
        self.assertFalse(preview['allowed'])
        if message is not None:
            self.assertEqual(preview['diagnostics'][-1]['message'], message)
        kind, got = self.native('accept', j['id'], expected, candidate_digest(j), policy=policy)
        self.assertEqual(kind, 'frontend', got)
        self.assertEqual(got, preview['diagnostics'][-1]['message'])
        self.assertEqual(self.store.path.read_bytes(), before)
        return preview

    # -- tests -----------------------------------------------------------------
    def test_g0_g1_g2_lifecycle_retry_stale_and_equal_bytes(self):
        s1 = self.candidate()
        self.assertEqual(self.native('digest', s1['id']), ('ok', candidate_digest(s1)))
        preview = self.preview_both(s1, self.g0)
        self.assertEqual((preview['status'], preview['allowed']), ('eligible', True))
        self.assertEqual(preview['candidate_sha256'], candidate_digest(s1))
        self.assertEqual(self.recover_both(s1['id'])[1]['result'], 'not-committed')
        kind, accepted1 = self.accept_both(s1, self.g0)
        g1 = accepted1['current_generation']
        self.assertEqual((kind, accepted1['result']), ('ok', 'accepted'))
        self.assertNotEqual(g1, self.g0)
        self.assertEqual(self.head(), g1)  # the reference reads natively written authority
        self.assertEqual(read_journal(self.store, s1['id']), s1)
        data = self.store.read()
        _, g1root, g1members, g1bytes = resolve_generation(self.store, data, data['associations'][self.association])
        self.assertEqual((g1members, g1bytes), capture(session_root(self.store, s1['id']) / 'returned'))
        self.assertNotEqual(g1bytes, self.fixture.original[1])
        self.assertEqual(self.preview_both(s1, self.g0)['status'], 'already-accepted')
        # Two sessions continue from the native generation; one becomes stale.
        s2, s3 = self.prepare(), self.prepare()
        self.assertEqual((s2['pins']['generation_id'], s3['pins']['generation_id']), (g1, g1))
        self.assertEqual(s2['pins']['source_root'], str(g1root))
        s2, s3 = self.candidate(s2), self.candidate(s3)
        kind, accepted2 = self.accept_both(s2, g1)
        g2 = accepted2['current_generation']
        self.assertEqual((kind, self.head()), ('ok', g2))
        self.assertEqual(len(self.generations()), 3)
        scores = [resolve_generation(self.store, self.store.read(), self.store.read()['associations'][self.association], g)[3]['trophy00.sav'][132:136]
                  for g in (self.g0, g1, g2)]
        self.assertEqual(len(set(scores)), 3)  # G0, G1, G2 carry distinct scores
        # Retrying the old G1 acceptance returns already-accepted and leaves G2 head.
        kind, retry = self.accept_both(s1, self.g0)
        self.assertEqual((kind, retry['result'], retry['receipt'], retry['current_generation']),
                         ('ok', 'already-accepted', accepted1['receipt'], g2))
        self.assertEqual(self.head(), g2)
        self.assertEqual(self.accept_both(s1, g1)[0], 'frontend')
        self.assertEqual(self.accept_both(s1, self.g0, digest='0' * 64)[0], 'frontend')
        self.assertEqual(self.accept_both(s2, g1)[1]['receipt'], accepted2['receipt'])
        # A stale G1-predecessor candidate after G2 is rejected under either expectation.
        for expected in (g1, g2):
            self.assert_blocked(s3, expected, 'candidate predecessor is stale or mismatched')
        self.assertEqual(self.head(), g2)
        self.assertEqual(self.recover_both(s3['id'])[1]['result'], 'not-committed')
        # Equal-byte competing sessions: the first is accepted, the second is stale.
        c1, c2 = self.candidate(scenario=''), self.candidate(scenario='')
        self.assertEqual(c1['returned_members'], c2['returned_members'])
        kind, accepted3 = self.accept_both(c1, g2)
        self.assertEqual(kind, 'ok')
        self.assert_blocked(c2, g2, 'candidate predecessor is stale or mismatched')
        self.assertEqual(self.head(), accepted3['current_generation'])
        self.assertEqual(self.recover_both(s1['id'])[1]['receipt'], accepted1['receipt'])
        self.assertEqual(self.recover_both(c1['id'])[1]['current_generation'], accepted3['current_generation'])
        self.assertEqual(self.recover_both(new_id())[1]['result'], 'not-committed')
        self.assertEqual(self.native('recover', 'not-a-uuid'), ('frontend', 'invalid session UUID'))
        self.assertEqual(self.reference(recover_acceptance, 'not-a-uuid'), ('frontend', 'invalid session UUID'))

    def test_failure_matrix_keeps_authority_and_recovers_identically(self):
        j = self.candidate()
        before = self.store.path.read_bytes()
        store_name = self.store.directory.name
        not_committed = ['replace:.pending-', 'replace:lodge.json.bak', 'replace:lodge.json']
        for failure in not_committed:
            with self.subTest(failure=failure):
                kind, got = self.native('accept', j['id'], self.g0, candidate_digest(j), failure=failure)
                self.assertEqual(kind, 'oserror', got)
                self.assertIn('injected failure ' + failure, got)
                self.assertEqual(self.store.path.read_bytes(), before)
                self.assertEqual(self.head(), self.g0)
                self.assertEqual(self.recover_both(j['id'])[1]['result'], 'not-committed')
                self.assertEqual(self.preview_both(j, self.g0)['status'], 'eligible')
        orphans = sorted(p.name for p in (self.store.directory / 'generations' / self.association).iterdir())
        self.assertEqual(len(orphans), 3)  # partial/unreferenced evidence retained, never selected
        self.assertTrue(any(name.startswith('.pending-') for name in orphans))
        data = self.store.read()
        self.assertEqual(resolve_generation(self.store, data, data['associations'][self.association])[0]['id'], self.g0)
        kind, accepted = self.accept_both(j, self.g0)
        self.assertEqual(kind, 'ok')
        g1 = accepted['current_generation']
        self.assertNotIn(g1, orphans)
        self.assertTrue(all((self.store.directory / 'generations' / self.association / name).exists() for name in orphans))
        # After the commit: authority is never rolled back; recovery completes the receipt.
        committed = ['replace:acceptance.json', 'temp_create:{session}']
        if POSIX:
            committed.append(f'directory_fsync:{store_name}:2')
        head = g1
        for spec in committed:
            with self.subTest(failure=spec):
                c = self.candidate()
                failure = spec.format(session=c['id'])
                receipt = session_root(self.store, c['id']) / 'acceptance.json'
                kind, got = self.native('accept', c['id'], head, candidate_digest(c), failure=failure)
                self.assertEqual(kind, 'oserror', got)
                new_head = self.head()
                self.assertNotEqual(new_head, head)
                self.assertFalse(receipt.exists())
                self.assertEqual(self.store.read()['associations'][self.association]['managed_state']['receipts'][c['id']]['generation_id'], new_head)
                kind, recovered = self.recover_both(c['id'])
                self.assertEqual((kind, recovered['result'], recovered['current_generation']), ('ok', 'already-accepted', new_head))
                self.assertTrue(receipt.exists())
                kind, retry = self.accept_both(c, head)
                self.assertEqual((kind, retry['result'], retry['receipt']), ('ok', 'already-accepted', recovered['receipt']))
                self.assertEqual(self.head(), new_head)
                head = new_head
        self.assertEqual(self.prepare()['pins']['generation_id'], head)

    def test_corrupt_or_missing_head_never_falls_back(self):
        s1 = self.candidate()
        kind, accepted = self.accept_both(s1, self.g0)
        self.assertEqual(kind, 'ok')
        g1 = accepted['current_generation']
        data = self.store.read()
        _, root, _, _ = resolve_generation(self.store, data, data['associations'][self.association])
        file = root / 'trophy00.sav'
        original = file.read_bytes()
        message = 'managed generation snapshot is missing, changed or unsafe; no fallback'
        for blob in (None, b'bad'):
            if blob is None:
                file.unlink()
            else:
                file.write_bytes(blob)
            self.assert_blocked(s1, self.g0, message)
            self.assertEqual(self.native('recover', s1['id']), ('frontend', message))
            self.assertEqual(self.reference(recover_acceptance, s1['id']), ('frontend', message))
            self.assertEqual(self.native('accept', s1['id'], g1, candidate_digest(s1))[0], 'frontend')
            self.assertEqual(self.head(), g1)
            file.write_bytes(original)
        self.assertEqual(self.recover_both(s1['id'])[1]['receipt'], accepted['receipt'])

    def test_receipt_copy_tampering_and_lock_are_refused(self):
        j = self.candidate()
        kind, accepted = self.accept_both(j, self.g0)
        self.assertEqual(kind, 'ok')
        path = session_root(self.store, j['id']) / 'acceptance.json'
        good = path.read_bytes()
        path.write_text('{}')
        message = 'receipt copy differs; retained for review'
        self.assertEqual(self.native('recover', j['id']), ('frontend', message))
        self.assertEqual(self.reference(recover_acceptance, j['id']), ('frontend', message))
        self.assertEqual(self.native('accept', j['id'], self.g0, candidate_digest(j)), ('frontend', message))
        self.assertEqual(self.head(), accepted['current_generation'])
        path.write_bytes(good)
        self.assertEqual(self.recover_both(j['id'])[1]['receipt'], accepted['receipt'])
        c = self.candidate()
        lock = self.store.directory / 'lodge.lock'
        lock.write_bytes(b'{}')
        try:
            self.assertEqual(self.preview_both(c, accepted['current_generation'])['status'], 'eligible')
            kind, got = self.native('accept', c['id'], accepted['current_generation'], candidate_digest(c))
            self.assertEqual((kind, got), self.reference(accept_candidate, c['id'], accepted['current_generation'], candidate_digest(c)))
            self.assertEqual(kind, 'frontend')
            self.assertEqual(self.native('recover', c['id']), self.reference(recover_acceptance, c['id']))
        finally:
            lock.unlink()
        self.assertEqual(self.accept_both(c, accepted['current_generation'])[0], 'ok')

    def test_malformed_ineligible_and_corrupted_candidates_block_identically(self):
        original = self.candidate()
        root = session_root(self.store, original['id'])
        mutations = [lambda j: j['pins'].update(association_id=new_id()),
                     lambda j: j['pins'].update(hunter_id=new_id()), lambda j: j['pins'].update(instance_id=new_id()),
                     lambda j: j['pins'].update(native_slot=1), lambda j: j['pins']['revision'].update(sha256='0' * 64),
                     lambda j: j['pins'].update(adapter='other'), lambda j: j['pins']['hunt_policy'].update(adapter='other'),
                     lambda j: j['pins'].update(generation_id=new_id()),
                     lambda j: j['process'].update(exit_code=3), lambda j: j['process'].update(exit_code=False),
                     lambda j: j['process'].update(stop_reason='cancelled'), lambda j: j['process'].update(pid=None),
                     lambda j: j['process'].pop('returned_at'), lambda j: j.update(process=[1]),
                     lambda j: j.update(process=None), lambda j: j.pop('reconciliation'),
                     lambda j: j.update(reconciliation={'status': 'clean-candidate'}),
                     lambda j: j['capabilities'].update(engine_process_executed=False),
                     lambda j: j['capabilities'].update(engine_process_executed=1),
                     lambda j: j['capabilities'].update(returned_native_state_readable='unknown'),
                     lambda j: j['capabilities'].update(engine_contract={'version': 1}),
                     lambda j: j['reconciliation']['observation'].update(codec_inspection='unavailable'),
                     lambda j: j['reconciliation'].update(changed_members=None),
                     lambda j: j.update(returned_observation=None), lambda j: j.update(returned_members=[]),
                     lambda j: j.pop('return_capture'),
                     lambda j: j['returned_observation']['trophy00.sav'].update(registration=7),
                     lambda j: j['execution']['argv'].append('-debug'),
                     lambda j: j['execution']['contract'].update(version=True),
                     lambda j: j['execution'].update(shell=0), lambda j: j['execution'].update(timeout_seconds=True),
                     lambda j: j['logs']['stdout'].update(error='lost log'),
                     lambda j: j['logs']['stderr'].update(total_bytes=False),
                     lambda j: j['logs']['stderr'].pop('error'), lambda j: j['logs']['stdout'].update(truncated=True),
                     lambda j: j['logs']['stdout'].update(retained_bytes=1), lambda j: j.update(logs=[]),
                     lambda j: j['logs'].pop('stdout'), lambda j: j.update(launched_at='later'),
                     lambda j: j['pins'].pop('hunt_policy'), lambda j: j['pins'].update(hunt_policy=None)]
        for mutate in mutations:
            j = copy.deepcopy(original)
            mutate(j)
            (root / 'journal.json').write_bytes(encode(j))
            with self.subTest(mutate=mutate):
                self.assert_blocked(j, self.g0)
        for version, kind in [(1, 'controlled-synthetic'), (2, 'experimental-native-observer-v1'),
                              (3, 'experimental-native-hunt-v1'), (99, 'unknown')]:
            j = copy.deepcopy(original)
            j.update(schema_version=version)
            j['execution']['kind'] = kind
            (root / 'journal.json').write_bytes(encode(j))
            with self.subTest(version=version):
                self.assert_blocked(j, self.g0)
        (root / 'journal.json').write_bytes(encode(original))
        # Wrong expected generation formats and candidate digests.
        for expected in ('', 'not-a-uuid', new_id(), self.g0.upper()):
            self.assert_blocked(original, expected)
        self.assertEqual(self.native('accept', original['id'], self.g0, 'ff' * 32), ('frontend', 'candidate differs from explicit preview'))
        # Evidence corruption in every retained domain.
        for domain in ('baseline', 'returned', 'work/state'):
            file = root / domain / 'trophy00.sav'
            blob = file.read_bytes()
            for corruption in ('change', 'missing', 'short', 'extra'):
                with self.subTest(domain=domain, corruption=corruption):
                    extra = root / domain / 'unexpected'
                    try:
                        if corruption == 'change':
                            file.write_bytes(save_bytes(score=52))
                        elif corruption == 'short':
                            file.write_bytes(blob[:-1])
                        elif corruption == 'missing':
                            file.unlink()
                        else:
                            extra.write_bytes(b'unknown')
                        self.assert_blocked(original, self.g0)
                    finally:
                        if extra.exists():
                            extra.unlink()
                        file.write_bytes(blob)
        (root / 'work/output/extra.txt').write_bytes(b'x')
        try:
            self.assert_blocked(original, self.g0, 'workspace/config/output requires review')
        finally:
            (root / 'work/output/extra.txt').unlink()
        self.assertEqual(self.preview_both(original, self.g0)['status'], 'eligible')
        # Ineligible lifecycles: quarantined, failed, interrupted, prepared.
        returned = self.prepare()
        with patch.dict(os.environ, {'C2_NATIVE_FIXTURE_BEHAVIOR': 'nonzero'}):
            run_hunt(self.store, returned['id'], Path(ENGINE), self.digest, True)
        quarantined = reconcile_session(self.store, returned['id'])
        self.assertEqual(quarantined['state'], 'quarantined')
        self.assert_blocked(quarantined, self.g0, 'candidate has unresolved failure/review conditions')
        failed = self.prepare()
        transition(session_root(self.store, failed['id']), failed, 'failed')
        self.assert_blocked(failed, self.g0)
        launching = self.prepare()
        transition(session_root(self.store, launching['id']), launching, 'launching')
        interrupted = recover_session(self.store, launching['id'])
        self.assertEqual(interrupted['state'], 'interrupted')
        self.assert_blocked(interrupted, self.g0)
        self.assertEqual(self.recover_both(interrupted['id'])[1]['result'], 'not-committed')
        self.assert_blocked(self.prepare(), self.g0)
        self.assertEqual(self.head(), self.g0)

    def test_production_policy_refuses_fixture_identically(self):
        j = self.candidate()
        self.policy.stop()
        try:
            preview = self.assert_blocked(j, self.g0, policy='production')
            self.assertIn('unpinned content', preview['diagnostics'][-1]['message'])
        finally:
            self.policy.start()
        self.assertEqual(self.preview_both(j, self.g0)['status'], 'eligible')

    def test_missing_journal_and_manifest_conditions(self):
        j = self.candidate()
        (session_root(self.store, j['id']) / 'journal.json').unlink()
        kind, got = self.native('preview', j['id'], self.g0)
        reference = preview_acceptance(self.store, j['id'], self.g0)
        self.assertEqual((kind, got['status'], list(got)), ('ok', 'blocked', list(reference)))
        self.assertTrue(got['diagnostics'][0]['message'].startswith('cannot read session journal'))
        self.assertTrue(reference['diagnostics'][0]['message'].startswith('cannot read session journal'))
        self.assertEqual(self.native('accept', j['id'], self.g0, candidate_digest(j))[0], 'frontend')
        self.assertEqual(self.recover_both(j['id'])[1]['result'], 'not-committed')


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
