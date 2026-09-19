"""Bounded session evidence and durable journals; no child execution here."""
import hashlib
import json
import os
from pathlib import Path
import stat

from .profiles import MAX_STATE_BYTES, codec_inspect
from .store import FrontendError, _unique_object, atomic_write, now, valid_id

TRANSITIONS = {
    'prepared': {'launching', 'failed'},
    'launching': {'running', 'failed', 'interrupted'},
    'running': {'returned', 'interrupted'},
    'returned': {'inspecting'},
    'inspecting': {'candidate', 'quarantined'},
    'candidate': set(), 'quarantined': set(), 'failed': set(), 'interrupted': set(),
}


def safe_path(path):
    """Reject aliases, traversal, links and special files, including ancestors.

    This guards frontend-owned paths, not hostile concurrent filesystem mutation.
    """
    path = Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise FrontendError('session path must be absolute without traversal')
    for part in [*reversed(path.parents), path]:
        if part.is_symlink() or part.resolve() != part:
            raise FrontendError('session path contains an alias')
        if part.exists():
            info = part.lstat()
            if not (stat.S_ISDIR(info.st_mode) or stat.S_ISREG(info.st_mode)):
                raise FrontendError('session path contains a special file')
            if stat.S_ISREG(info.st_mode) and info.st_nlink != 1:
                raise FrontendError('session file has multiple hard links')
    return path


def session_root(store, identity):
    if not valid_id(identity):
        raise FrontendError('invalid session UUID')
    return safe_path(store.directory / 'sessions' / identity)


def encode(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + '\n').encode()


def persist(root, journal):
    safe_path(root)
    validate_journal(journal, root.name)
    atomic_write(safe_path(root / 'journal.json'), encode(journal))


def validate_journal(journal, identity):
    if (not isinstance(journal, dict) or journal.get('schema_version') != 1
            or type(journal.get('schema_version')) is not int
            or journal.get('id') != identity or not valid_id(identity)
            or journal.get('path_flavor') != os.name
            or journal.get('state') not in TRANSITIONS):
        raise FrontendError('invalid/foreign session journal; explicit review required')
    events = journal.get('transitions')
    if not isinstance(events, list) or not events or events[0].get('state') != 'prepared':
        raise FrontendError('invalid session transition history')
    previous = None
    for event in events:
        if not isinstance(event, dict) or not isinstance(event.get('at'), str):
            raise FrontendError('invalid session event')
        state = event.get('state')
        if state not in TRANSITIONS or (previous and state not in TRANSITIONS[previous]):
            raise FrontendError('illegal session transition history')
        previous = state
    if previous != journal['state']:
        raise FrontendError('session state disagrees with transition history')
    for field in ('pins', 'execution', 'capabilities'):
        if not isinstance(journal.get(field), dict):
            raise FrontendError('incomplete session journal')
    if not isinstance(journal.get('diagnostics'), list):
        raise FrontendError('invalid session diagnostics')
    for field in ('hunter_id', 'instance_id', 'association_id'):
        if not valid_id(journal['pins'].get(field)):
            raise FrontendError('invalid session provenance identity')
    return journal


def read_journal(store, identity):
    path = safe_path(session_root(store, identity) / 'journal.json')
    try:
        if path.stat().st_size > 4 * 1024 * 1024:
            raise FrontendError('session journal exceeds limit')
        return validate_journal(json.loads(path.read_text(), object_pairs_hook=_unique_object), identity)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise FrontendError(f'cannot read session journal: {error}') from error


def transition(root, journal, state, **fields):
    if state not in TRANSITIONS[journal['state']]:
        raise FrontendError(f"illegal session transition: {journal['state']} -> {state}")
    # Mutate the caller only after durable replacement succeeds.
    updated = json.loads(json.dumps(journal))
    updated.update(fields, state=state)
    updated['transitions'].append({'state': state, 'at': now()})
    persist(root, updated)
    journal.clear()
    journal.update(updated)


def capture(directory):
    """Inventory all entries, preserving bounded regular bytes, never follow links.

    Unsafe or oversized entries remain in work for review; no partial inventory
    can become a clean candidate. Two observations detect ordinary live writers.
    """
    directory = safe_path(directory)
    if not directory.is_dir():
        raise FrontendError('state directory missing')

    def once():
        entries, blobs = [], {}
        total = 0

        def visit(parent):
            nonlocal total
            for path in sorted(parent.iterdir()):
                if len(entries) >= 128:
                    raise FrontendError('state inventory exceeds 128 entries; retained for review')
                relative = path.relative_to(directory).as_posix()
                info = path.lstat()
                entry = {'path': relative, 'type': 'unsafe', 'size': info.st_size}
                entries.append(entry)
                if any(c in path.name for c in ('\\', ':', '\x00')):
                    continue
                if path.is_symlink():
                    entry['type'] = 'symlink'
                elif path.is_dir():
                    entry.update(type='directory', size=0)
                    visit(path)
                elif stat.S_ISREG(info.st_mode) and info.st_nlink == 1:
                    total += info.st_size
                    if info.st_size > MAX_STATE_BYTES or total > 32 * 1024 * 1024:
                        entry['type'] = 'oversized'
                        continue
                    with path.open('rb') as stream:
                        content = stream.read(MAX_STATE_BYTES + 1)
                    after = path.stat()
                    signature = lambda s: (s.st_size, s.st_mtime_ns, s.st_ino, s.st_mode, s.st_nlink)
                    if len(content) != info.st_size or signature(after) != signature(info):
                        raise FrontendError('state changed while capturing')
                    entry.update(type='file', sha256=hashlib.sha256(content).hexdigest())
                    blobs[relative] = content
        visit(directory)
        return entries, blobs

    first = once()
    if first != once():
        raise FrontendError('state changed during capture')
    return first


def write_blobs(directory, blobs):
    safe_path(directory).mkdir(parents=True, exist_ok=True)
    for relative, content in blobs.items():
        parts = Path(relative)
        if parts.is_absolute() or '..' in parts.parts or '\\' in relative or ':' in relative:
            raise FrontendError('unsafe captured state path')
        path = safe_path(directory / parts)
        path.parent.mkdir(parents=True, exist_ok=True)
        atomic_write(path, content)


def inspect_bytes(blobs, slot, probe):
    decoded, diagnostics = {}, []
    for name, content in blobs.items():
        if name not in (f'trophy{slot:02d}.sav', f'trophy{slot:02d}.sab'):
            continue
        value = codec_inspect(content, Path(name).suffix[1:], probe)
        decoded[name] = value
        if not value.get('codec_roundtrip_exact'):
            diagnostics.append({'code': 'unreadable-state', 'path': name})
        if value.get('registration', slot) != slot:
            diagnostics.append({'code': 'registration-mismatch', 'path': name})
    return decoded, diagnostics
