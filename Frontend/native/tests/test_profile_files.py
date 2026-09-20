#!/usr/bin/env python3
"""Source profile API against unchanged Python and the original compiled helper."""
import hashlib
import io
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unicodedata

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import profiles

DRIVER, PROBE = map(str, map(Path, sys.argv[1:3]))
ENV = dict(os.environ)
ENV.pop('C2_PROFILE_PROBE', None)
os.environ.pop('C2_PROFILE_PROBE', None)
CASES = 0


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def invoke(mode, root, key=None, codec='native', dialect='unknown', cwd=None):
    args = [DRIVER, mode, str(root)]
    if key is not None:
        args += [key, codec, dialect]
    return subprocess.run(args, capture_output=True, env=ENV, cwd=cwd, timeout=60)


def snapshot(root):
    result = []
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in sorted(dirs + files):
            p = Path(parent) / name
            s = p.lstat()
            result.append((str(p), s.st_mode, s.st_size, s.st_mtime_ns,
                           os.readlink(p) if p.is_symlink() else
                           hashlib.sha256(p.read_bytes()).digest() if p.is_file() else None))
    return result


def frame(stream):
    line = stream.readline()
    assert line and line.rstrip().isdigit(), line
    n = int(line)
    data = stream.read(n)
    assert len(data) == n
    return data


def check_inventory(root, cwd=None):
    global CASES
    before = Path.cwd()
    try:
        if cwd:
            os.chdir(cwd)
        expected = profiles.inventory(root)
    finally:
        os.chdir(before)
    actual = invoke('inventory', root, cwd=cwd)
    assert actual.returncode == 0, (root, actual.stderr)
    assert actual.stdout == encoded(expected), (root, actual.stdout, encoded(expected))
    CASES += 1
    return expected


def check_set(root, state, codec='native', dialect='unknown'):
    global CASES
    probe = PROBE if codec == 'native' else None
    try:
        raw = profiles.stable_read(root, state)
        expected = profiles.inspect_set(root, state, probe, dialect)
    except Exception as error:
        actual = invoke('inspect', root, state['key'], codec, dialect)
        assert actual.returncode == 2, (state, error, actual.stdout, actual.stderr)
        CASES += 1
        return
    actual = invoke('inspect', root, state['key'], codec, dialect)
    assert actual.returncode == 0, (state, actual.stderr)
    stream = io.BytesIO(actual.stdout)
    actual_json = frame(stream)
    assert actual_json == encoded(expected), (state['key'], actual_json, encoded(expected))
    for entry in state['files']:
        assert frame(stream) == raw[entry['path']]
    assert stream.read() == b''
    for mode in ('read', 'stable'):
        actual = invoke(mode, root, state['key'])
        assert actual.returncode == 0, actual.stderr
        stream = io.BytesIO(actual.stdout)
        for entry in state['files']:
            assert frame(stream) == raw[entry['path']]
        assert not stream.read()
    CASES += 1


def check_tree(root, inspect=True):
    before = snapshot(root)
    states = check_inventory(root)
    if inspect:
        for state in states:
            check_set(root, state)
    assert snapshot(root) == before, 'read-only operation changed source'
    return states


def write(root, name, content=b'x'):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)
    return path


def save(slot=0, name=b'Name', fill=0):
    b = bytearray([fill] * 1660)
    b[:len(name)] = name
    struct.pack_into('<i', b, 128, slot)
    return bytes(b)


def waiting(mode, root, key, mutation, codec='native', dialect='unknown'):
    p = subprocess.Popen([DRIVER, mode, str(root), key, codec, dialect],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=ENV)
    try:
        assert p.stdout.readline() == b'ready\n'
        mutation()
        out, err = p.communicate(b'continue\n', timeout=60)
        return p.returncode, out, err
    finally:
        if p.poll() is None:
            p.kill()
            p.communicate()


def changes(root):
    global CASES
    root.mkdir()
    p = write(root, 'trophy00.sav', save())
    def compare_change(change, reset, expected_error=None):
        global CASES
        state = profiles.inventory(root)[0]
        code, out, err = waiting('wait-stable', root, state['key'], change)
        try:
            expected = profiles.stable_read(root, state)
        except Exception as e:
            assert code == 2 and str(e).encode() in err, (e, err)
            if expected_error:
                assert expected_error in str(e)
        else:
            assert code == 0, err
            stream = io.BytesIO(out)
            for entry in state['files']:
                assert frame(stream) == expected[entry['path']]
        reset()
        CASES += 1
    # Changed state bytes/extent before the first read are allowed, not compared
    # against the old inventory as if that were an authoritative baseline.
    compare_change(lambda: p.write_bytes(b'new bytes'), lambda: p.write_bytes(save()))
    room = root / 'trophy00.sab'
    compare_change(lambda: room.write_bytes(b'room'), lambda: room.unlink(), 'membership')
    comp = root / 'trophy00.extra'
    compare_change(lambda: comp.write_bytes(b'companion'), lambda: comp.unlink(), 'companion inventory')
    comp.write_bytes(b'old')
    compare_change(lambda: comp.write_bytes(b'longer'), lambda: comp.write_bytes(b'old'), 'companion inventory')
    # Companion bytes are not observed/hashed by the reference, only metadata.
    compare_change(lambda: comp.write_bytes(b'new'), lambda: comp.write_bytes(b'old'))
    compare_change(lambda: comp.unlink(), lambda: comp.write_bytes(b'old'), 'companion inventory')
    comp.unlink()
    state = profiles.inventory(root)[0]
    code, out, err = waiting('wait-inspect', root, state['key'], lambda: p.write_bytes(b'changed size'))
    assert code == 0, err
    stream = io.BytesIO(out)
    assert frame(stream) == encoded(profiles.inspect_set(root, state, PROBE))
    assert frame(stream) == b'changed size'
    p.write_bytes(save())
    expected = profiles.inspect_set(root, profiles.inventory(root)[0], PROBE)
    code, out, err = waiting('owned', root, 'trophy00', lambda: p.unlink())
    assert code == 0, err
    stream = io.BytesIO(out)
    assert frame(stream) == encoded(expected)
    assert frame(stream) == save()
    p.write_bytes(save())
    # Replacement by special files is rejected without blocking.
    if os.name != 'nt':
        def fifo():
            p.unlink()
            os.mkfifo(p)
        code, out, err = waiting('wait-read', root, 'trophy00', fifo)
        assert code == 2 and b'regular file' in err
        p.unlink()
        p.write_bytes(save())
    CASES += 3



def active_writer(root):
    global CASES
    root.mkdir()
    path = write(root, 'trophy00.sav', b'0' * (4 * 1024 * 1024))
    ready, stop = threading.Event(), threading.Event()
    errors = []
    def writer():
        try:
            with path.open('r+b', buffering=0) as stream:
                pattern = 0
                while not stop.is_set():
                    stream.seek(0)
                    block = bytes([pattern]) * (64 * 1024)
                    for _ in range(64):
                        stream.write(block)
                    pattern ^= 255
                    ready.set()
        except BaseException as error:
            errors.append(error)
            ready.set()
    thread = threading.Thread(target=writer)
    thread.start()
    rejected = 0
    try:
        assert ready.wait(30) and not errors
        for _ in range(12):
            actual = invoke('inspect', root, 'trophy00')
            if actual.returncode == 2:
                assert b'changed' in actual.stderr, actual.stderr
                rejected += 1
            else:
                assert actual.returncode == 0, actual.stderr
                stream = io.BytesIO(actual.stdout)
                result = json.loads(frame(stream))
                raw = frame(stream)
                assert not stream.read()
                entry = result['files'][0]
                assert entry['size'] == len(raw) == 4 * 1024 * 1024
                assert entry['sha256'] == hashlib.sha256(raw).hexdigest()
                assert entry['decoded'] == profiles.codec_inspect(raw, 'sav', PROBE)
                assert result['diagnostics'][-1]['code'] == 'pair-coherence-unverified'
                # No atomic-content claim: a valid pair of equal observations
                # can still contain mixed writer chunks, just as Python can.
            CASES += 1
    finally:
        stop.set()
        thread.join(timeout=30)
    assert not thread.is_alive() and not errors
    assert rejected, 'active sustained writer must exercise a detected change'
    check_tree(root)


def capability(name, base):
    if name == 'permissions':
        if os.name == 'nt':
            return 77
        root = base / name
        root.mkdir()
        child = root / 'denied'
        write(child, 'trophy00.sav', save())
        p = write(root, 'trophy01.sav', save(1))
        base.chmod(0o755)
        root.chmod(0o755)
        child.chmod(0)
        p.chmod(0)
        identity = {'user': 65534, 'group': 65534, 'extra_groups': []} if os.geteuid() == 0 else {}
        try:
            # Run BOTH implementations with the same restricted identity. This
            # remains a real permission test when CI starts as root.
            script = ('import json,sys;sys.path.insert(0,sys.argv[1]);'
                      'from lodge.profiles import inventory;'
                      'print(json.dumps(inventory(sys.argv[2]),indent=2))')
            py = subprocess.run([sys.executable, '-c', script, str(Path(__file__).resolve().parents[2]), str(root)],
                                capture_output=True, env=ENV, **identity)
            native = subprocess.run([DRIVER, 'inventory', str(root)], capture_output=True, env=ENV, **identity)
            assert py.returncode == native.returncode == 0, (py.stderr, native.stderr)
            assert py.stdout == native.stdout
            values = json.loads(py.stdout)
            assert len(values) == 1 and values[0]['key'] == 'trophy01', values
            native = subprocess.run([DRIVER, 'read', str(root), 'trophy01'], capture_output=True, env=ENV, **identity)
            assert native.returncode == 2, native.stdout
            print('actual permission-denied directory omitted; unreadable member rejected')
            return 0
        except PermissionError as e:
            print('permission identity unavailable:', e)
            return 77
        finally:
            child.chmod(0o700)
            p.chmod(0o600)
    if os.name != 'nt':
        return 77
    root = base / name
    root.mkdir()
    outside = base / (name + '-outside')
    outside.mkdir()
    write(root, 'trophy00.sav', save())
    write(outside, 'trophy01.sav', save(1))
    try:
        if name == 'file-link':
            (root / 'trophy02.sav').symlink_to(root / 'trophy00.sav')
            (root / 'internal').symlink_to(root, target_is_directory=True)
            (base / 'root-link').symlink_to(root, target_is_directory=True)
        elif name == 'dangling-link':
            (root / 'trophy02.sab').symlink_to(root / 'absent')
        elif name == 'unpaired-utf16':
            write(root, '\ud800/trophy03.sav', save(3))
        elif name == 'hardlink':
            os.link(root / 'trophy00.sav', root / 'trophy02.sav')
            os.link(outside / 'trophy01.sav', root / 'trophy03.sav')
        elif name in ('junction', 'cycle'):
            p = subprocess.run(['cmd', '/c', 'mklink', '/J', str(root / 'junction'), str(root if name == 'cycle' else outside)], capture_output=True)
            if p.returncode:
                print('junction unavailable:', p.stderr.decode(errors='replace'))
                return 77
        else:
            raise AssertionError(name)
    except OSError as e:
        print(name, 'creation unavailable:', e)
        return 77
    if name == 'cycle':
        # Bounded unchanged traversal evidence, never unbounded inventory.
        from lodge.discovery import walk_files
        walk = walk_files(root)
        paths = [next(walk).relative_to(root).as_posix() for _ in range(3)]
        walk.close()
        assert paths == ['trophy00.sav', 'junction/trophy00.sav', 'junction/junction/trophy00.sav'], paths
        actual = invoke('inventory', root)
        assert actual.returncode == 2 and b'directory cycle' in actual.stderr and not actual.stdout
        return 0
    if name == 'junction':
        # Two finite internal aliases must both be preserved, no global dedup.
        write(root, 'real/trophy04.sav', save(4))
        for alias in ('alias-a', 'alias-b'):
            p = subprocess.run(['cmd', '/c', 'mklink', '/J', str(root / alias), str(root / 'real')], capture_output=True)
            assert p.returncode == 0, p.stderr
        states = check_tree(root)
        assert {'alias-a/trophy04', 'alias-b/trophy04', 'real/trophy04'} <= {s['key'] for s in states}
    else:
        check_tree(root)
    if name == 'unpaired-utf16':
        check_tree(Path('\\\\?\\' + str(root)))
    if name == 'file-link':
        check_tree(base / 'root-link')
    if name == 'junction':
        # Junction is traversed by inventory; out-of-root read is rejected.
        assert any(s['key'] == 'junction/trophy01' for s in profiles.inventory(root))
        check_tree(root / 'junction')
    return 0


def main(base):
    global CASES
    root = base / 'profiles'
    root.mkdir()
    check_inventory(base / 'missing')
    ordinary = write(root, 'ordinary', b'file')
    check_inventory(ordinary)
    check_inventory(ordinary / 'child')
    check_inventory(root)
    check_inventory('', cwd=root)
    assert invoke('resolve-nul', root).returncode == 0
    nul = invoke('nul', root)
    assert nul.returncode == 0 and nul.stdout == encoded(profiles.inventory(str(root) + '\0suffix'))
    # Each decimal block plus unusual Python IGNORECASE and spelling behavior.
    names = ['trophy00.sav', 'trophy00.sab', 'trophy00.extra', 'trophy0.SAV',
             'trophy08.sab', 'TROPHY03.ſav', 'trophy03.ſab', 'trophy9999999999999999999999999999999999.sav',
             'trophy٠١.sav', 'trophy𝟘𝟚.SAB', 'trophy00.a\nb', 'trophy00.\n',
             'trophy00.sav\n', 'trophy00.', 'trophy00..', 'trophy⁰⁰.sav',
             'trophy-1.sav', 'not-trophy00.sav', 'trophy00.SAV.bak', 'Trophy05.SaV',
             'Pack/trophy00.sav', 'pack/trophy00.sab', 'Σ/trophy01.sav',
             'ΣΣ/trophy01.bak', 'Straße/trophy000.sav', '𝄞/trophy01.sav',
             '\ue000/trophy01.sav', 'backup/trophy001.sav', './~/trophy02.sav']
    if os.name == 'nt':
        names = [n for n in names if '\n' not in n and not n.endswith('.')]
    else:
        names += ['back\\slash/trophy01.sav', 'raw\udcff/trophy01.sav']
    for name in names:
        write(root, name)
    for cp in range(0x110000):
        if unicodedata.decimal(chr(cp), -1) == 0:
            write(root, 'digits-' + str(cp) + '/trophy' + chr(cp) + chr(cp + 9) + '.sav')
    states = check_tree(root)
    for state in states[:12]:
        check_set(root, state, 'unavailable')
        check_set(root, state, 'native', 'iceage-triassic')
    check_inventory('.', cwd=root)
    check_inventory('~', cwd=root)
    check_inventory('../profiles', cwd=root)
    # Cases are collision-capability dependent; assert actual on-disk inventory.
    collision = base / 'collision'
    collision.mkdir()
    write(collision, 'trophy00.sav')
    write(collision, 'TROPHY00.SAV')
    check_tree(collision)
    # Native projection and raw-byte retention over full Latin-1, all zero/FF,
    # opaque, wrong dialect, arbitrary sizes, and registration mismatch.
    data = base / 'data'
    data.mkdir()
    randomizer = random.Random(721)
    payloads = [b'', b'x', save(), save(6), save(-1), save(0, bytes(range(128))),
                save(0, bytes(range(128, 256))), bytes([255]) * 1660,
                bytes(randomizer.randrange(256) for _ in range(1660)), bytes(1659), bytes(1661)]
    for i, payload in enumerate(payloads):
        write(data, f'case{i}/trophy00.sav', payload)
        write(data, f'case{i}/trophy00.sab', bytes((j + i) % 256 for j in range(7176)))
    for state in check_tree(data):
        check_set(data, state, 'unavailable')
        check_set(data, state, 'native', 'iceage-triassic')
    large = base / 'large'
    large.mkdir()
    write(large, 'trophy00.sav', b'B' * (16 * 1024 * 1024))
    write(large, 'trophy01.sav', b'C' * (16 * 1024 * 1024 + 1))
    check_tree(large)
    # More than capture's 128 entries and 32MiB aggregate are legitimate source
    # inventory/read observations; there is only a per-state-file byte ceiling.
    many = base / 'many'
    many.mkdir()
    for i in range(130):
        write(many, f'trophy{i:02}.sav', b'a')
    assert len(check_inventory(many)) == 130
    write(many, 'trophy00.sav', b'A' * (16 * 1024 * 1024))
    write(many, 'trophy00.sab', b'B' * (16 * 1024 * 1024))
    write(many, 'trophy00.extra', b'X')
    check_set(many, profiles.inventory(many)[0])
    changes(base / 'changes')
    active_writer(base / 'active-writer')
    # An observation retains its absolute lexical root across process cwd changes.
    p = subprocess.run([DRIVER, 'cwd', 'data', 'case0/trophy00', str(root)], cwd=base,
                       capture_output=True, env=ENV, timeout=60)
    assert p.returncode == 0, p.stderr
    if os.name != 'nt':
        links = base / 'links'
        links.mkdir()
        real = base / 'real'
        write(real, 'nested/trophy00.sav', save())
        write(real, 'trophy01.sav', save(1))
        write(links, 'trophy02.sav', save(2))
        os.link(links / 'trophy02.sav', links / 'trophy03.sav')
        (links / 'trophy04.sav').symlink_to(real / 'trophy01.sav')
        (links / 'trophy05.sav').symlink_to(links / 'trophy02.sav')
        (links / 'trophy06.sab').symlink_to(links / 'missing')
        (links / 'outside').symlink_to(real, target_is_directory=True)
        (links / 'inside').symlink_to(links, target_is_directory=True)
        os.mkfifo(links / 'trophy07.sav')
        check_tree(links)
        (base / 'root-link').symlink_to(real, target_is_directory=True)
        check_tree(base / 'root-link')
        # Collapsing link/.. lexically would incorrectly inspect base here.
        (base / 'deep-link').symlink_to(real / 'nested', target_is_directory=True)
        check_tree(base / 'deep-link' / '..')
        check_tree(Path('//' + str(real).lstrip('/')))
        target = links / 'trophy02.sav'
        def substitute():
            target.unlink()
            target.symlink_to(real / 'trophy01.sav')
        code, out, err = waiting('wait-read', links, 'trophy02', substitute)
        assert code == 2 and b'escaped its root' in err
        # Exercise the exact private reader used by the public state API on an
        # actual virtual regular file, never a stat-sized simulated byte buffer.
        actual = invoke('source-read', '/proc', 'version')
        if Path('/proc/version').exists():
            assert actual.returncode == 0, actual.stderr
            assert frame(io.BytesIO(actual.stdout)) == Path('/proc/version').read_bytes()
        denied = base / 'denied'
        write(denied, 'trophy00.sav')
        denied.chmod(0)
        try:
            check_inventory(denied)
        finally:
            denied.chmod(0o700)
    else:
        extended = Path('\\\\?\\' + str(data))
        check_tree(extended)
        # Direct reader materialization must also translate nested presentation
        # separators under an extended prefix, retaining all native code units.
        for relative in ('case0/trophy00.sab', 'case1/trophy00.sav'):
            actual = invoke('source-read', extended, relative)
            assert actual.returncode == 0, actual.stderr
            stream = io.BytesIO(actual.stdout)
            assert frame(stream) == (extended / relative).read_bytes()
            assert not stream.read()
    CASES += 8


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='c2-profile-files-') as temp:
        if len(sys.argv) == 4:
            raise SystemExit(capability(sys.argv[3], Path(temp)))
        main(Path(temp))
    print(f'{CASES} source-profile oracle cases passed; exact JSON/raw bytes and no-write snapshots')
