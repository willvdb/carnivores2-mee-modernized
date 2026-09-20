"""Manifest-v2 immutable generation history. No native file conversion or launch.

lodge.json alone publishes current_generation and authoritative receipts together.
Snapshot directories and per-session receipt copies never select the active state.
"""
import copy
import hashlib
import json
import os
from pathlib import Path
import re

from .store import FrontendError, atomic_write, new_id, now, valid_id

AUTHORITY = 'managed-state-history'
UPGRADE_BACKUP = 'lodge.schema-1.backup.json'


def members_from_import(association):
    return sorted([{'path': f['path'], 'size': f['size'], 'sha256': f['sha256'], 'type': 'file'}
                   for f in association['files']], key=lambda f: f['path'])


def provenance(association):
    return {key: copy.deepcopy(association[key]) for key in
            ('id', 'hunter_id', 'instance_id', 'filename_slot', 'state_key', 'origin', 'revision')}


def initialize_history(association):
    if 'managed_state' in association:
        raise FrontendError('reserved managed_state metadata already exists; explicit review required')
    identity = new_id()
    association['authority'] = AUTHORITY
    association['managed_state'] = {'schema_version': 1, 'current_generation': identity,
        'generations': {identity: {'id': identity, 'sequence': 0, 'predecessor': None,
            'source_session': None, 'kind': 'original-import', 'created_at': now(),
            'members': members_from_import(association), 'provenance': provenance(association),
            'snapshot': f"snapshots/{association['id']}"}}, 'receipts': {}}


def _members_valid(members):
    if not isinstance(members, list) or not members:
        return False
    names = set()
    for m in members:
        if not isinstance(m, dict): return False
        name = m.get('path')
        if (not isinstance(name, str) or not name or Path(name).is_absolute()
                or '..' in Path(name).parts or any(c in name for c in '\\:\x00')
                or name.casefold() in names or m.get('type') != 'file'
                or type(m.get('size')) is not int or m['size'] < 0
                or not isinstance(m.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', m['sha256'])):
            return False
        names.add(name.casefold())
    return members == sorted(members, key=lambda m: m['path'])


def validate_history(association):
    try:
        _validate_history(association)
    except (KeyError, TypeError, AttributeError) as error:
        raise FrontendError('malformed managed-state metadata') from error


def _execution_valid(spec, slot):
    # Validate historical evidence structurally without requiring the old binary
    # to exist and without executing a capability query.
    from .native_session import CONFIG, supported_contract
    from .genesis_hunt import SCORE_MODIFIERS
    if not isinstance(spec, dict): return False
    e = spec.get('executable'); argv = spec.get('argv')
    if (spec.get('kind') != 'managed-native-hunt-v1' or not isinstance(e, dict)
            or not isinstance(e.get('path'), str) or not e['path'] or '\x00' in e['path']
            or not isinstance(e.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', e['sha256'])
            or not supported_contract(spec.get('contract')) or spec.get('shell') is not False
            or type(spec.get('timeout_seconds')) not in (int, float) or not 30 <= spec['timeout_seconds'] <= 3600
            or spec.get('config_sha256') != hashlib.sha256(CONFIG).hexdigest()
            or not isinstance(spec.get('cwd'), str) or not spec['cwd'] or '\x00' in spec['cwd']
            or not isinstance(argv, list) or len(argv) != 11
            or any(not isinstance(v, str) or not v or '\x00' in v for v in argv)):
        return False
    return (argv[:3] == [e['path'], '--session-contract=1', f'--session-slot={slot}']
            and all(argv[i].startswith(prefix) and len(argv[i]) > len(prefix)
                    for i, prefix in [(3,'--session-root='),(4,'--session-source='),(5,'--session-baseline=')])
            and re.fullmatch(r'prj=huntdat/areas/(area[1-8]|external)', argv[6])
            and argv[7] in {f'din={1<<i}' for i in range(9)}
            and argv[8] in {f'wep={1<<i}' for i in range(8)}
            and argv[9] in ('dtm=0','dtm=1','dtm=2') and argv[10] == SCORE_MODIFIERS)


def _validate_history(association):
    h = association.get('managed_state')
    if (not isinstance(h, dict) or type(h.get('schema_version')) is not int or h['schema_version'] != 1
            or not isinstance(h.get('generations'), dict) or not isinstance(h.get('receipts'), dict)
            or not valid_id(h.get('current_generation')) or h['current_generation'] not in h['generations']):
        raise FrontendError('invalid managed-state history/head')
    generations = h['generations']
    chain, seen, current = [], set(), h['current_generation']
    while current is not None:
        if not valid_id(current) or current in seen or current not in generations:
            raise FrontendError('invalid generation ancestry')
        seen.add(current); g = generations[current]
        if (not isinstance(g, dict) or not {'id','sequence','predecessor','source_session','kind',
                'created_at','members','provenance','snapshot'} <= g.keys() or g.get('id') != current or not _members_valid(g.get('members'))
                or g.get('provenance') != provenance(association)
                or not isinstance(g.get('created_at'), str) or not g['created_at']
                or type(g.get('sequence')) is not int):
            raise FrontendError('invalid generation record/provenance')
        chain.append(g); current = g.get('predecessor')
    if seen != set(generations):
        raise FrontendError('unconnected generation metadata; no guessed head')
    chain.reverse()
    sessions = set()
    for sequence, g in enumerate(chain):
        if g['sequence'] != sequence:
            raise FrontendError('noncontiguous generation history')
        if sequence == 0:
            if (g.get('kind') != 'original-import' or g.get('source_session') is not None
                    or g.get('snapshot') != f"snapshots/{association['id']}"
                    or g['members'] != members_from_import(association)):
                raise FrontendError('generation zero differs from original import provenance')
        else:
            sid = g.get('source_session')
            receipt = h['receipts'].get(sid) if isinstance(sid, str) else None
            if (not valid_id(sid) or sid in sessions or g.get('kind') != 'accepted-native-hunt'
                    or g.get('snapshot') != f"generations/{association['id']}/{g['id']}"
                    or not isinstance(receipt, dict) or receipt.get('schema_version') != 1
                    or type(receipt.get('schema_version')) is not int
                    or receipt.get('association_id') != association['id'] or receipt.get('session_id') != sid
                    or receipt.get('generation_id') != g['id'] or receipt.get('predecessor') != g['predecessor']
                    or receipt.get('members') != g['members'] or receipt.get('accepted_at') != g['created_at']
                    or receipt.get('policy') != 'genesis-current-mee-hunt-v1'
                    or g.get('policy') != receipt.get('policy')
                    or not _execution_valid(g.get('execution'), association['filename_slot'])
                    or g.get('execution') != receipt.get('execution')
                    or g.get('revision') != association['revision']
                    or receipt.get('revision') != association['revision']
                    or not isinstance(receipt.get('candidate_sha256'), str)
                    or not re.fullmatch('[0-9a-f]{64}', receipt['candidate_sha256'])
                    or receipt.get('acceptance') != 'explicit'):
                raise FrontendError('invalid authoritative acceptance receipt')
            sessions.add(sid)
    if sessions != set(h['receipts']):
        raise FrontendError('orphan acceptance receipt')


def sync_directory(path):
    from .session_io import safe_path
    safe_path(path)
    if os.name == 'posix':
        fd = os.open(path, os.O_RDONLY)
        try: os.fsync(fd)
        finally: os.close(fd)


def resolve_generation(store, data, association, identity=None):
    from .session_io import capture, safe_path
    if data['schema_version'] != 2 or association.get('authority') != AUTHORITY:
        raise FrontendError('explicit managed-state schema upgrade required')
    validate_history(association)
    h = association['managed_state']
    identity = h['current_generation'] if identity is None else identity
    if not valid_id(identity) or identity not in h['generations']:
        raise FrontendError('unknown managed-state generation')
    g = h['generations'][identity]
    # snapshot spelling was checked against IDs/kind above; no arbitrary locator.
    root = safe_path(store.directory / g['snapshot'])
    entries, blobs = capture(root)
    if entries != g['members']:
        raise FrontendError('managed generation snapshot is missing, changed or unsafe; no fallback')
    return copy.deepcopy(g), root, entries, blobs


def upgrade_store(store):
    from .session_io import capture, encode, safe_path
    from .store import read_manifest, validate
    safe_path(store.directory); safe_path(store.path)
    with store.lock():
        data = store.read()
        if data['schema_version'] == 2:
            return {'result': 'already-upgraded', 'schema_version': 2}
        # Reserved names in old unknown metadata cannot be silently reinterpreted.
        if 'state_upgrade' in data:
            raise FrontendError('reserved upgrade metadata already exists')
        for a in data['associations'].values():
            if 'managed_state' in a:
                raise FrontendError('reserved managed_state metadata already exists')
            if a['ownership'] == 'managed':
                entries, _ = capture(safe_path(store.directory / 'snapshots' / a['id']))
                if entries != members_from_import(a):
                    raise FrontendError('import snapshot differs from provenance; upgrade blocked')
        before = store.path.read_bytes() if store.path.exists() else encode(data)
        backup = safe_path(store.directory / UPGRADE_BACKUP)
        if backup.exists():
            if backup.read_bytes() != before or read_manifest(backup) != data:
                raise FrontendError('prior upgrade backup differs; retain for explicit review')
        else:
            atomic_write(backup, before)
        if backup.read_bytes() != before or read_manifest(backup) != data:
            raise FrontendError('upgrade backup did not validate')
        for a in data['associations'].values():
            if a['ownership'] == 'managed': initialize_history(a)
        data.update(schema_version=2, state_upgrade={'from_version': 1, 'at': now(),
            'backup': UPGRADE_BACKUP, 'backup_sha256': hashlib.sha256(before).hexdigest()})
        validate(data)
        # Sole authority commit: native snapshots are unchanged; old readers reject v2.
        atomic_write(store.path, encode(data))
        return {'result': 'upgraded', 'schema_version': 2, 'backup': UPGRADE_BACKUP,
                'associations': {a['id']: a['managed_state']['current_generation']
                                 for a in data['associations'].values() if a['ownership'] == 'managed'}}


def inspect_history(store, association_id):
    data = store.read()
    a = data['associations'].get(association_id)
    if a is None: raise FrontendError('unknown association')
    g, _, _, _ = resolve_generation(store, data, a)
    return {'association_id': a['id'], 'hunter_id': a['hunter_id'], 'instance_id': a['instance_id'],
            'native_slot': a['filename_slot'], 'authority': AUTHORITY,
            'current_generation': g['id'], 'history': copy.deepcopy(a['managed_state']),
            'import_provenance': {k: copy.deepcopy(v) for k, v in a.items()
                                  if k not in ('managed_state', 'last_observation', 'authority')}}
