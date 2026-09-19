"""Durable return candidates. Never promotes or mutates association authority."""
from .session_io import (capture, inspect_bytes, persist, read_journal, safe_path,
                         session_root, transition, write_blobs)
from .sessions import codec_evidence, snapshot_pins
from .store import FrontendError, now


def reconcile_locked(store, root, journal, probe=None):
    if journal['state'] == 'returned':
        transition(root, journal, 'inspecting', inspecting_at=now())
    if journal['state'] != 'inspecting':
        return journal
    diagnostics = list(journal['diagnostics'])
    pins = journal['pins']
    process = journal.get('process') or {}
    if type(process.get('exit_code')) is not int or process.get('stop_reason') != 'exited' or process['exit_code'] != 0:
        diagnostics.append({'code': 'unclean-process-return'})
    entries, blobs, decoded = [], {}, {}
    try:
        if journal['schema_version'] == 2:
            from .native_observer import CAPABILITY, execution_spec, native_pins, supported_contract
            current, _ = native_pins(store, pins['association_id'], pins['selection'], probe, pins['codec'])
            from .sessions import executable_evidence
            spec = journal['execution']
            evidence = executable_evidence(spec['executable']['path'])
            if evidence != spec['executable']:
                diagnostics.append({'code': 'selected-engine-changed-on-return'})
            # Recovery validates evidence without executing any engine query.
            expected = execution_spec(root, pins, evidence, CAPABILITY, spec.get('timeout_seconds'))
            if expected != spec or not supported_contract(spec.get('contract')) or spec.get('shell') is not False:
                diagnostics.append({'code': 'native-execution-evidence-changed-on-return'})
        else:
            current, _ = snapshot_pins(store, pins['association_id'], pins['selection'], probe, pins['codec'])
        if current != pins:
            diagnostics.append({'code': 'pinned-evidence-changed-on-return'})
        baseline, _ = capture(root / 'baseline')
        if baseline != pins['source_members'] or baseline != journal['baseline_members']:
            diagnostics.append({'code': 'baseline-changed'})
    except (FrontendError, OSError, KeyError, TypeError, ValueError) as error:
        diagnostics.append({'code': 'source-review-required', 'message': str(error)})
    try:
        safe_path(root / 'work')
        if journal['schema_version'] == 2:
            from .native_observer import validate_workspace
            validate_workspace(root, journal, returning=True)
        elif {p.name for p in (root / 'work').iterdir()} != {'state'}:
            diagnostics.append({'code': 'unexpected-workspace-entry',
                                'message': 'All extra workspace entries retained in work.'})
        entries, blobs = capture(root / 'work/state')
        baseline_names = {e['path'] for e in journal['baseline_members']}
        actual_names = {e['path'] for e in entries}
        for name in sorted(baseline_names - actual_names):
            diagnostics.append({'code': 'missing-state-member', 'path': name})
        for name in sorted(actual_names - baseline_names):
            diagnostics.append({'code': 'unexpected-state-member', 'path': name})
        for entry in entries:
            if entry['type'] != 'file':
                diagnostics.append({'code': 'unsafe-state-entry', 'path': entry['path'],
                                    'type': entry['type'], 'retained_in': 'work/state'})
        if 'return_capture' not in journal:
            # Freeze membership/hash observations before copying. Recovery cannot
            # silently replace an earlier capture with later different bytes.
            journal['return_capture'] = entries
            persist(root, journal)
        if journal['return_capture'] != entries:
            raise FrontendError('returned state changed since durable capture; original capture retained')
        destination = safe_path(root / 'returned')
        if destination.exists():
            existing_entries, existing_blobs = capture(destination)
            if (any(e['type'] not in ('file', 'directory') for e in existing_entries)
                    or any(name not in blobs or blobs[name] != blob for name, blob in existing_blobs.items())):
                raise FrontendError('existing returned evidence changed; never overwritten')
        write_blobs(destination, blobs)
        copied, copied_blobs = capture(destination)
        if copied_blobs != blobs or any(e['type'] not in ('file', 'directory') for e in copied):
            raise FrontendError('returned evidence copy did not verify')
        codec = codec_evidence(probe)
        if codec != pins['codec']:
            raise FrontendError('codec helper evidence changed')
        decoded, findings = inspect_bytes(copied_blobs, pins['native_slot'], codec['path'])
        diagnostics.extend(findings)
    except (FrontendError, OSError, KeyError, TypeError, ValueError) as error:
        diagnostics.append({'code': 'return-review-required', 'message': str(error)})
    clean = not diagnostics
    expected = {e['path']: e.get('sha256') for e in journal['baseline_members']}
    actual = {e['path']: e.get('sha256') for e in entries}
    readable = bool(decoded) and all(d.get('codec_roundtrip_exact') for d in decoded.values())
    # A readable SAV alone must not hide a missing expected SAB.
    readable = readable and set(decoded) == set(expected)
    journal['capabilities']['returned_native_state_readable'] = 'yes' if readable else 'no'
    journal['diagnostics'] = diagnostics
    transition(root, journal, 'candidate' if clean else 'quarantined', reconciled_at=now(),
               returned_members=entries, returned_observation=decoded,
               reconciliation={'status': 'clean-candidate' if clean else 'review-required',
                   'authority': 'original-managed-snapshot', 'promotion': 'deferred',
                   'changed_members': sorted(name for name in expected.keys() | actual.keys()
                                             if expected.get(name) != actual.get(name)),
                   'pair_atomicity': 'unverified', 'progression_semantics': 'unverified'})
    return journal


def reconcile_session(store, identity, probe=None):
    with store.lock():
        root = session_root(store, identity)
        journal = read_journal(store, identity)
        if journal['state'] not in ('returned', 'inspecting', 'candidate', 'quarantined'):
            raise FrontendError('only a durably returned session may be reconciled')
        return reconcile_locked(store, root, journal, probe)
