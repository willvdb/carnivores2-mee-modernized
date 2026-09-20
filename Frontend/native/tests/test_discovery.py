#!/usr/bin/env python3
"""Synthetic read-only discovery/instance oracles; no game assets or execution."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import discovery as reference
from lodge.store import validate

DRIVER = str(Path(sys.argv[1]).resolve())
CASES = 0


def native(request, **kwargs):
    p = subprocess.run([DRIVER], input=(json.dumps(request) + '\n').encode(),
                       capture_output=True, timeout=90, **kwargs)
    assert p.returncode == 0, (request, p.returncode, p.stderr)
    return json.loads(p.stdout)


def oracle(request):
    op = request['op']
    if op == 'script_prefix':
        with Path(request['root']).open('rb') as stream:
            return stream.read(8 * 1024 * 1024 + 1).decode('latin-1')
    if op in ('recognize', 'discover'):
        return getattr(reference, op)(request['root'])
    if op == 'engine_evidence':
        return reference.engine_evidence(request['root'], reference.recognize(request['root']))
    if op == 'inspect_raw':
        return reference.inspect_instance(request['instance'])
    data = json.loads((Path(request['store']) / 'lodge.json').read_bytes())
    validate(data)
    if op == 'move_candidates':
        return reference.move_candidates(data, request['root'])
    instance = reference.get_instance(data, request['identity'])
    return instance if op == 'get_instance' else reference.inspect_instance(instance)


def check(op, root=None, cwd=None, **args):
    global CASES
    request = {'op': op, **args}
    if root is not None:
        request['root'] = str(root)
    old = Path.cwd()
    try:
        if cwd is not None:
            os.chdir(cwd)
        try:
            expected = oracle(request)
            expected_json = json.dumps(expected, indent=2, ensure_ascii=True, allow_nan=False) + '\n'
        except (ValueError, OSError, RuntimeError, KeyError) as e:
            expected = e
    finally:
        os.chdir(old)
    actual = native(request, cwd=cwd)
    if isinstance(expected, Exception):
        assert not actual['ok'], (request, expected, actual)
    else:
        assert actual['ok'], (request, expected, actual)
        assert actual['json'] == expected_json, (request, expected_json, actual)
        assert json.dumps(actual['value'], ensure_ascii=True, separators=(',', ':')) == json.dumps(expected, ensure_ascii=True, separators=(',', ':')), (request, expected, actual)
    CASES += 1
    return actual


def write(root, relative, data=b'x'):
    p = root / relative
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p


def game(root, engine=True):
    write(root, 'HUNTDAT/_RES.TXT', b'characters { }\nweapons { }')
    (root / 'HUNTDAT/MENU').mkdir(parents=True, exist_ok=True)
    write(root, 'HUNTDAT/AREAS/Island.MAP')
    write(root, 'HUNTDAT/AREAS/Island.RSC')
    if engine:
        write(root, 'HUNT.EXE', b'candidate, never execute')
    return root


def snapshot(root):
    out = []
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            p = Path(parent) / name
            s = p.lstat()
            out.append((str(p), s.st_mode, s.st_size, s.st_mtime_ns,
                        os.readlink(p) if p.is_symlink() else hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None))
    return sorted(out)


def observed(root):
    before = snapshot(root)
    for op in ('recognize', 'discover', 'engine_evidence'):
        check(op, root)
    assert snapshot(root) == before, 'source modified by observation'


def instance(root, identity=1):
    revision = reference.fingerprint(root)
    return {'id': str(uuid.UUID(int=identity)), 'path': str(root), 'path_flavor': os.name,
            'mode': 'registered', 'managed_root': None, 'dialect_hint': 'unknown',
            'revision': revision, 'revisions': [copy.deepcopy(revision)],
            'engine_evidence': reference.engine_evidence(root, reference.recognize(root)),
            'unknown': {'nested': [True, None, 'unpaired\ud800', 10**80]}}


def manifest(store, instances):
    data = {'schema_version': 1, 'hunters': {}, 'active_hunter': None,
            'instances': {i['id']: i for i in instances}, 'associations': {}, 'host_settings': {}}
    validate(data)
    write(store, 'lodge.json', json.dumps(data).encode())
    return data


def inspect(store, i):
    before = snapshot(store)
    result = check('inspect_instance', store=str(store), identity=i['id'])
    assert snapshot(store) == before, 'manifest modified by inspection'
    return result


def main(base):
    root = base / 'game'
    for source in (root, str(root) + '\0suffix'):
        check('recognize', source)
        check('discover', source)
    root.mkdir()
    check('recognize', root)
    write(root, 'HUNTDAT')
    observed(root)
    (root / 'HUNTDAT').unlink()
    game(root, False)
    result = check('recognize', root)['value']
    assert result['recognized'] and result['diagnostics'][0]['code'] == 'missing-engine-evidence'
    observed(root)
    names = ['Z.REN', 'a.ExE', 'B.exe', 'carnivores2', 'CARNIVORES2-GL', 'not.exe.txt',
             '.exe', 'trailing.exe.', 'Σ.exe', 'ΟΣ.exe', 'İ.exe', 'ß.exe', '\ue000.exe', '𝄞.exe']
    if os.name == 'posix':
        names += ['raw\udcff.exe', 'back\\slash.exe', 'line\nbreak.exe']
    for n in names:
        write(root, n, n.encode('utf-8', 'surrogatepass'))
    (root / 'directory.exe').mkdir()
    observed(root)
    for spelling in ('', '.', './', './HUNTDAT/..', str(root) + '//./', 'C:relative', 'C:/abs', '\\rooted', '~missing-c2-discovery-user'):
        check('recognize', spelling, cwd=root)
        check('discover', spelling, cwd=root)
    # Path construction precedes expanduser, even when spelling starts with ./.
    for spelling in ('~', './~', '~/', './~/'):
        env_name = 'USERPROFILE' if os.name == 'nt' else 'HOME'
        old = os.environ.get(env_name)
        os.environ[env_name] = str(root)
        try:
            check('recognize', spelling, cwd=base)
            check('discover', spelling, cwd=base)
        finally:
            if old is None:
                del os.environ[env_name]
            else:
                os.environ[env_name] = old
    if os.name == 'posix':
        observed(game(base / 'literal\\root'))
    else:
        observed(Path('\\\\?\\' + str(root)))

    script = root / 'HUNTDAT/_RES.TXT'
    scripts = [b'CHARACTERS\t\n\v\f\r { WEAPONS {', b'xcharacters { weapons {',
               b'_characters { weapons {', b'0characters { weapons {', b'charactersX { weapons {',
               b'characters\x85{ weapons {', b'characters\xa0{ weapons {',
               b'\xffcharacters { \x80weapons {', b'characters\0{ weapons {',
               b'characters { } weapons{', b'characters { weapons', b'']
    prefix = b'characters { weapons {'
    scripts += [prefix + b' ' * (8 * 1024 * 1024 - len(prefix)),
                prefix + b' ' * (8 * 1024 * 1024 + 1 - len(prefix)),
                b' ' * (8 * 1024 * 1024) + prefix]
    scripts += [bytes([b]) + b'characters{ weapons{' for b in range(256)]
    scripts += [b'characters' + bytes([b]) + b'{ weapons{' for b in range(256)]
    for raw in scripts:
        script.write_bytes(raw)
        before = script.read_bytes()
        check('recognize', root)
        assert script.read_bytes() == before
    script.write_bytes(prefix)
    for entry in ('_RES.TXT', 'MENU', 'AREAS'):
        p = root / 'HUNTDAT' / entry
        saved = p.with_name(entry + '-saved')
        p.rename(saved)
        check('recognize', root)
        if saved.is_dir():
            p.write_bytes(b'not a directory')
        else:
            p.mkdir()
        check('recognize', root)
        if p.is_dir():
            p.rmdir()
        else:
            p.unlink()
        saved.rename(p)
    area = root / 'HUNTDAT/AREAS'
    for stem in ('TROPHY', 'trophY-room', 'Trophÿ', 'ß', 'SS', '𝄞', '\ue000', '.hidden'):
        write(area, stem + '.MAP')
        write(area, stem + '.rsc')
    observed(root)
    # Case collisions apply to RSC resolution, not MAP enumeration itself.
    write(area, 'collision.MAP'); write(area, 'COLLISION.map'); write(area, 'collision.rsc')
    check('recognize', root)
    write(area, 'COLLISION.RSC')
    check('recognize', root)
    for key in ('_res.txt', 'menu', 'areas'):
        p = root / 'HUNTDAT' / key
        if os.name == 'posix':
            p.write_bytes(b'collision')
            check('recognize', root)
            p.unlink()
    # Nested games survive; games inside asset trees are pruned.
    nested = game(base / 'tree/A/nested/game')
    game(base / 'tree/A')
    game(base / 'tree/A/HUNTDAT/not-a-game')
    game(base / 'tree/b')
    game(base / 'tree/𝄞')
    game(base / 'tree/\ue000')
    check('discover', base / 'tree')
    check('discover', '.', cwd=base / 'tree')
    check('discover', nested)

    clean = game(base / 'instance-game')
    store = base / 'store'
    i = instance(clean)
    manifest(store, [i])
    retained_baseline(store, i, clean)
    check('get_instance', store=str(store), identity=i['id'])
    unknown = check('get_instance', store=str(store), identity=str(uuid.UUID(int=999)))
    assert unknown['error'] == 'unknown instance ID'
    inspect(store, i)
    # Mutable content does not change content identity; engine bytes are independent.
    write(clean, 'HUNTDAT/slot.sav', b'mutable')
    assert not inspect(store, i)['value']['revision_changed']
    write(clean, 'HUNT.EXE', b'changed executable')
    result = inspect(store, i)['value']
    assert not result['revision_changed'] and result['engine_changed'] and result['engine_review_required']
    write(clean, 'HUNTDAT/AREAS/Island.MAP', b'changed map')
    result = inspect(store, i)['value']
    assert result['revision_changed'] and result['engine_changed']
    write(clean, 'HUNTDAT/AREAS/Island.MAP', b'x')
    write(clean, 'HUNT.EXE', b'candidate, never execute')
    assert not inspect(store, i)['value']['engine_review_required']
    # Pending relocation remains pending even after engine bytes return to baseline.
    i['engine_relocation_reviews'] = [{'status': 'required', 'observed_at': 'authored',
        'from': {'path': str(base / 'old'), 'path_flavor': os.name},
        'to': {'path': str(clean), 'path_flavor': os.name},
        'baseline_engine_evidence': copy.deepcopy(i['engine_evidence']),
        'destination_engine_evidence': []}]
    manifest(store, [i])
    assert inspect(store, i)['value']['engine_review_required']
    i.pop('engine_relocation_reviews')
    # Current revision equality allows integral floats through validated history membership.
    i['revision']['file_count'] = float(i['revision']['file_count'])
    i['revision']['byte_count'] = float(i['revision']['byte_count'])
    manifest(store, [i])
    assert not inspect(store, i)['value']['revision_changed']
    for extra in ({'unknown': {'a': [None, True, 1.0]}}, {'unknown': float('nan')}, {'unknown': float('inf')}):
        v = copy.deepcopy(i)
        v['revision'].update(extra)
        v['revisions'][0].update(extra)
        manifest(store, [v])
        assert inspect(store, v)['value']['revision_changed']
    # Nonfinite unrelated metadata survives get_instance and does not obstruct inspection.
    i['unknown']['nonfinite'] = float('nan')
    manifest(store, [i])
    assert not check('get_instance', store=str(store), identity=i['id'])['ok']
    assert not inspect(store, i)['value']['revision_changed']
    i['unknown'].pop('nonfinite')
    i['engine_evidence'][0]['extra'] = True
    manifest(store, [i])
    assert inspect(store, i)['value']['engine_changed']
    i['engine_evidence'][0].pop('extra')
    # Foreign shape has only recognized/diagnostics before review-required fields.
    foreign = copy.deepcopy(i)
    foreign.update(path='C:\\game' if os.name == 'posix' else '/game', path_flavor='nt' if os.name == 'posix' else 'posix')
    manifest(store, [foreign])
    result = inspect(store, foreign)['value']
    assert list(result) == ['recognized', 'diagnostics', 'engine_review_required']
    foreign['engine_relocation_reviews'] = [{'status': 'required', 'observed_at': 'authored',
        'from': {'path': foreign['path'], 'path_flavor': foreign['path_flavor']},
        'to': {'path': str(clean), 'path_flavor': os.name},
        'baseline_engine_evidence': copy.deepcopy(foreign['engine_evidence']), 'destination_engine_evidence': []}]
    manifest(store, [foreign]); assert inspect(store, foreign)['value']['engine_review_required']
    absent = copy.deepcopy(i); absent['path'] = str(base / 'absent')
    manifest(store, [absent]); inspect(store, absent)
    for value in (None, [], False, {}, [1]):
        raw = copy.deepcopy(i)
        raw['engine_relocation_reviews'] = value
        check('inspect_raw', instance=raw)
    for value in ('absent', None, []):
        raw = copy.deepcopy(i)
        if value == 'absent':
            raw.pop('engine_evidence')
        else:
            raw['engine_evidence'] = value
        check('inspect_raw', instance=raw)
    # Move candidates use full revision equality and preserve manifest insertion order.
    entries = []
    for n in (8, 3, 6, 2, 4):
        v = instance(clean, n)
        if n == 8:
            v.update(path=foreign['path'], path_flavor=foreign['path_flavor'])
        elif n != 6:
            v['path'] = str(base / ('old-' + str(n)))
        if n == 2:
            v['revision']['unknown'] = 1
            v['revisions'][0]['unknown'] = 1
        if n == 4:
            v['revision']['file_count'] = float(v['revision']['file_count'])
        entries.append(v)
    manifest(store, entries)
    result = check('move_candidates', clean, store=str(store))['value']
    assert result == [entries[n]['id'] for n in (0, 1, 4)]
    # No coherent-root precondition: zero-member regular HUNTDAT and boolean counts.
    empty = base / 'empty'
    write(empty, 'HUNTDAT', b'file')
    v = instance(empty, 15); v['path'] = str(base / 'missing-empty')
    v['revision']['file_count'] = False; v['revision']['byte_count'] = False
    manifest(store, [v]); assert check('move_candidates', empty, store=str(store))['value'] == [v['id']]
    # A one-byte/one-member revision permits bool/int equality in a valid current baseline.
    one = base / 'one'; write(one, 'HUNTDAT/member', b'x')
    v = instance(one, 16); v['path'] = str(base / 'missing-one')
    v['revision']['file_count'] = True; v['revision']['byte_count'] = True
    manifest(store, [v]); assert check('move_candidates', one, store=str(store))['value'] == [v['id']]
    # Recognition has no managed-capture entry/aggregate/member ceilings.
    large = game(base / 'large')
    for n in range(130):
        write(large, 'HUNTDAT/unrelated/' + str(n))
    for n in range(3):
        write(large, str(n) + '.exe', b'x' * (12 * 1024 * 1024))
    observed(large)
    if os.name == 'posix':
        posix_links(base)
        fifo = base / 'substituted-fifo'
        os.mkfifo(fifo)
        # Native-only operational distinction: owned O_NONBLOCK regular read
        # must reject a substituted FIFO, not enter Python's blocking open.
        got = native({'op': 'script_prefix', 'root': str(fifo)})
        assert not got['ok'] and 'not a regular file' in got['error']
        fifo.unlink()
        proc = Path('/proc/version')
        if proc.exists():
            assert proc.stat().st_size == 0
            assert check('script_prefix', proc)['value']



def retained_baseline(store, original, root):
    """Owned baseline outlives Store/Manifest and never rereads rewritten disk."""
    import queue
    import threading
    request = {'op': 'inspect_retained', 'store': str(store), 'identity': original['id']}
    child = subprocess.Popen([DRIVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    lines = queue.Queue()
    reader = threading.Thread(target=lambda: lines.put(child.stdout.readline()))
    try:
        child.stdin.write((json.dumps(request) + '\n').encode()); child.stdin.flush()
        reader.start()
        assert json.loads(lines.get(timeout=30)) == {'ready': True}
        reader.join(timeout=5); assert not reader.is_alive()
        old_bytes = (root / 'HUNT.EXE').read_bytes()
        (root / 'HUNT.EXE').write_bytes(b'fresh engine after retained baseline')
        updated = instance(root)
        manifest(store, [updated])
        expected = reference.inspect_instance(original)
        disk_before = snapshot(store)
        child.stdin.write(b'continue\n'); child.stdin.flush()
        stdout, stderr = child.communicate(timeout=90)
        assert child.returncode == 0, stderr
        got = json.loads(stdout)
        assert got['ok'] and got['json'] == json.dumps(expected, indent=2) + '\n'
        assert got['value']['engine_changed'] and not got['value']['revision_changed']
        assert snapshot(store) == disk_before
        (root / 'HUNT.EXE').write_bytes(old_bytes)
        manifest(store, [original])
    finally:
        if child.poll() is None:
            child.kill()
        child.wait(timeout=10)
        if reader.ident is not None:
            reader.join(timeout=5)
        child.stdin.close(); child.stdout.close(); child.stderr.close()


def posix_links(base):
    root = game(base / 'links')
    outside = write(base, 'outside-map')
    (root / 'HUNTDAT/AREAS/alias.map').symlink_to(outside)
    write(root, 'HUNTDAT/AREAS/alias.rsc')
    (root / 'alias.exe').symlink_to(root / 'HUNT.EXE')
    (root / 'dangling.exe').symlink_to(root / 'absent')
    (root / 'directory-link').symlink_to(root / 'HUNTDAT', target_is_directory=True)
    observed(root)
    # Contained HUNTDAT link: direct recognition sees it; discover filters it.
    inner = game(base / 'huntdat-link')
    (inner / 'HUNTDAT').rename(inner / 'assets')
    (inner / 'HUNTDAT').symlink_to(inner / 'assets', target_is_directory=True)
    assert check('recognize', inner)['value']['recognized']
    assert check('discover', inner)['value'] == []
    # Contained script aliases and source hardlinks remain valid evidence.
    script = inner / 'assets/_RES.TXT'
    script.rename(inner / 'script')
    script.symlink_to(inner / 'script')
    check('recognize', inner)
    os.link(inner / 'script', inner / 'hard.exe')
    observed(inner)
    old = base / 'dangling-old'; old.symlink_to(base / 'gone')
    clean = game(base / 'dangling-candidate'); i = instance(clean); i['path'] = str(old)
    store = base / 'link-store'; manifest(store, [i])
    assert check('move_candidates', clean, store=str(store))['value'] == [i['id']]
    alias = base / 'root-alias'; alias.symlink_to(clean, target_is_directory=True)
    observed(alias)


def capability(name, base):
    if name == 'permissions':
        if os.name != 'posix':
            return 77
        root = game(base / 'game')
        denied = game(base / 'denied')
        for d in (base, root, root / 'HUNTDAT', root / 'HUNTDAT/AREAS', root / 'HUNTDAT/MENU'):
            d.chmod(0o755)
        denied.chmod(0)
        identity = {'user': 65534, 'group': 65534, 'extra_groups': []} if os.geteuid() == 0 else {}
        try:
            code = 'import sys,json;sys.path.insert(0,sys.argv[1]);from lodge.discovery import discover;print(json.dumps(discover(sys.argv[2]),indent=2))'
            p = subprocess.run([sys.executable, '-c', code, str(Path(__file__).resolve().parents[2]), str(base)], capture_output=True, **identity)
            assert p.returncode == 0, p.stderr
            got = native({'op': 'discover', 'root': str(base)}, **identity)
            assert got['ok'] and got['json'].encode() == p.stdout
            assert len(got['value']) == 1
            script = root / 'HUNTDAT/_RES.TXT'; script.chmod(0)
            assert not native({'op': 'recognize', 'root': str(root)}, **identity)['ok']
            print('Passed: failed scandir omitted; unreadable script rejects recognition')
            return 0
        except PermissionError as e:
            print('permission identity unavailable:', e)
            return 77
        finally:
            denied.chmod(0o755)
            (root / 'HUNTDAT/_RES.TXT').chmod(0o644)
    if os.name != 'nt':
        print(name, 'unsupported on this platform')
        return 77
    root = game(base / 'game')
    target = game(base / 'target')
    link = root / 'nested'
    try:
        if name == 'file-link':
            (root / 'alias.exe').symlink_to(root / 'HUNT.EXE')
            (root / 'HUNTDAT/AREAS/alias.map').symlink_to(target / 'HUNTDAT/AREAS/Island.MAP')
            write(root, 'HUNTDAT/AREAS/alias.rsc')
            (root / 'linked-dir').symlink_to(target, target_is_directory=True)
        elif name == 'dangling-link':
            (root / 'dangling.exe').symlink_to(root / 'absent')
            old = base / 'old'; old.symlink_to(base / 'absent', target_is_directory=True)
            i = instance(root); i['path'] = str(old)
            store = base / 'store'; manifest(store, [i])
            assert check('move_candidates', root, store=str(store))['value'] == [i['id']]
        elif name == 'hardlink':
            os.link(root / 'HUNT.EXE', root / 'hard.exe')
            os.link(root / 'HUNTDAT/_RES.TXT', root / 'script-copy')
        elif name == 'unpaired-utf16':
            write(root, '\ud800.exe')
            write(root, 'HUNTDAT/AREAS/\ud800.map'); write(root, 'HUNTDAT/AREAS/\ud800.rsc')
            game(root / '\ud800')
        elif name in ('junction', 'cycle'):
            p = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(root if name == 'cycle' else target)], capture_output=True)
            if p.returncode:
                print(name, 'creation unavailable:', p.stderr)
                return 77
        else:
            raise AssertionError(name)
    except OSError as e:
        print(name, 'creation unavailable:', e)
        return 77
    try:
        if name == 'cycle':
            walk = os.walk(root, followlinks=False)
            assert len([next(walk) for _ in range(8)]) == 8
            walk.close()
            result = native({'op': 'discover', 'root': str(root)})
            assert not result['ok'] and 'discovery directory cycle' in result['error']
        else:
            if name == 'junction':
                other = root / 'second'
                p = subprocess.run(['cmd', '/c', 'mklink', '/J', str(other), str(target)], capture_output=True)
                assert p.returncode == 0, p.stderr
                try:
                    result = check('discover', root)['value']
                    assert len(result) == 3
                finally:
                    other.rmdir()
            observed(root)
            observed(Path('\\\\?\\' + str(root)))
        print('Passed:', name)
        return 0
    finally:
        if name in ('junction', 'cycle') and link.exists():
            link.rmdir()


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='c2-discovery-') as temp:
        if len(sys.argv) > 2:
            raise SystemExit(capability(sys.argv[2], Path(temp)))
        main(Path(temp))
    print(f'{CASES} discovery oracle cases; exact JSON bytes, retained baselines and source/manifest immutability')
