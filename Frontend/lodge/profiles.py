"""State-set discovery, codec inspection and explicit lossless association."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from .discovery import diagnostic, get_instance, inspect_instance, walk_files
from .store import FrontendError, atomic_write, new_id, now

STATE_NAME = re.compile(r'^trophy(\d+)\.(sav|sab)$', re.IGNORECASE)
MAX_STATE_BYTES = 16 * 1024 * 1024


def inventory(root):
    root = Path(root)
    groups = {}
    if not root.is_dir():
        return []
    for path in walk_files(root):
        match = STATE_NAME.fullmatch(path.name)
        if not match:
            continue
        relative = path.relative_to(root)
        key = (relative.parent / path.stem.casefold()).as_posix()
        item = groups.setdefault(key, {'key': key, 'filename_slot': int(match[1]), 'files': [],
                                      'ownership': 'unclaimed', 'diagnostics': []})
        item['files'].append({'path': relative.as_posix(), 'kind': match[2].lower(), 'size': path.stat().st_size})
    for item in groups.values():
        kinds = [f['kind'] for f in item['files']]
        if kinds.count('sav') > 1 or kinds.count('sab') > 1:
            item['diagnostics'].append(diagnostic('ambiguous-state-files', 'Case-colliding state members; no file chosen.'))
        if 'sav' not in kinds:
            item['diagnostics'].append(diagnostic('orphan-companion', 'Room has no corresponding save; retained without repair.'))
        if item['filename_slot'] not in range(8):
            item['diagnostics'].append(diagnostic('slot-outside-menu', 'Outside evidenced eight-slot menu range.'))
        expected = f"trophy{item['filename_slot']:02d}"
        if Path(item['key']).name != expected:
            item['diagnostics'].append(diagnostic('noncanonical-slot-name', 'Filename differs from menu convention.'))
        if Path(item['key']).parent != Path('.'):
            item['diagnostics'].append(diagnostic('non-root-state', 'Packaged/example/backup candidate; not an active root slot.'))
        item['diagnostics'].append(diagnostic('ownership-unproven', 'Discovery cannot distinguish user saves from packaged/example state.'))
    return sorted(groups.values(), key=lambda x: x['key'])


def read_set(root, state):
    if any(d['code'] == 'ambiguous-state-files' for d in state['diagnostics']):
        raise FrontendError('ambiguous native state file names')
    root = Path(root).resolve()
    result = {}
    for entry in state['files']:
        path = root / entry['path']
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            raise FrontendError('state member escaped its root')
        with path.open('rb') as stream:
            content = stream.read(MAX_STATE_BYTES + 1)
        if len(content) > MAX_STATE_BYTES:
            raise FrontendError('state member exceeds conservative inspection limit')
        result[entry['path']] = content
    return result


def stable_read(root, state):
    first = read_set(root, state)
    # Detect companion addition/removal as well as member changes between reads.
    again = next((s for s in inventory(root) if s['key'] == state['key']), None)
    if again is None or {f['path'] for f in again['files']} != set(first):
        raise FrontendError('state membership changed during snapshot')
    second = read_set(root, again)
    if first != second:
        raise FrontendError('native state changed during snapshot; close legacy writers')
    return first


def codec_inspect(content, kind, probe=None, dialect='unknown'):
    if dialect == 'iceage-triassic':
        return {'layout': 'unsupported-iceage-family', 'codec_roundtrip_exact': False}
    if len(content) != (1660 if kind == 'sav' else 7176):
        return {'layout': 'unknown', 'codec_roundtrip_exact': False}
    probe = probe or os.environ.get('C2_PROFILE_PROBE')
    if not probe:
        return {'layout': 'size-candidate-only', 'codec_roundtrip_exact': False, 'diagnostic': 'codec-helper-unavailable'}
    try:
        process = subprocess.run([str(probe), 'save' if kind == 'sav' else 'room'], input=content,
                                 capture_output=True, timeout=15, check=False)
    except subprocess.TimeoutExpired as error:
        raise FrontendError('codec helper timed out') from error
    if process.returncode != 0:
        raise FrontendError(f'codec helper failed ({process.returncode})')
    result = json.loads(process.stdout)
    if 'name_hex' in result:
        result['name_display_latin1'] = bytes.fromhex(result['name_hex']).split(b'\0', 1)[0].decode('latin1')
    return result


def inspect_set(root, state, probe=None, dialect='unknown'):
    blobs = stable_read(root, state)
    result = {**state, 'files': [], 'diagnostics': list(state['diagnostics'])}
    for entry in state['files']:
        content = blobs[entry['path']]
        decoded = codec_inspect(content, entry['kind'], probe, dialect)
        result['files'].append({**entry, 'size': len(content), 'sha256': hashlib.sha256(content).hexdigest(), 'decoded': decoded})
        if decoded.get('registration', state['filename_slot']) != state['filename_slot']:
            result['diagnostics'].append(diagnostic('registration-mismatch', 'Filename slot disagrees with embedded registration; no normalization performed.'))
        if not decoded.get('codec_roundtrip_exact'):
            result['diagnostics'].append(diagnostic('unreadable-layout', 'Preserved as opaque bytes; no save compatibility claim.', path=entry['path']))
    result['diagnostics'].append(diagnostic('pair-coherence-unverified', 'Two equal observations cannot certify an externally updated SAV/SAB transaction.'))
    return result


def associate(store, data, hunter_id, instance_id, state_key, origin, ownership=None, probe=None):
    if hunter_id not in data['hunters'] or data['hunters'][hunter_id].get('archived_at'):
        raise FrontendError('association requires an active hunter identity')
    if origin not in ('personal', 'bundled-example', 'unknown'):
        raise FrontendError('explicit source declaration required: personal, bundled-example, or unknown')
    instance = get_instance(data, instance_id)
    observation = inspect_instance(instance)
    if not observation['recognized'] or observation.get('revision_changed'):
        raise FrontendError('installation unavailable or revision changed; refresh and review before association')
    ownership = ownership or ('managed' if instance['mode'] == 'managed' else 'referenced')
    if ownership not in ('managed', 'referenced'):
        raise FrontendError('invalid ownership mode')
    if ownership == 'referenced' and any(a['instance_id'] == instance_id and a['state_key'] == state_key
                                       and a['ownership'] == 'referenced' for a in data['associations'].values()):
        raise FrontendError('source already referenced; choose an explicit independent managed copy')
    state = next((s for s in inventory(instance['path']) if s['key'] == state_key), None)
    if not state or not any(f['kind'] == 'sav' for f in state['files']):
        raise FrontendError('selected state has no save; orphan rooms are never adopted as profiles')
    inspection = inspect_set(instance['path'], state, probe, instance['dialect_hint'])
    if any(d['code'] == 'registration-mismatch' for d in inspection['diagnostics']):
        raise FrontendError('registration mismatch requires explicit future reconciliation; source unchanged')
    identity = new_id()
    association = {'id': identity, 'hunter_id': hunter_id, 'instance_id': instance_id,
                   'state_key': state_key, 'filename_slot': state['filename_slot'], 'origin': origin,
                   'ownership': ownership, 'authority': 'native-files' if ownership == 'referenced' else 'independent-snapshot',
                   'writable': False, 'revision': instance['revision'], 'created_at': now(),
                   'files': [{k: v for k, v in f.items() if k != 'decoded'} for f in inspection['files']],
                   'diagnostics': inspection['diagnostics']}
    if ownership == 'managed':
        blobs = stable_read(instance['path'], state)
        if any(hashlib.sha256(blobs[f['path']]).hexdigest() != f['sha256'] for f in association['files']):
            raise FrontendError('state changed after inspection')
        staging = store.directory / 'snapshots' / ('.pending-' + identity)
        final = store.directory / 'snapshots' / identity
        staging.mkdir(parents=True, exist_ok=False)
        for relative, blob in blobs.items():
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            atomic_write(target, blob)
        os.replace(staging, final)
        if os.name == 'posix':
            fd = os.open(final.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        association['snapshot'] = f'snapshots/{identity}'
    data['associations'][identity] = association
    return association


def association_root(store, instance, association):
    if association['ownership'] == 'referenced':
        if instance['path_flavor'] != os.name:
            raise FrontendError('foreign installation path requires relocation')
        return Path(instance['path'])
    # Compute from validated UUID, never trust an arbitrary manifest path.
    return store.directory / 'snapshots' / association['id']


def refresh_association(store, data, identity, probe=None):
    if identity not in data['associations']:
        raise FrontendError('unknown association ID')
    association = data['associations'][identity]
    instance = get_instance(data, association['instance_id'])
    root = association_root(store, instance, association)
    state = next((s for s in inventory(root) if s['key'] == association['state_key']), None)
    if not state:
        result = {'status': 'missing-state', 'diagnostics': [diagnostic('missing-state', 'Native source remains associated; nothing deleted.')]}
    else:
        result = inspect_set(root, state, probe, instance['dialect_hint'])
        expected = {f['path']: f['sha256'] for f in association['files']}
        actual = {f['path']: f['sha256'] for f in result['files']}
        result['status'] = 'changed-state' if expected != actual else 'unchanged-state'
        if expected != actual:
            result['diagnostics'].append(diagnostic('state-drift', 'Native state differs from association baseline; no overwrite or normalization performed.'))
    association['last_observation'] = result
    return result
