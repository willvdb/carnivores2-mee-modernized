"""Read-only content observations and explicit instance registration."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re

from .store import FrontendError, new_id, now

MUTABLE_SUFFIXES = {'.sav', '.sab', '.log', '.tmp', '.bak', '.cfg'}
MUTABLE_DIRS = {'saves', 'logs', 'screenshots', 'cache'}
DIALECTS = {'unknown', 'c2-classic', 'iceage-triassic', 'mee-older', 'mee-newer'}


def diagnostic(code, message, **context):
    return {'code': code, 'message': message, **context}


def resolve_reference(root, reference):
    root = Path(root).resolve()
    parts = PurePosixPath(reference.replace('\\', '/'))
    if parts.is_absolute() or '..' in parts.parts or ':' in reference or not parts.parts:
        return {'reference': reference, 'status': 'unsafe'}
    current = root
    for component in parts.parts:
        if not current.is_dir():
            return {'reference': reference, 'status': 'missing'}
        matches = [p for p in current.iterdir() if p.name.casefold() == component.casefold()]
        if len(matches) != 1:
            return {'reference': reference, 'status': 'ambiguous' if matches else 'missing'}
        current = matches[0]
        # Even exact-spelling matches do not erase case ambiguity.
        if not current.resolve().is_relative_to(root):
            return {'reference': reference, 'status': 'unsafe'}
    return {'reference': reference, 'status': 'found', 'path': current.relative_to(root).as_posix()}


def resolved_path(root, reference):
    result = resolve_reference(root, reference)
    if result['status'] != 'found':
        raise FrontendError(f"{reference}: {result['status']}")
    return Path(root) / result['path']


def hash_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def walk_files(root):
    """Do not follow symlinks, even internal ones: inventories must be finite."""
    for directory, dirs, files in os.walk(root, followlinks=False):
        dirs[:] = sorted(d for d in dirs if not (Path(directory) / d).is_symlink())
        for name in sorted(files):
            path = Path(directory) / name
            if not path.is_symlink() and path.is_file():
                yield path


def fingerprint(root):
    content = resolved_path(root, 'HUNTDAT')
    entries = []
    # Omitting aliases would make revision equality falsely ignore live content.
    for parent, dirs, files in os.walk(content, followlinks=False):
        seen = set()
        for name in dirs + files:
            path = Path(parent) / name
            if path.is_symlink():
                raise FrontendError(f'content symlink requires explicit policy: {path.relative_to(content)}')
            if name.casefold() in seen:
                raise FrontendError(f'case-colliding content names: {path.relative_to(content)}')
            seen.add(name.casefold())
    for path in walk_files(content):
        relative = path.relative_to(content)
        if path.suffix.lower() in MUTABLE_SUFFIXES or any(p.casefold() in MUTABLE_DIRS for p in relative.parts[:-1]):
            continue
        before = path.stat()
        digest = hash_file(path)
        after = path.stat()
        if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
            raise FrontendError(f"content changed while hashing: {relative}")
        entries.append([relative.as_posix(), after.st_size, digest])
    payload = json.dumps(sorted(entries), ensure_ascii=True, separators=(',', ':')).encode()
    return {'algorithm': 'huntdat-sha256-v1', 'sha256': hashlib.sha256(payload).hexdigest(),
            'file_count': len(entries), 'byte_count': sum(e[1] for e in entries)}


def recognize(root):
    root = Path(root).expanduser().resolve()
    result = {'path': str(root), 'recognized': False, 'diagnostics': [], 'executables': []}
    if not root.is_dir():
        result['diagnostics'].append(diagnostic('missing-installation', 'Registered root is unavailable.'))
        return result
    for reference in ('HUNTDAT/_RES.TXT', 'HUNTDAT/MENU', 'HUNTDAT/AREAS'):
        status = resolve_reference(root, reference)
        if status['status'] != 'found':
            result['diagnostics'].append(diagnostic('incomplete-root', 'Required coherent-root evidence unavailable.', **status))
    result['executables'] = [p.name for p in sorted(root.iterdir()) if p.is_file() and not p.is_symlink()
                             and (p.suffix.lower() in ('.exe', '.ren') or p.name.lower() in ('carnivores2', 'carnivores2-gl'))]
    if not result['executables']:
        result['diagnostics'].append(diagnostic('missing-engine-evidence', 'No root engine/launcher candidate; assets or partial overlay only.'))
    areas = resolve_reference(root, 'HUNTDAT/AREAS')
    pairs = []
    if areas['status'] == 'found':
        directory = root / areas['path']
        if directory.is_dir():
            for path in directory.iterdir():
                if path.is_file() and path.suffix.lower() == '.map':
                    rsc = resolve_reference(root, (path.relative_to(root).with_suffix('.rsc')).as_posix())
                    if rsc['status'] == 'found':
                        if not path.stem.casefold().startswith('trophy'):
                            pairs.append(path.name)
    result['map_pairs'] = sorted(pairs)
    if not pairs:
        result['diagnostics'].append(diagnostic('missing-map-pair', 'No paired MAP/RSC content.'))
    # Scripts and menu directories, not merely paths with convenient names.
    for reference, directory in (('HUNTDAT/_RES.TXT', False), ('HUNTDAT/MENU', True)):
        status = resolve_reference(root, reference)
        if status['status'] == 'found':
            path = root / status['path']
            invalid = not path.is_dir() if directory else not path.is_file()
            if invalid:
                result['diagnostics'].append(diagnostic('invalid-root-entry', reference))
            elif not directory:
                with path.open('rb') as stream:
                    script = stream.read(8 * 1024 * 1024 + 1)
                if len(script) > 8 * 1024 * 1024 or not all(re.search(rb'\b' + key + rb'\s*\{', script, re.I) for key in (b'characters', b'weapons')):
                    result['diagnostics'].append(diagnostic('missing-script-evidence', 'Resource script lacks conventional characters/weapons blocks.'))
    result['recognized'] = not result['diagnostics']
    return result


def discover(directory):
    roots = []
    for parent, dirs, _ in os.walk(Path(directory).expanduser(), followlinks=False):
        dirs[:] = sorted(d for d in dirs if not (Path(parent) / d).is_symlink())
        if any(d.casefold() == 'huntdat' for d in dirs):
            roots.append(recognize(parent))
        # Keep searching siblings and nested games, but never mistake asset subtrees for games.
        dirs[:] = [d for d in dirs if d.casefold() != 'huntdat']
    return roots


def register(data, path, mode='registered', dialect='unknown', family=None, release=None, managed_root=None):
    root = Path(path).expanduser().resolve()
    if mode not in ('registered', 'managed') or dialect not in DIALECTS:
        raise FrontendError('invalid installation mode or dialect')
    if mode == 'managed' and (managed_root is None or not root.is_relative_to(Path(managed_root).expanduser().resolve())):
        raise FrontendError('managed installation must be under the explicit Expeditions directory')
    for instance in data['instances'].values():
        if instance['path_flavor'] == os.name and Path(instance['path']).resolve() == root:
            return instance
    evidence = recognize(root)
    if not evidence['recognized']:
        raise FrontendError('not a coherent installation: ' + json.dumps(evidence['diagnostics']))
    revision = fingerprint(root)
    engines = [{'path': name, 'sha256': hash_file(root / name), 'semantics': 'unknown'} for name in evidence['executables']]
    identity = new_id()
    instance = {'id': identity, 'path': str(root), 'path_flavor': os.name, 'mode': mode,
                'family': family, 'release': release, 'dialect_hint': dialect,
                'identity_evidence': 'user assertion' if family or release or dialect != 'unknown' else 'unresolved',
                'created_at': now(), 'revision': revision, 'revisions': [revision], 'evidence': evidence,
                'engine_evidence': engines}
    data['instances'][identity] = instance
    return instance


def get_instance(data, identity):
    if identity not in data['instances']:
        raise FrontendError('unknown instance ID')
    return data['instances'][identity]


def inspect_instance(instance):
    if instance['path_flavor'] != os.name:
        return {'recognized': False, 'diagnostics': [diagnostic('foreign-path', 'Explicit relocation needed on this OS.')]}
    observed = recognize(instance['path'])
    if observed['recognized']:
        observed['revision'] = fingerprint(instance['path'])
        observed['revision_changed'] = observed['revision'] != instance['revision']
        if observed['revision_changed']:
            observed['diagnostics'].append(diagnostic('content-revision-changed', 'Interpretations require review; saves are unchanged by inspection.'))
        observed['engine_evidence'] = [{'path': name, 'sha256': hash_file(Path(instance['path']) / name), 'semantics': 'unknown'} for name in observed['executables']]
        observed['engine_changed'] = observed['engine_evidence'] != instance.get('engine_evidence', [])
        if observed['engine_changed']:
            observed['diagnostics'].append(diagnostic('engine-evidence-changed', 'Engine/launcher bytes changed independently of content revision.'))
    return observed


def refresh_instance(instance):
    result = inspect_instance(instance)
    if result.get('revision_changed'):
        instance['revision'] = result['revision']
        if result['revision'] not in instance['revisions']:
            instance['revisions'].append(result['revision'])
    instance['last_observation'] = result
    return result


def move_candidates(data, path):
    revision = fingerprint(path)
    return [i['id'] for i in data['instances'].values()
            if (i['path_flavor'] != os.name or not Path(i['path']).exists()) and i['revision'] == revision]


def relocate(data, identity, path):
    instance = get_instance(data, identity)
    root = Path(path).expanduser().resolve()
    if instance['path_flavor'] == os.name and Path(instance['path']).exists():
        raise FrontendError('old installation still exists; this could be a clone, not a move')
    if any(i['path_flavor'] == os.name and Path(i['path']).resolve() == root for i in data['instances'].values()):
        raise FrontendError('destination already registered')
    if not recognize(root)['recognized'] or fingerprint(root) != instance['revision']:
        raise FrontendError('relocation requires a coherent root with matching content revision')
    instance.setdefault('previous_locations', []).append({'path': instance['path'], 'path_flavor': instance['path_flavor']})
    instance.update(path=str(root), path_flavor=os.name)
    # Moving outside the previous management tree relinquishes managed-install ownership.
    instance['mode'] = 'registered'
    return instance
