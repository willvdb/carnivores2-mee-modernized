"""Explicit adoption of one fully revalidated generation-pinned native candidate.

No engine query/launch, no native re-encoding, no fallback or force acceptance.
The snapshot is published before lodge.json atomically commits head + receipt.
"""
import copy
import hashlib
import os

from . import native_continuation
from .genesis_hunt import POLICY_ID
from .managed_state import (AUTHORITY, provenance, resolve_generation, sync_directory)
from .native_session import CAPABILITY, supported_contract, workspace_findings
from .session_io import (capture, encode, inspect_bytes, read_journal, safe_path,
                         session_root, write_blobs)
from .sessions import executable_evidence
from .store import FrontendError, atomic_write, new_id, now, valid_id, validate


def candidate_digest(journal):
    return hashlib.sha256(encode(journal)).hexdigest()


def _require(condition, reason):
    if not condition: raise FrontendError(reason)


def validate_candidate(store, data, journal, expected_generation, probe=None):
    """Reconstruct policy and fresh state; editable candidate status alone grants nothing."""
    try:
        return _validate_candidate(store, data, journal, expected_generation, probe)
    except (KeyError, TypeError, AttributeError) as error:
        raise FrontendError('incomplete or malformed candidate evidence') from error


def _validate_candidate(store, data, j, expected, probe):
    _require(data['schema_version'] == 2, 'explicit managed-state schema upgrade required')
    _require(j['schema_version'] == 4 and j['execution']['kind'] == native_continuation.KIND,
             'only generation-pinned native normal-hunt candidates are eligible')
    _require(j['state'] == 'candidate' and not j['diagnostics'], 'candidate has unresolved failure/review conditions')
    pins = j['pins']; a = data['associations'].get(pins['association_id'])
    _require(a is not None and a['authority'] == AUTHORITY, 'association is not managed history')
    _require(valid_id(expected) and expected == a['managed_state']['current_generation']
             and expected == pins['generation_id'], 'candidate predecessor is stale or mismatched')
    process = j.get('process') or {}
    _require(type(process.get('exit_code')) is int and process['exit_code'] == 0
             and process.get('stop_reason') == 'exited'
             and type(process.get('pid')) is int and process['pid'] > 0
             and process.get('started_at') == j.get('launched_at')
             and isinstance(process.get('started_at'), str) and bool(process['started_at'])
             and process.get('returned_at') == j.get('returned_at')
             and isinstance(process.get('returned_at'), str) and bool(process['returned_at']), 'no clean owned process return')
    _require([e['state'] for e in j['transitions']] ==
             ['prepared','launching','running','returned','inspecting','candidate'],
             'incomplete native lifecycle history')
    capabilities = j['capabilities']
    _require(capabilities.get('engine_process_executed') is True
             and capabilities.get('native_hunt_lifecycle') == 'completed'
             and capabilities.get('synthetic_child_lifecycle') == 'not-executed'
             and capabilities.get('returned_native_state_readable') == 'yes'
             and supported_contract(capabilities.get('engine_contract')), 'incomplete native capabilities')
    rec = j['reconciliation']
    _require(rec.get('status') == 'clean-candidate' and rec.get('authority') == AUTHORITY
             and rec.get('promotion') == 'explicit-only' and rec.get('comparison_status') == 'complete'
             and rec.get('observation') == {'inventory':'complete','byte_capture':'complete',
                 'retained_capture':'verified','codec_inspection':'complete'}, 'incomplete saved observations')
    _require(pins.get('adapter') == POLICY_ID and pins['hunt_policy'].get('adapter') == POLICY_ID,
             'unsupported native policy provenance')
    # Fresh content/association/profile/codec/selection checks; no engine query.
    current, _ = native_continuation.native_pins(store, a['id'], pins['selection'], probe, pins['codec'])
    _require(current == pins, 'association, hunter, slot, content, policy or source pins changed')
    root = session_root(store, j['id']); spec = j['execution']
    evidence = executable_evidence(spec['executable']['path'])
    _require(evidence == spec['executable'], 'pinned engine build changed')
    _require(supported_contract(spec.get('contract')) and spec.get('shell') is False
             and native_continuation.execution_spec(root,pins,evidence,CAPABILITY,spec.get('timeout_seconds')) == spec,
             'native execution evidence changed')
    _require(not workspace_findings(root, returning=True), 'workspace/config/output requires review')
    baseline, baseline_blobs = capture(root/'baseline')
    members, blobs = capture(root/'returned')
    work, work_blobs = capture(root/'work/state')
    _require(baseline == pins['source_members'] == j['baseline_members'], 'immutable baseline changed')
    allowed = {f"trophy{pins['native_slot']:02d}.sav", f"trophy{pins['native_slot']:02d}.sab"}
    _require(set(blobs) == allowed and len(members) == 2 and all(m['type']=='file' for m in members),
             'returned native membership is incomplete or unsafe')
    _require(members == j.get('returned_members') == j.get('return_capture') == work and blobs == work_blobs,
             'candidate bytes changed since durable observation')
    before, errors_before = inspect_bytes(baseline_blobs, pins['native_slot'], pins['codec']['path'])
    after, errors_after = inspect_bytes(blobs, pins['native_slot'], pins['codec']['path'])
    _require(not errors_before and not errors_after and set(before) == set(after) == allowed,
             'native codecs/registration require review')
    _require(before == pins['source_observation'] and after == j.get('returned_observation'),
             'saved native observations disagree with fresh bytes')
    changed = sorted(m['path'] for m in members if m not in baseline)
    _require(changed == rec.get('changed_members'), 'saved native comparison is incomplete')
    logs, _ = capture(root/'logs')
    _require({m['path'] for m in logs} == {'stdout.log','stderr.log'} and len(logs) == 2,
             'owned process log evidence is incomplete')
    for member in logs:
        log = j['logs'][member['path'].split('.')[0]]
        from .session_runner import LOG_LIMIT
        _require(type(log.get('total_bytes')) is int and log['total_bytes'] >= 0
                 and type(log.get('retained_bytes')) is int
                 and log['retained_bytes'] == min(log['total_bytes'], LOG_LIMIT)
                 and type(log.get('truncated')) is bool
                 and log['truncated'] == (log['total_bytes'] > LOG_LIMIT)
                 and member['type'] == 'file' and member['size'] == log['retained_bytes']
                 and log['path'] == 'logs/'+member['path'] and 'error' in log and log['error'] is None,
                 'owned process log capture requires review')
    return a, members, blobs


def _receipt(data, session_id):
    _require(valid_id(session_id), 'invalid session UUID')
    found = [(a, a['managed_state']['receipts'][session_id]) for a in data['associations'].values()
             if a.get('authority') == AUTHORITY and session_id in a['managed_state']['receipts']]
    _require(len(found) <= 1, 'ambiguous committed session receipt')
    return found[0] if found else None


def preview_acceptance(store, identity, expected_generation, probe=None):
    """Read-only preview; acceptance repeats every check under the writer lock."""
    result = {'kind':'acceptance-preview-v1', 'session_id':identity,
              'expected_generation':expected_generation, 'allowed':False, 'diagnostics':[]}
    try:
        data = store.read(); j = read_journal(store, identity); pins = j['pins']
        result.update(association_id=pins['association_id'],hunter_id=pins['hunter_id'],
            instance_id=pins['instance_id'],native_slot=pins['native_slot'],
            revision=pins['revision'],policy=pins.get('adapter'),execution=j['execution'],
            baseline_members=j['baseline_members'],returned_members=j.get('returned_members'),
            observed_changes={'before':pins.get('source_observation'), 'after':j.get('returned_observation'),
                              'changed_members':j.get('reconciliation',{}).get('changed_members')},
            session_diagnostics=j['diagnostics'],candidate_sha256=candidate_digest(j))
        committed = _receipt(data,identity)
        if committed:
            a, receipt = committed
            resolve_generation(store,data,a)
            result.update(receipt=receipt, status='already-accepted')
            return result
        validate_candidate(store,data,j,expected_generation,probe)
        result.update(allowed=True,status='eligible')
    except (FrontendError, OSError, KeyError, TypeError, ValueError) as error:
        result['diagnostics'].append({'code':'acceptance-blocked','message':str(error)})
        result['status']='blocked'
    return result


def complete_receipt(store, receipt):
    """A convenience copy only. Its absence never rolls back manifest authority."""
    path = safe_path(session_root(store,receipt['session_id'])/'acceptance.json')
    content = encode(receipt)
    if path.exists():
        _require(path.read_bytes() == content, 'receipt copy differs; retained for review')
    else:
        atomic_write(path,content)


def _existing(store, data, found, expected, digest):
    a, receipt = found
    _require(receipt['predecessor'] == expected and receipt['candidate_sha256'] == digest,
             'retry does not identify the committed candidate/predecessor')
    resolve_generation(store,data,a)  # A corrupt current head never silently rolls back.
    resolve_generation(store,data,a,receipt['generation_id'])
    complete_receipt(store,receipt)
    return {'result':'already-accepted', 'receipt':copy.deepcopy(receipt),
            'current_generation':a['managed_state']['current_generation']}


def accept_candidate(store, identity, expected_generation, expected_candidate_sha256, probe=None):
    safe_path(store.directory); safe_path(store.path)
    with store.lock():
        data = store.read(); found = _receipt(data,identity)
        if found:
            return _existing(store,data,found,expected_generation,expected_candidate_sha256)
        j = read_journal(store,identity)
        _require(candidate_digest(j) == expected_candidate_sha256, 'candidate differs from explicit preview')
        a, members, blobs = validate_candidate(store,data,j,expected_generation,probe)
        gid = new_id(); created = now(); parent = safe_path(store.directory/'generations'/a['id'])
        parent.mkdir(parents=True,exist_ok=True)
        stage = safe_path(parent/('.pending-'+gid)); final = safe_path(parent/gid)
        stage.mkdir(exist_ok=False)
        write_blobs(stage,blobs)
        _require(capture(stage) == (members,blobs), 'staged native bytes did not verify')
        _require(not final.exists(), 'generation identity already exists')
        os.rename(stage,final)
        # The verified snapshot and all newly created ancestors precede the head.
        for directory in (final,parent,parent.parent,store.directory): sync_directory(directory)
        _require(capture(final) == (members,blobs), 'published snapshot did not verify')
        receipt = {'schema_version':1,'association_id':a['id'],'session_id':identity,
            'generation_id':gid,'predecessor':expected_generation,'members':members,
            'revision':copy.deepcopy(j['pins']['revision']),'policy':POLICY_ID,
            'execution':copy.deepcopy(j['execution']), 'accepted_at':created,
            'candidate_sha256':expected_candidate_sha256,'acceptance':'explicit'}
        h = a['managed_state']; previous = h['generations'][expected_generation]
        h['generations'][gid] = {'id':gid,'sequence':previous['sequence']+1,
            'predecessor':expected_generation,'source_session':identity,'kind':'accepted-native-hunt',
            'created_at':created,'members':members,'provenance':provenance(a),
            'snapshot':f"generations/{a['id']}/{gid}", 'policy':POLICY_ID,
            'revision':copy.deepcopy(j['pins']['revision']),'execution':copy.deepcopy(j['execution'])}
        h['receipts'][identity] = receipt; h['current_generation'] = gid
        validate(data)
        atomic_write(safe_path(store.path.with_suffix('.json.bak')),store.path.read_bytes())
        # THE COMMIT POINT: new head and authoritative receipt become visible together.
        atomic_write(store.path,encode(data))
        complete_receipt(store,receipt)
        return {'result':'accepted','receipt':receipt,'current_generation':gid}


def recover_acceptance(store, identity):
    """Complete only an already committed receipt; never promote orphan snapshots."""
    safe_path(store.directory); safe_path(store.path)
    with store.lock():
        data = store.read(); found = _receipt(data,identity)
        if not found:
            return {'result':'not-committed','session_id':identity,
                    'action':'evidence-retained-no-promotion'}
        receipt = found[1]
        return _existing(store,data,found,receipt['predecessor'],receipt['candidate_sha256'])
