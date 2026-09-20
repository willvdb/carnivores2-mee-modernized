#!/usr/bin/env python3
"""Read-only content observations against the unchanged discovery reference."""
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import discovery as reference

DRIVER = str(Path(sys.argv[1]).resolve())
CASES = 0


def native(op, root, ref=None, **kwargs):
    request = {'op': op, 'root': str(root)}
    if ref is not None:
        request['reference'] = ref
    p = subprocess.run([DRIVER], input=(json.dumps(request) + '\n').encode(),
                       capture_output=True, timeout=90, **kwargs)
    assert p.returncode == 0, (request, p.returncode, p.stderr)
    return json.loads(p.stdout)


def oracle(op, root, ref=None):
    root = Path(root) if op not in ('native_path', 'resolve_reference', 'resolved_path') else root
    if op == 'fingerprint_payload':
        content = reference.resolved_path(root, 'HUNTDAT')
        return json.dumps(sorted([[r, s, reference.hash_file(content / r)]
                                 for r, s, _, _ in reference.content_inventory(content)]),
                          ensure_ascii=True, separators=(',', ':'))
    if op == 'walk_files':
        return [str(p) for p in reference.walk_files(root)]
    result = getattr(reference, op)(root, ref) if ref is not None else getattr(reference, op)(root)
    return str(result) if isinstance(result, Path) else result


def check(op, root, ref=None, cwd=None):
    global CASES
    old = Path.cwd()
    try:
        if cwd:
            os.chdir(cwd)
        try:
            expected = oracle(op, root, ref)
        except (ValueError, OSError, RuntimeError) as e:
            expected = e
    finally:
        os.chdir(old)
    actual = native(op, root, ref, cwd=cwd)
    if isinstance(expected, Exception):
        assert not actual['ok'], (op, root, ref, expected, actual)
    else:
        assert actual['ok'], (op, root, ref, expected, actual)
        # Compare exact compact framing, not Python's loose numeric equality.
        assert json.dumps(actual['value'], ensure_ascii=True, separators=(',', ':')) == json.dumps(expected, ensure_ascii=True, separators=(',', ':')), (op, root, ref, expected, actual)
    CASES += 1
    return actual


def write(root, relative, data=b'x'):
    p = root / relative
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p


def snapshot(root):
    out = []
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            p = Path(parent) / name
            s = p.lstat()
            out.append((str(p), s.st_mode, s.st_size, s.st_mtime_ns,
                        os.readlink(p) if p.is_symlink() else hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None))
    return sorted(out)


def tree(root):
    before = snapshot(root)
    check('walk_files', root)
    check('content_inventory', root / 'HUNTDAT')
    check('fingerprint', root)
    check('fingerprint_payload', root)
    assert snapshot(root) == before, 'source changed by observation'


def main(base):
    root = base / 'game'
    root.mkdir()
    for op in ('native_path', 'walk_files', 'content_inventory', 'resolve_reference', 'resolved_path', 'fingerprint'):
        for source in (root, root / 'absent', str(root) + '\0tail'):
            check(op, source, 'HUNTDAT' if op in ('resolve_reference', 'resolved_path') else None)
    write(root, 'HUNTDAT', b'not a directory')
    tree(root)  # Shape validation belongs to recognize in later 2A.2.
    (root / 'HUNTDAT').unlink()
    (root / 'HUNTDAT').mkdir()
    tree(root)
    names = ['empty', 'a.bin', 'nested/b.bin', 'Save.SAV', 'saves/a.bin', 'LOGS/a', 'Cache/a',
             'Screenshots/a', 'nested/a.TmP', 'a.sab', 'a.BAK', 'a.LOG', '.sav', 'ß', 'Σ',
             'ﬃ', 'İ', '𝄞/音', '\ue000/a', 'z/.hidden', 'z/a.sav.extra']
    if os.name != 'nt':
        names += ['trailing.', 'raw\udcff', 'back\\slash/a', 'line\nbreak', 'colon:name']
    for i, name in enumerate(names):
        write(root / 'HUNTDAT', name, bytes(range(i)))
    tree(root)
    for ref in ('HUNTDAT', 'huntdat', 'HUNTDAT//./nested/b.bin/', 'HUNTDAT\\nested\\b.bin',
                'HUNTDAT/ß', 'HUNTDAT/SS', 'HUNTDAT/σ', 'HUNTDAT/ς', 'HUNTDAT/FFI',
                '', '.', '..', 'HUNTDAT/..', '/HUNTDAT', '//HUNTDAT', 'C:foo', 'HUNTDAT/colon:name',
                'HUNTDAT/absent', 'HUNTDAT/a.bin/child', 'HUNTDAT/\0', 'HUNTDAT/raw\udcff'):
        check('resolve_reference', root, ref)
        check('resolved_path', root, ref)
    for spelling in ('.', './', 'HUNTDAT/..', './HUNTDAT/..', str(root) + '//./'):
        check('walk_files', spelling, cwd=root)
        check('resolved_path', spelling, 'HUNTDAT/nested/b.bin', cwd=root)
    for path in ('', '.', '~', '~/missing', 'C:relative', 'C:/absolute', '\\rooted', '\\\\server\\share\\dir'):
        check('native_path', path, cwd=root)
    # Literal tilde/backslash roots differ from native locator expansion/rejection.
    write(root / '~', 'HUNTDAT/a')
    tree(root / '~')
    check('resolve_reference', '~', 'HUNTDAT/a', cwd=root)
    if os.name != 'nt':
        write(root / 'literal\\root', 'HUNTDAT/a')
        tree(root / 'literal\\root')
        check('native_path', root / 'literal\\root')
    # Every equal-casefold sibling participates, including exact matches.
    collisions = base / 'collision'
    write(collisions, 'HUNTDAT/a.bin')
    write(collisions, 'HUNTDAT/A.BIN')
    tree(collisions)
    check('resolve_reference', collisions, 'HUNTDAT/a.bin')
    for i, pair in enumerate((('ß', 'ss'), ('Σ', 'ς'), ('ﬃ', 'FFI'))):
        r = base / f'unicode-collision-{i}'
        for n in pair:
            write(r, 'HUNTDAT/' + n)
        tree(r)
        check('resolve_reference', r, 'HUNTDAT/' + pair[0])
    # Collision validation must see mutable files and descend ignored parents.
    for i, names in enumerate((('a.sav', 'A.SAV'), ('logs/x', 'logs/X'), ('cache/d', 'cache/D'))):
        r = base / f'ignored-collision-{i}'
        for n in names:
            write(r, 'HUNTDAT/' + n)
        tree(r)
    # Signed nanosecond metadata is compared with original stat, no float bridge.
    timestamp = root / 'HUNTDAT/a.bin'
    for ns in (-1000000001, -1, 0, 1, 1234567890123456789):
        try:
            os.utime(timestamp, ns=(ns, ns))
        except (OverflowError, OSError):
            continue
        check('content_inventory', root / 'HUNTDAT')
    rng = random.Random(98271)
    for i in range(24):
        r = base / f'random-{i}'
        (r / 'HUNTDAT').mkdir(parents=True)
        for j in range(rng.randrange(1, 20)):
            name = rng.choice(['a', 'ß', 'ς', '𝄞', '\ue123']) + str(j)
            parent = rng.choice(['', 'p/', 'logs/', 'nested/deep/'])
            suffix = rng.choice(['.bin', '.sav', '.TMP', '.map', ''])
            write(r / 'HUNTDAT', parent + name + suffix, rng.randbytes(rng.randrange(256)))
        tree(r)
    # Stream beyond all existing capture/profile ceilings, and >128 entries.
    big = base / 'big'
    for i in range(130):
        write(big, f'HUNTDAT/f{i:03}', b'abc')
    write(big, 'HUNTDAT/large', b'0123456789abcdef' * (3 * 1024 * 1024 + 1))
    tree(big)
    if os.name != 'nt':
        links(base)
    else:
        tree(Path('\\\\?\\' + str(big)))
        check('resolve_reference', Path('\\\\?\\' + str(big)), 'HUNTDAT/large')
    active_writer(base / 'race')
    membership_writer(base / 'membership-race')
    metadata_writer(base / 'metadata-race')


def links(base):
    root = base / 'links'
    write(root, 'HUNTDAT/a', b'alias')
    os.link(root / 'HUNTDAT/a', root / 'HUNTDAT/hard')
    tree(root)
    (base / 'root-alias').symlink_to(root, target_is_directory=True)
    tree(base / 'root-alias')
    (root / 'alias').symlink_to(root / 'HUNTDAT', target_is_directory=True)
    check('resolve_reference', root, 'alias/a')
    (root / 'HUNTDAT/internal').symlink_to(root / 'HUNTDAT/a')
    tree(root)
    (root / 'HUNTDAT/internal').unlink()
    (root / 'HUNTDAT/logs').mkdir()
    (root / 'HUNTDAT/logs/ignored.tmp').symlink_to(base / 'absent')
    tree(root)
    (root / 'HUNTDAT/logs/ignored.tmp').unlink()
    (root / 'HUNTDAT/outside').symlink_to(base, target_is_directory=True)
    check('resolve_reference', root, 'HUNTDAT/outside')
    tree(root)
    (root / 'HUNTDAT/outside').unlink()
    os.mkfifo(root / 'HUNTDAT/pipe')
    tree(root)
    assert not native('hash_file', root / 'HUNTDAT/pipe')['ok']  # Never call blocking Python hash_file.
    (base / 'deep-alias').symlink_to(root / 'HUNTDAT', target_is_directory=True)
    check('resolve_reference', base / 'deep-alias' / '..', 'HUNTDAT/a')
    check('native_path', base / 'deep-alias' / '..')
    tree(Path('//' + str(root).lstrip('/')))
    if Path('/proc/version').is_file():
        check('hash_file', '/proc/version')  # Actual bytes, not reported zero extent.


def active_writer(root):
    # Atomic same-sized replacements yield only two valid complete member hashes.
    p = write(root, 'HUNTDAT/a', b'A' * (4 * 1024 * 1024))
    a = oracle('fingerprint', root)
    p.write_bytes(b'B' * (4 * 1024 * 1024))
    b = oracle('fingerprint', root)
    stop = threading.Event()
    errors = []
    rejected = 0
    def writer():
        try:
            i = 0
            while not stop.is_set():
                temp = root / 'replacement'
                temp.write_bytes((b'A' if i % 2 else b'B') * (4 * 1024 * 1024))
                os.replace(temp, p)
                i += 1
        except Exception as e:
            errors.append(e)
    thread = threading.Thread(target=writer)
    thread.start()
    try:
        for _ in range(16):
            result = native('fingerprint', root)
            if result['ok']:
                assert result['value'] in (a, b), result
            else:
                rejected += 1
    finally:
        stop.set()
        thread.join(timeout=30)
    assert not thread.is_alive() and not errors
    assert rejected, 'active writer did not exercise change rejection'
    tree(root)


def membership_writer(root):
    write(root, 'HUNTDAT/a', b'A' * (8 * 1024 * 1024))
    expected = [oracle('fingerprint', root)]
    member = write(root, 'HUNTDAT/b', b'B')
    expected.append(oracle('fingerprint', root))
    stop = threading.Event()
    errors = []
    def writer():
        try:
            while not stop.is_set():
                member.unlink(missing_ok=True)
                temp = root / 'replacement'
                temp.write_bytes(b'B')
                os.replace(temp, member)
        except Exception as e:
            errors.append(e)
    thread = threading.Thread(target=writer)
    thread.start()
    rejected = 0
    try:
        for _ in range(12):
            result = native('fingerprint', root)
            if result['ok']:
                assert result['value'] in expected, result
            else:
                rejected += 1
    finally:
        stop.set()
        thread.join(timeout=30)
    assert not thread.is_alive() and not errors
    assert rejected, 'membership changes did not exercise rejection'
    tree(root)


def metadata_writer(root):
    p = write(root, 'HUNTDAT/a', b'M' * (8 * 1024 * 1024))
    expected = oracle('fingerprint', root)
    stop = threading.Event()
    errors = []
    def writer():
        try:
            i = 0
            while not stop.is_set():
                stamp = 1234567890000000000 + i * 1000000
                os.utime(p, ns=(stamp, stamp))
                i += 1
        except Exception as e:
            errors.append(e)
    thread = threading.Thread(target=writer)
    thread.start()
    rejected = 0
    try:
        for _ in range(12):
            result = native('fingerprint', root)
            if result['ok']:
                assert result['value'] == expected, result
            else:
                rejected += 1
    finally:
        stop.set()
        thread.join(timeout=30)
    assert not thread.is_alive() and not errors
    assert rejected, 'metadata changes did not exercise rejection'
    tree(root)


def capability(name, base):
    if name == 'permissions':
        if os.name != 'posix':
            return 77
        root = base / 'game'
        p = write(root, 'HUNTDAT/a')
        denied = root / 'HUNTDAT/denied'
        write(denied, 'x')
        for d in (base, root, root / 'HUNTDAT'):
            d.chmod(0o755)
        denied.chmod(0)
        p.chmod(0)
        identity = {'user': 65534, 'group': 65534, 'extra_groups': []} if os.geteuid() == 0 else {}
        try:
            script = 'import sys,json;sys.path.insert(0,sys.argv[1]);from lodge.discovery import content_inventory;print(json.dumps(content_inventory(__import__("pathlib").Path(sys.argv[2]))))'
            py = subprocess.run([sys.executable, '-c', script, str(Path(__file__).resolve().parents[2]), str(root / 'HUNTDAT')], capture_output=True, **identity)
            assert py.returncode == 0, py.stderr
            result = native('content_inventory', root / 'HUNTDAT', **identity)
            assert result['ok'] and result['value'] == json.loads(py.stdout)
            assert len(result['value']) == 1
            assert not native('hash_file', p, **identity)['ok']
            assert not native('resolve_reference', root, 'HUNTDAT/denied/x', **identity)['ok']
            print('permission denied scandir omitted; hash and reference enumeration rejected')
            return 0
        except PermissionError as e:
            print('permission identity unavailable:', e)
            return 77
        finally:
            denied.chmod(0o700)
            p.chmod(0o600)
    if os.name != 'nt':
        print(name, 'unsupported on this platform')
        return 77
    root = base / 'game'
    write(root, 'HUNTDAT/a', b'a')
    outside = base / 'outside'
    write(outside, 'x')
    try:
        if name == 'unpaired-utf16':
            write(root, 'HUNTDAT/\ud800/x')
        elif name == 'hardlink':
            os.link(root / 'HUNTDAT/a', root / 'HUNTDAT/hard')
        elif name == 'file-link':
            (root / 'HUNTDAT/link').symlink_to(root / 'HUNTDAT/a')
            (base / 'alias').symlink_to(root, target_is_directory=True)
        elif name == 'dangling-link':
            (root / 'HUNTDAT/link').symlink_to(root / 'absent')
        elif name in ('junction', 'cycle'):
            target = root / 'HUNTDAT' if name == 'cycle' else outside
            p = subprocess.run(['cmd', '/c', 'mklink', '/J', str(root / 'HUNTDAT/junction'), str(target)], capture_output=True)
            if p.returncode:
                print(name, 'unavailable', p.stderr)
                return 77
        else:
            raise AssertionError(name)
    except OSError as e:
        print(name, 'creation unavailable:', e)
        return 77
    if name == 'cycle':
        walk = reference.walk_files(root / 'HUNTDAT')
        paths = [str(next(walk)) for _ in range(3)]
        walk.close()
        assert len(paths) == 3  # Never run an unbounded Python cyclic inventory.
        for op in ('walk_files', 'content_inventory', 'fingerprint'):
            result = native(op, root if op == 'fingerprint' else root / 'HUNTDAT')
            assert not result['ok'] and 'directory cycle' in result['error']
        return 0
    if name == 'junction':
        for n in ('alias-a', 'alias-b'):
            p = subprocess.run(['cmd', '/c', 'mklink', '/J', str(root / 'HUNTDAT' / n), str(outside)], capture_output=True)
            assert p.returncode == 0, p.stderr
        assert len(reference.content_inventory(root / 'HUNTDAT')) == 4
    tree(root)
    check('resolve_reference', root, 'HUNTDAT/' + ('junction/x' if name == 'junction' else 'a'))
    if name == 'file-link':
        tree(base / 'alias')
    if name == 'unpaired-utf16':
        tree(Path('\\\\?\\' + str(root)))
        check('resolve_reference', root, 'HUNTDAT/\ud800/x')
    return 0


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='c2-content-') as temp:
        if len(sys.argv) > 2:
            raise SystemExit(capability(sys.argv[2], Path(temp)))
        main(Path(temp))
    print(f'{CASES} content oracle cases; exact observations/payloads, read-only snapshots, streaming and valid raced identities')
