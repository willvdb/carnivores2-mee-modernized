"""Filesystem oracles call unchanged Python capture/resolve/inspect; no game assets."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import threading

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root))
sys.path.insert(0, str(root / 'tools'))
from lodge.store import Store, validate
from lodge.session_io import capture
from lodge.managed_state import resolve_generation, inspect_history
from generate_schema_fixtures import base, A, G0, G1, G2
cli, api = sys.argv[1:3]
count = 0

def run(args):
    return subprocess.run(list(map(str, args)), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)

def display(value):
    return (json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False) + '\n').encode()

def observed(pair):
    entries, blobs = pair
    return display(entries) + b''.join(str(len(b)).encode() + b'\n' + b for b in blobs.values())

def snapshot(root):
    result = {}
    def visit(p):
        s = p.lstat()
        v = (s.st_mode, s.st_nlink, s.st_size, s.st_mtime_ns)
        reparse = getattr(s, 'st_file_attributes', 0) & 1024
        if stat.S_ISLNK(s.st_mode): v += (os.readlink(p),)
        elif stat.S_ISREG(s.st_mode) and not reparse: v += (p.read_bytes(),)
        result[str(p.relative_to(root))] = v
        if stat.S_ISDIR(s.st_mode) and not reparse:
            for child in p.iterdir(): visit(child)
    visit(root)
    return result

def differential(args, oracle):
    global count
    try: expected = oracle(); error = None
    except (ValueError, OSError, RuntimeError) as e: error = e
    actual = run(args)
    if error is None:
        assert actual.returncode == 0 and actual.stdout == expected, (args, actual.returncode, actual.stderr, actual.stdout[:1000], expected[:1000])
    else:
        assert actual.returncode == 2 and not actual.stdout, (args, repr(error), actual)
    count += 1
    return error

def check_capture(path, parent=None):
    before = snapshot(parent) if parent else None
    error = differential([api, 'capture', path], lambda: observed(capture(path)))
    if parent: assert snapshot(parent) == before, ('capture wrote', path)
    return error

def write_manifest(directory, data):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'lodge.json').write_bytes(json.dumps(data, ensure_ascii=True).encode())

def fixture(directory, n=2, names=None):
    data = base(2, n)
    a = data['associations'][A]
    history = a['managed_state']
    for index, g in enumerate(history['generations'].values()):
        target = directory / g['snapshot']; target.mkdir(parents=True, exist_ok=True)
        blobs = names if names is not None else {'trophy00.sav': bytes(range(256)) + b'\0opaque-profile-' + bytes([index])}
        for name, content in blobs.items():
            file = target / name; file.parent.mkdir(parents=True, exist_ok=True); file.write_bytes(content)
        # Actual capture provides expected metadata. Nested member fixture below
        # deliberately strips directories only in input to expose exact mismatch.
        entries, _ = capture(target)
        g['members'] = [e for e in entries if e['type'] == 'file']
        if index == 0:
            a['files'] = [{**{k: e[k] for k in ('path', 'size', 'sha256')}, 'kind': 'sav'} for e in g['members']]
        else: history['receipts'][g['source_session']]['members'] = copy.deepcopy(g['members'])
    validate(data)
    write_manifest(directory, data)
    return data

def generation(directory, identity=None):
    store = Store(directory); data = store.read()
    return resolve_generation(store, data, data['associations'][A], identity)

def check_generation(directory, identity=None):
    args = [api, 'resolve', directory, A] + ([identity] if identity is not None else [])
    differential(args, lambda: observed(generation(directory, identity)[2:]))
    differential([api, 'generation', directory, A] + ([identity] if identity is not None else []),
                 lambda: display(generation(directory, identity)[0]))

def check_cli(directory):
    return differential([cli, '--store', directory, 'managed-state', 'inspect', A], lambda: display(inspect_history(Store(directory), A)))

# Separate capability cases make Passed/Skipped visible under the existing
# CTest --output-on-failure workflow. Keep the main oracle assertions above/below.
if len(sys.argv) == 4:
    case = sys.argv[3]
    assert case in ('file-link', 'dangling-link', 'unpaired-utf16')
    if os.name != 'nt': raise SystemExit(77)
    with tempfile.TemporaryDirectory(prefix='c2-capture-windows-') as temporary:
        parent = Path(temporary).resolve()
        state = parent / 'state'; state.mkdir()
        (state / 'bytes').write_bytes(bytes(range(256)))
        try:
            if case == 'unpaired-utf16':
                (state / 'unpaired-\ud800.sav').write_bytes(b'opaque-unpaired-name')
            else:
                (state / case).symlink_to('bytes' if case == 'file-link' else 'absent')
        except (OSError, UnicodeError) as error:
            print(f'{case} creation unavailable on this Windows host: {error!r}', flush=True)
            raise SystemExit(77)
        assert check_capture(state, parent) is None
        print(f'{case}: actual Python/native capture metadata and bytes matched', flush=True)
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix='c2-generation-') as temporary:
    parent = Path(temporary).resolve()
    state = parent / 'capture'; state.mkdir()
    assert check_capture(state, parent) is None
    assert check_capture(parent / 'missing', parent) is not None
    (state / 'empty').write_bytes(b''); (state / 'bytes').write_bytes(bytes(range(256)))
    (state / 'directory').mkdir(); (state / 'directory' / 'nested').write_bytes(b'nested')
    for name in ['z', 'a', 'É', 'é', 'Σ', 'ς', 'ΟΣ', 'İ', '\ue000', '🦖', '𐐀']:
        (state / name).write_bytes(name.encode())
    if os.name == 'posix':
        # Raw byte order disagrees with Unicode/surrogateescape ordering here.
        for name in [b'\xff', b'\xed\xa0\x80', b'\xc0\xaf', b'\xf0\x80\x80\x80', b'\xe2\x82', b'bad\\name', b'bad:name']:
            (state / os.fsdecode(name)).write_bytes(name)
        (state / 'aA').write_bytes(b'1'); (state / 'Aa').write_bytes(b'2')
        os.mkfifo(state / 'fifo')
    check_capture(state, parent)
    hard = state / 'hard'; os.link(state / 'bytes', hard); check_capture(state, parent); hard.unlink()
    if os.name == 'posix':
        (state / 'link').symlink_to('bytes'); (state / 'broken').symlink_to('absent')
        (state / 'dirlink').symlink_to('directory', target_is_directory=True)
        check_capture(state, parent)
        check_capture(state / 'dirlink', parent)
        check_capture(parent / 'capture' / '..' / 'capture', parent)
    else:
        # Symlink privilege varies across Windows hosts. Exercise each available
        # kind, and report denied creation explicitly rather than silently skip.
        for name, target_name in [('file-link', 'bytes'), ('dangling-link', 'absent')]:
            link = state / name
            try: link.symlink_to(target_name)
            except OSError as error: print(f'Windows symlink creation unavailable: {name}: {error}', flush=True)
            else:
                check_capture(state, parent)
                link.unlink()
        unpaired = state / 'unpaired-\ud800.sav'
        try: unpaired.write_bytes(b'opaque-unpaired-name')
        except (OSError, UnicodeError) as error: print(f'Windows unpaired UTF16 name unavailable: {error!r}', flush=True)
        else:
            check_capture(state, parent)
            unpaired.unlink()
        junction = parent / 'junction'
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(junction), str(state)], capture_output=True)
        assert result.returncode == 0, result.stderr
        check_capture(junction, parent)
        (state / 'junction').mkdir(); (state / 'junction').rmdir()
        result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(state / 'junction'), str(parent)], capture_output=True)
        assert result.returncode == 0, result.stderr
        check_capture(state, parent)
        os.rmdir(state / 'junction'); os.rmdir(junction)
        # Retain extended-prefix spelling throughout native path access.
        check_capture(Path('\\\\?\\' + str(state)), parent)
    shutil.rmtree(state); state.mkdir()
    for i in range(128): (state / f'{i:03}').write_bytes(b'')
    assert check_capture(state, parent) is None
    (state / '129').write_bytes(b''); assert check_capture(state, parent) is not None
    shutil.rmtree(state); state.mkdir()
    limit = 16 * 1024 * 1024
    (state / 'a').write_bytes(b'x' * limit)
    assert check_capture(state, parent) is None
    (state / 'b').write_bytes(b'y' * limit)
    assert check_capture(state, parent) is None
    (state / 'c').write_bytes(b'z')
    check_capture(state, parent)  # third entry oversized by aggregate accounting
    with (state / 'a').open('ab') as out: out.write(b'x')
    check_capture(state, parent)  # oversized a still counts towards total, making b oversized too
    shutil.rmtree(state)
    # Virtual regular files retain st_size==0 while returning data. Capture must
    # reject, never return a falsely certified empty blob. Environment conditional.
    virtual = Path('/proc/sys/kernel/random')
    if os.name == 'posix' and virtual.is_dir():
        assert check_capture(virtual) is not None

    for n in range(3):
        directory = parent / f'g{n}'
        data = fixture(directory, n)
        before = snapshot(parent)
        check_generation(directory); check_cli(directory)
        for gid in list(data['associations'][A]['managed_state']['generations']): check_generation(directory, gid)
        assert snapshot(parent) == before
    directory = parent / 'g2'; data = Store(directory).read()
    history = data['associations'][A]['managed_state']
    for old in [G0, G1]:
        p = run([api, 'history', directory, A, old])
        assert p.returncode == 2 and not p.stdout
    check_generation(directory, 'not-a-uuid'); check_generation(directory, '00000000-0000-0000-0000-ffffffffffff')
    differential([cli, '--store', directory, 'managed-state', 'inspect', 'unknown'], lambda: display(inspect_history(Store(directory), 'unknown')))
    # Deleting old snapshots cannot affect the current head or historical
    # executable/helper evidence (these paths have never existed in the fixture).
    for gid in [G0, G1]: shutil.rmtree(directory / history['generations'][gid]['snapshot'])
    check_generation(directory); check_cli(directory)
    data = fixture(directory, 2)
    target = directory / data['associations'][A]['managed_state']['generations'][G2]['snapshot']
    for damage in ['missing', 'corrupt', 'empty', 'extra-directory', 'hardlink']:
        if target.exists(): shutil.rmtree(target)
        if damage != 'missing':
            target.mkdir(parents=True)
            if damage == 'corrupt': (target / 'trophy00.sav').write_bytes(b'wrong')
            if damage in ['extra-directory', 'hardlink']:
                (target / 'trophy00.sav').write_bytes(bytes(range(256)) + b'\0opaque-profile-\2')
                if damage == 'extra-directory': (target / 'extra').mkdir()
                else: os.link(target / 'trophy00.sav', target / 'second')
        assert check_cli(directory) is not None
        check_generation(directory)
        for old in [G0, G1]:
            check_generation(directory, old)
            p = run([api, 'history', directory, A, old]); assert p.returncode == 2 and not p.stdout
    shutil.rmtree(directory); data = fixture(directory, 2)
    # Unknown metadata preserves insertion order, arbitrary integer/Unicode and
    # historical pins. Only the three reference import-provenance keys disappear.
    a = data['associations'][A]; a['forward'] = {'z': '\ud800🦖', 'a': [True, 1, 1.0, 10**90]}
    a['last_observation'] = {'unrelated': 'omit only here'}
    a['codec'] = {'helper': '/absent/never-probe', 'sha256': 'b'*64}
    a['managed_state']['future'] = {'retained': True}
    a['managed_state']['generations'][G2]['extra'] = [1, {'z': 2, 'a': 3}]
    write_manifest(directory, data); check_cli(directory); check_generation(directory)
    # Member extras can validate for accepted generations yet must fail complete
    # captured-entry equality, including extras with null or Python-equal numbers.
    for extra in [None, True, 1, 1.0, {'nested': [float('nan')]}]:
        d = copy.deepcopy(data); h = d['associations'][A]['managed_state']; g = h['generations'][G2]
        g['members'][0]['extra'] = extra
        h['receipts'][g['source_session']]['members'] = copy.deepcopy(g['members'])
        write_manifest(directory, d); assert check_cli(directory) is not None; check_generation(directory)
    write_manifest(directory, data)
    # Opaque immutable observation owns bytes after paths/manifest are removed.
    expected = observed(generation(directory)[2:])
    child = subprocess.Popen([api, 'owned', str(directory), A], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert child.stdout.readline() == b'ready\n'
    shutil.rmtree(directory)
    output, error = child.communicate(b'go\n', timeout=60)
    assert child.returncode == 0 and output == expected, error
    data = fixture(directory, 2)
    expected = observed(generation(directory)[2:])
    child = subprocess.Popen([api, 'manifest-owned', str(directory), A], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert child.stdout.readline() == b'ready\n'
    (directory / 'lodge.json').write_bytes(b'corrupt')
    output, error = child.communicate(b'go\n', timeout=60)
    assert child.returncode == 0 and output == expected, error
    # No hidden reread or identity reassociation; root aliases after read reject.
    write_manifest(directory, data)
    if os.name == 'posix':
        child = subprocess.Popen([api, 'manifest-owned', str(directory), A], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert child.stdout.readline() == b'ready\n'
        moved = parent / 'moved'; directory.rename(moved); directory.symlink_to(moved, target_is_directory=True)
        output, error = child.communicate(b'go\n', timeout=60)
        assert child.returncode == 2 and not output, error
        directory.unlink(); moved.rename(directory)
    for version in [1, 2]:
        d = base(version)
        if version == 2:
            d['associations'][A].update(ownership='referenced', authority='native-files')
        write_manifest(directory, d); assert check_cli(directory) is not None
    nested = parent / 'nested'
    fixture(nested, 0, {'sub/trophy00.sav': b'x'})
    assert check_cli(nested) is not None  # directories are never filtered away
    unusual = parent / 'unusual'
    fixture(unusual, 0, {'É.sav': b'x', '🦖.sav': b'y'})
    check_cli(unusual)
    if os.name == 'posix':
        raw = parent / os.fsdecode(b'store-\xff')
        fixture(raw, 0, {os.fsdecode(b'trophy-\xff.sav'): b'opaque'})
        check_cli(raw)
    # Ordinary concurrent same-size writes: no production test hook. Each result
    # must reject or be a full valid observation; sustained mutation must be seen.
    changing = parent / 'changing'; changing.mkdir(); file = changing / 'state'
    file.write_bytes(b'a' * limit)
    stop = threading.Event(); started = threading.Event()
    def writer():
        with file.open('r+b', buffering=0) as out:
            n = 0
            while not stop.is_set():
                out.seek(0); out.write(bytes([n % 256]) * 65536); n += 1; started.set()
    thread = threading.Thread(target=writer); thread.start(); started.wait()
    try:
        failures = 0
        for _ in range(3):
            p = run([api, 'capture', changing])
            assert p.returncode in (0, 2), p.stderr
            if p.returncode == 2: failures += 1
            else:
                # ASCII JSON entry projection precedes length-framed raw blobs.
                end = p.stdout.index(b'\n]\n') + 3
                entries = json.loads(p.stdout[:end])
                remaining = p.stdout[end:]
                assert len(entries) == 1 and entries[0]['type'] == 'file'
                size_line, remaining = remaining.split(b'\n', 1)
                size = int(size_line)
                assert size == limit == entries[0]['size'] and len(remaining) == size
                assert hashlib.sha256(remaining).hexdigest() == entries[0]['sha256']
        assert failures, 'sustained writer was not detected'
    finally: stop.set(); thread.join()
print(f'{count} generation/capture differential checks passed')
