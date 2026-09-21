"""Differential write-primitive tests against unchanged Python store/session_io.

Disposable stores only. Modes: default (portable), file-link (symlink aliases;
skip 77 when the host cannot create links), posix (durability phases, modes,
FIFOs and raw names; skip 77 elsewhere).
"""
import copy
import json
import os
from datetime import datetime, timezone
from pathlib import Path
import socket
import stat
import subprocess
import sys
import tempfile
import uuid
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root))
sys.path.insert(0, str(root / 'tools'))
import lodge.store as store_module
from lodge.store import FrontendError, Store, atomic_write, empty_manifest, valid_id
from lodge.session_io import capture, session_root, write_blobs
from generate_schema_fixtures import base, H, I
api = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else 'default'
count = 0
LOCK_MESSAGE = 'frontend writer lock exists: {}; verify its owner before manual recovery'


def run(args, stdin=b'', env=None, timeout=60):
    return subprocess.run(list(map(str, args)), input=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, env=env)


def framed(payload):
    if not isinstance(payload, bytes):
        payload = json.dumps(payload, ensure_ascii=True).encode()
    return str(len(payload)).encode() + b'\n' + payload


def snapshot(top):
    # Names, kinds, link counts, sizes and exact regular bytes; no timestamps.
    out = {}
    def visit(p):
        s = p.lstat()
        value = (stat.S_IFMT(s.st_mode), s.st_nlink, s.st_size)
        if stat.S_ISLNK(s.st_mode):
            target = Path(os.readlink(p))
            value += (target.relative_to(top).as_posix() if target.is_absolute() and target.is_relative_to(top) else str(target),)
        elif stat.S_ISREG(s.st_mode): value += (p.read_bytes(),)
        out[p.relative_to(top).as_posix()] = value
        if stat.S_ISDIR(s.st_mode) and not getattr(s, 'st_file_attributes', 0) & 1024:
            for child in p.iterdir(): visit(child)
    visit(top)
    return out


def residue(directory):
    return sorted(p.name for p in Path(directory).glob('.pending-*')) if Path(directory).is_dir() else []


def python_outcome(action):
    try:
        return action(), None
    except (ValueError, OSError, RuntimeError) as error:
        return None, error


AUTHORED = ('frontend writer lock exists', 'invalid session UUID', 'unsafe captured state path', 'session path', 'session file', 'manifest missing with backup', 'state ')


def expect(result, error, translate=None):
    """Native exit classes: 0 ok, 2 FrontendError, 4 injected, 5 OSError, 6 callback.

    Messages authored by this slice (and the safe-path/read refusals it reuses)
    are compared exactly; the 1A validator and parser keep their reviewed texts.
    """
    global count
    count += 1
    if error is None:
        assert result.returncode == 0, (result.returncode, result.stderr, result.stdout)
    elif isinstance(error, FrontendError):
        assert result.returncode == 2 and not result.stdout, (result.returncode, result.stderr, str(error))
        expected = str(error)
        if translate: expected = expected.replace(str(translate[0]), str(translate[1]))
        if expected.startswith(AUTHORED):
            assert result.stderr == f'error: {expected}\n'.encode(), (result.stderr, expected)
    elif isinstance(error, RuntimeError):
        assert result.returncode == 6, (result.returncode, result.stderr)
    elif isinstance(error, OSError):
        assert result.returncode in (2, 5) and not result.stdout, (result.returncode, result.stderr, repr(error))
    else:
        assert result.returncode == 2 and not result.stdout, (result.returncode, result.stderr, repr(error))


def write(directory, data):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'lodge.json').write_bytes(data if isinstance(data, bytes) else json.dumps(data, ensure_ascii=True).encode())


def apply(data, ops):
    for op in ops:
        if op[0] == 'throw': raise RuntimeError('authored callback failure')
        if op[0] == 'replace': data.clear(); data.update(op[1]); continue
        parent = data
        for key in op[1][:-1]: parent = parent[key]
        last = op[1][-1]
        if op[0] == 'set':
            if isinstance(parent, list) and last == len(parent): parent.append(op[2])
            else: parent[last] = op[2]
        elif op[0] == 'del': del parent[last]
        else: raise RuntimeError('unknown authored op')


def python_transaction(directory, ops):
    writes = []
    def counting(path, content):
        writes.append(path.name); return atomic_write(path, content)
    with patch.object(store_module, 'atomic_write', counting):
        with Store(directory).transaction() as data:
            apply(data, ops)
    return 'lodge.json' in writes


def transaction_case(parent, name, initial, ops, extra=None):
    """Same initial tree for Python and native; compare outcome and final trees."""
    trees = {}
    for label in ('python', 'native'):
        directory = parent / f'{name}-{label}' / 'store'
        if initial is not None: write(directory, copy.deepcopy(initial))
        if extra: extra(directory)
        trees[label] = directory
    # Planted foreign locks/pending files must survive untouched; our own must not remain.
    planted = ((trees['native'] / 'lodge.lock').is_symlink() or (trees['native'] / 'lodge.lock').exists(), residue(trees['native']))
    expected, error = python_outcome(lambda: python_transaction(trees['python'], ops))
    result = run([api, 'transaction', trees['native']], framed(ops))
    expect(result, error, (trees['python'], trees['native']))
    if error is None:
        assert result.stdout == f'written {int(expected)}\n'.encode(), (name, result.stdout, expected)
    assert snapshot(trees['python'].parent) == snapshot(trees['native'].parent), name
    for directory in trees.values():
        assert ((directory / 'lodge.lock').is_symlink() or (directory / 'lodge.lock').exists(), residue(directory)) == planted, name
    return error


def blob_case(parent, name, blobs, prepare=None, target='out'):
    trees = {}
    for label in ('python', 'native'):
        directory = parent / f'{name}-{label}'
        directory.mkdir()
        if prepare: prepare(directory)
        trees[label] = directory
    _, error = python_outcome(lambda: write_blobs(trees['python'] / target, dict(blobs)))
    header = json.dumps([[k, len(v)] for k, v in blobs], ensure_ascii=True).encode()
    result = run([api, 'write-blobs', trees['native'] / target], framed(header) + b''.join(v for _, v in blobs))
    expect(result, error, (trees['python'], trees['native']))
    python_tree, native_tree = snapshot(trees['python']), snapshot(trees['native'])
    if isinstance(error, FrontendError) and str(error) == 'unsafe captured state path':
        # Native validates every member before creating the target directory,
        # so only Python's empty target directory may differ (fail closed).
        assert target not in native_tree and not any(k.startswith(target + '/') for k in python_tree), (name, python_tree)
        python_tree.pop(target, None); python_tree.pop('.', None); native_tree.pop('.', None)  # root nlink/size follow that directory
    assert python_tree == native_tree, (name, error)
    return error


def hold_python_lock(directory):
    code = 'import sys; from lodge.store import Store\nwith Store(sys.argv[1]).lock():\n    print("ready", flush=True); sys.stdin.readline()\nprint("released")'
    return subprocess.Popen([sys.executable, '-c', code, str(directory)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=str(root))


def check_lock_content(content, pid):
    info = json.loads(content)
    assert list(info) == ['pid', 'host', 'created_at'] and info['pid'] == pid and info['host'] == socket.gethostname(), info
    assert json.dumps(info).encode() == content, content
    assert datetime.fromisoformat(info['created_at']).tzinfo is not None
    assert datetime.fromisoformat(info['created_at']).isoformat() == info['created_at']


def default_mode(parent):
    global count
    # Foundation: json.dumps default separators with and without sort_keys.
    values = [base(1), base(2, 2), {}, [], {'z': [True, 1, 1.0, -0.0, 0.0, 10**100, -10**40, 1e16, 1e-5, 123456789.125, float('nan'), float('inf'), -float('inf')], 'a': {'ζ': 'S\ud800\U0001f985', 'b': '\n\t"\\/\x00\x7f\x1f'}},
              {'b': {'y': 1, 'x': [{'d': None, 'c': ''}]}, 'a': 3, 'B': 2, 'é': 4, 'é'.encode().decode('latin-1'): 5}]
    for value in values:
        for sort_keys in (0, 1):
            p = run([api, 'dumps', str(sort_keys)], framed(value))
            assert p.returncode == 0 and p.stdout == json.dumps(value, sort_keys=bool(sort_keys)).encode() + b'\n', (value, sort_keys, p)
            count += 1
    # Timestamps and identities.
    for seconds, micros in [(0, 0), (0, 1), (1, 999999), (951782400, 0), (951868799, 123456), (1700000000, 5), (4102444800, 100000), (253402300799, 0)]:
        p = run([api, 'isoformat', seconds, micros])
        expected = datetime.fromtimestamp(seconds, timezone.utc).replace(microsecond=micros).isoformat()
        assert p.returncode == 0 and p.stdout == expected.encode() + b'\n', (seconds, micros, p.stdout, expected)
        count += 1
    before = datetime.now(timezone.utc)
    stamp = run([api, 'now']).stdout.decode().strip()
    after = datetime.now(timezone.utc)
    parsed = datetime.fromisoformat(stamp)
    assert parsed.isoformat() == stamp and stamp.endswith('+00:00') and before <= parsed <= after, (stamp, before, after)
    ids = run([api, 'new-id', 500]).stdout.decode().split()
    assert len(ids) == 500 == len(set(ids)) and all(valid_id(v) and uuid.UUID(v).version == 4 and uuid.UUID(v).variant == uuid.RFC_4122 for v in ids)
    candidates = [ids[0], ids[0].upper(), '{' + ids[0] + '}', 'urn:uuid:' + ids[0], ids[0].replace('-', ''), ids[0][:-1], ids[0] + '0', ids[0].replace('-', '_', 1), '', 'x' * 36,
                  ids[0][:8] + '‐' + ids[0][9:], ids[0][:35] + 'g', ' ' + ids[0][1:], 'S\ud800\U0001f985' + ids[0][2:], 3, None, ['a']]
    p = run([api, 'valid-id'], framed(candidates))
    assert p.stdout.decode().split() == [str(int(valid_id(c))) for c in candidates], (p.stdout, candidates)
    count += 1
    # atomic_write: bytes, missing/present targets, failures and residue.
    target = parent / 'atomic'; target.mkdir()
    contents = [b'', b'\x00\xff\n\r\x1a' * 3, os.urandom(5 * 1024 * 1024)]
    for content in contents:
        p = run([api, 'atomic-write', target / 'file.bin'], framed(content))
        assert p.returncode == 0 and (target / 'file.bin').read_bytes() == content and not residue(target), p.stderr
        count += 1
    original = (target / 'file.bin').read_bytes()
    phases = ['temp_create', 'temp_write', 'temp_fsync', 'replace'] + (['directory_fsync'] if os.name == 'posix' else [])
    for phase in phases:
        p = run([api, 'atomic-write', target / 'file.bin', f'fail={phase}'], framed(b'replacement'))
        assert p.returncode == 4 and not residue(target), (phase, p)
        assert (target / 'file.bin').read_bytes() == (b'replacement' if phase == 'directory_fsync' else original), phase
        (target / 'file.bin').write_bytes(original)
        p = run([api, 'atomic-write', target / 'absent.bin', f'fail={phase}'], framed(b'x'))
        assert p.returncode == 4 and not residue(target) and (target / 'absent.bin').exists() == (phase == 'directory_fsync'), phase
        (target / 'absent.bin').unlink(missing_ok=True)
        count += 1
    # Pre-existing foreign pending files are never touched, only our own removed.
    foreign = target / '.pending-foreign'; foreign.write_bytes(b'foreign')
    p = run([api, 'atomic-write', target / 'file.bin', 'fail=replace'], framed(b'y'))
    assert p.returncode == 4 and residue(target) == ['.pending-foreign'] and foreign.read_bytes() == b'foreign'
    # Interrupted (killed) before replacement: old bytes intact, temporary left like Python.
    p = subprocess.Popen([api, 'atomic-write', str(target / 'file.bin'), 'hold=replace'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    p.stdin.write(framed(b'never published')); p.stdin.flush()
    assert p.stdout.readline() == b'ready\n'
    pending = [n for n in residue(target) if n != '.pending-foreign']
    assert len(pending) == 1 and len(pending[0]) == len('.pending-') + 8 and set(pending[0][9:]) <= set('abcdefghijklmnopqrstuvwxyz0123456789_'), pending
    assert (target / pending[0]).read_bytes() == b'never published' and (target / 'file.bin').read_bytes() == original
    p.kill(); p.wait(timeout=30)
    assert (target / 'file.bin').read_bytes() == original and (target / pending[0]).exists()
    (target / pending[0]).unlink(); foreign.unlink()
    # Resumed hold completes normally.
    p = subprocess.Popen([api, 'atomic-write', str(target / 'file.bin'), 'hold=replace'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    p.stdin.write(framed(b'published')); p.stdin.flush()
    assert p.stdout.readline() == b'ready\n'
    out, err = p.communicate(b'go\n', timeout=60)
    assert p.returncode == 0 and out == b'ok\n' and (target / 'file.bin').read_bytes() == b'published' and not residue(target), err
    count += 1
    # OS failures: missing parent and a directory target; Python raises OSError.
    for path in [parent / 'missing-parent' / 'x', target]:
        _, error = python_outcome(lambda: atomic_write(path, b'z'))
        assert isinstance(error, OSError)
        p = run([api, 'atomic-write', path], framed(b'z'))
        assert p.returncode == 5 and not residue(path.parent), (path, p)
        count += 1
    assert not residue(target) and (target / 'file.bin').read_bytes() == b'published'
    # Writer lock: content, refusal, foreign locks and failure paths.
    store = parent / 'lock' / 'nested'
    p = subprocess.Popen([api, 'lock', str(store), 'hold'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert p.stdout.readline() == b'ready\n'
    lock = store / 'lodge.lock'
    content = lock.read_bytes()
    check_lock_content(content, p.pid)
    if os.name == 'posix': assert stat.S_IMODE(lock.stat().st_mode) == 0o600
    _, error = python_outcome(lambda: python_transaction(store, []))
    assert isinstance(error, FrontendError) and str(error) == LOCK_MESSAGE.format(lock), error
    assert lock.read_bytes() == content and not (store / 'lodge.json').exists()
    out, err = p.communicate(b'go\n', timeout=60)
    assert p.returncode == 0 and out == b'released\n' and not lock.exists() and store.is_dir(), err
    count += 1
    # Python holds: native refuses with the exact message and leaves the lock alone.
    holder = hold_python_lock(store)
    assert holder.stdout.readline() == b'ready\n'
    content = lock.read_bytes()
    for args in (['lock', store], ['transaction', store]):
        p = run([api, *args], framed([]))
        assert p.returncode == 2 and p.stderr == f'error: {LOCK_MESSAGE.format(lock)}\n'.encode(), p.stderr
        count += 1
    assert lock.read_bytes() == content
    out, err = holder.communicate(b'go\n', timeout=60)
    assert holder.returncode == 0 and not lock.exists(), err
    # Stale, garbage, empty and directory locks are refused identically and untouched.
    for foreign in [b'not a lock JSON', b'', json.dumps({'pid': 1, 'host': 'gone', 'created_at': '2000-01-01T00:00:00+00:00'}).encode(), 'directory']:
        if foreign == 'directory': lock.mkdir()
        else: lock.write_bytes(foreign)
        before = snapshot(store)
        _, error = python_outcome(lambda: python_transaction(store, []))
        assert error is not None
        p = run([api, 'transaction', store], framed([]))
        expect(p, error)
        assert snapshot(store) == before, foreign
        if foreign == 'directory': lock.rmdir()
        else: lock.unlink()
    # Lock content failures unlink our own lock; the store directory remains.
    for phase in ('lock_write', 'lock_fsync'):
        p = run([api, 'lock', store, f'fail={phase}'])
        assert p.returncode == 4 and not lock.exists() and store.is_dir(), (phase, p)
        count += 1
    # A store path that is a regular file cannot host a lock: OSError in both.
    filestore = parent / 'filestore'; filestore.write_bytes(b'x')
    _, error = python_outcome(lambda: python_transaction(filestore, []))
    assert isinstance(error, OSError)
    p = run([api, 'transaction', filestore], framed([]))
    assert p.returncode in (2, 5), p
    count += 1
    # Cross-implementation contention: exactly one of eight simultaneous writers wins.
    arena = parent / 'arena'
    for round_index in range(3):
        native = [subprocess.Popen([api, 'lock', str(arena), 'hold'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(4)]
        python = [hold_python_lock(arena) for _ in range(4)]
        winners, losers = [], []
        for p in native + python:
            first = p.stdout.readline()
            if first == b'ready\n': winners.append(p)
            else:
                out, err = p.communicate(timeout=60)
                assert first == b'' and p.returncode != 0 and LOCK_MESSAGE.format(arena / 'lodge.lock').encode() in err, (first, err)
                losers.append(p)
        assert len(winners) == 1 and len(losers) == 7, (round_index, len(winners))
        lock = arena / 'lodge.lock'
        check_lock_content(lock.read_bytes(), winners[0].pid)
        out, err = winners[0].communicate(b'go\n', timeout=60)
        assert winners[0].returncode == 0 and out == b'released\n' and not lock.exists(), err
        count += 1
    # Manifest transactions: byte-exact differential over authored edits.
    NaN, Inf = float('nan'), float('inf')
    data = base(); data['unknown'] = {'z': [True, 1, 1.0, -0.0, 10**100], 'a': 'S\ud800\U0001f985', 'n': {'y': 1, 'x': 2}}
    cases = [
        ('create-empty', None, []),
        ('unchanged', data, []),
        ('rename', data, [['set', ['hunters', H, 'name'], 'Renamed é\U0001f996']]),
        ('escapes', data, [['set', ['unknown', 'a'], '\n\t"\\/\x00\x7f\x1f𐏿']]),
        ('int-to-float', data, [['set', ['unknown', 'z', 1], 1.0]]),
        ('float-to-int', data, [['set', ['unknown', 'z', 2], 1]]),
        ('true-to-int', data, [['set', ['unknown', 'z', 0], 1]]),
        ('int-to-true', data, [['set', ['unknown', 'z', 1], True]]),
        ('same-int', data, [['set', ['unknown', 'z', 1], 1]]),
        ('same-float', data, [['set', ['unknown', 'z', 2], 1.0]]),
        ('negative-zero-to-zero', data, [['set', ['unknown', 'z', 3], 0.0]]),
        ('negative-zero-to-int', data, [['set', ['unknown', 'z', 3], 0]]),
        ('big-int-increment', data, [['set', ['unknown', 'z', 4], 10**100 + 1]]),
        ('big-int-same', data, [['set', ['unknown', 'z', 4], 10**100]]),
        ('float-spellings', data, [['set', ['unknown', 'f'], [1e16, 1e15, 1e-4, 1e-5, 0.1, 123456789.123456789, 5e-324, 1.7976931348623157e308]]]),
        ('nested-append-order', data, [['set', ['unknown', 'n', 'w'], {'q': [1, {'b': 2, 'a': 3}]}], ['del', ['unknown', 'n', 'y']], ['set', ['unknown', 'n', 'y'], 1]]),
        ('reorder-only', data, [['del', ['unknown', 'n', 'y']], ['set', ['unknown', 'n', 'y'], 1]]),
        ('delete-unknown', data, [['del', ['unknown']]]),
        ('host-settings-nested', data, [['set', ['host_settings', 'render'], {'width': 1920, 'label': 'é'}]]),
        ('active-hunter-none', data, [['set', ['active_hunter'], None]]),
        ('invalid-active', data, [['set', ['active_hunter'], 'missing']]),
        ('invalid-version', data, [['set', ['schema_version'], 3]]),
        ('invalid-version-kind', data, [['set', ['schema_version'], 1.0]]),
        ('invalid-name', data, [['set', ['hunters', H, 'name'], ' ']]),
        ('invalid-table', data, [['set', ['instances'], []]]),
        ('callback-throws', data, [['set', ['hunters', H, 'name'], 'never'], ['throw']]),
        ('replace-empty', data, [['replace', empty_manifest()]]),
        ('replace-v2', data, [['replace', base(2, 2)]]),
        ('v2-unchanged', base(2, 2), []),
        ('v2-edit', base(2, 2), [['set', ['associations', list(base(2, 2)['associations'])[0], 'note'], 'x']]),
        ('nan-unchanged', {**data, 'nan': NaN}, []),
        ('nan-same', {**data, 'nan': NaN}, [['set', ['nan'], NaN]]),
        ('nan-changed', {**data, 'nan': NaN}, [['set', ['unknown', 'a'], 'x']]),
        ('nan-introduced', data, [['set', ['unknown', 'a'], NaN]]),
        ('inf-unchanged', {**data, 'inf': [Inf, -Inf]}, []),
        ('inf-changed', {**data, 'inf': [Inf, -Inf]}, [['set', ['inf', 0], 1]]),
        ('nan-to-int', {**data, 'nan': NaN}, [['set', ['nan'], 1]]),
        ('corrupt-current', b'{', []),
        ('duplicate-key', b'{"schema_version":1,"schema_version":1}', []),
        ('missing-with-bak', None, []),
        ('missing-with-upgrade-backup', None, []),
        ('existing-bak-replaced', data, [['set', ['unknown', 'a'], 'x']]),
        ('big-unknown', {**data, 'big': 'x' * (3 * 1024 * 1024)}, [['set', ['unknown', 'a'], 'x']]),
    ]
    extras = {
        'missing-with-bak': lambda d: (d.mkdir(parents=True), (d / 'lodge.json.bak').write_bytes(b'{}')),
        'missing-with-upgrade-backup': lambda d: (d.mkdir(parents=True), (d / 'lodge.schema-1.backup.json').write_bytes(b'{}')),
        'existing-bak-replaced': lambda d: ((d / 'lodge.json.bak').write_bytes(b'old backup'), (d / '.pending-stale').write_bytes(b'stale'), (d / 'lodge.recovery-x.json').write_bytes(b'r')),
    }
    outcomes = {}
    for name, initial, ops in cases:
        outcomes[name] = transaction_case(parent, name, initial, ops, extras.get(name))
    assert outcomes['invalid-active'] is not None and outcomes['callback-throws'] is not None
    assert outcomes['nan-unchanged'] is None and outcomes['nan-same'] is None and outcomes['inf-unchanged'] is None
    assert type(outcomes['nan-changed']) is ValueError and type(outcomes['nan-introduced']) is ValueError
    assert (parent / 'nan-changed-native' / 'store' / 'lodge.json.bak').exists()  # reference order: backup, then encode
    # Failure injection inside the transaction: original bytes intact, lock released.
    store = parent / 'inject' / 'store'; write(store, data)
    _ = python_transaction(store, [['set', ['unknown', 'a'], 'canonical']])
    (store / 'lodge.json.bak').unlink()
    original = (store / 'lodge.json').read_bytes(); before = snapshot(store)
    edit = [['set', ['unknown', 'a'], 'edited']]
    for phase in phases:
        p = run([api, 'transaction', store, f'fail={phase}'], framed(edit))
        assert p.returncode == 4 and not (store / 'lodge.lock').exists() and not residue(store), (phase, p)
        if phase == 'directory_fsync':
            assert (store / 'lodge.json.bak').read_bytes() == original and (store / 'lodge.json').read_bytes() == original
        else:
            assert snapshot(store) == before, phase
        (store / 'lodge.json.bak').unlink(missing_ok=True)
        p = run([api, 'transaction', store, f'fail-second={phase}'], framed(edit))
        assert p.returncode == 4 and not (store / 'lodge.lock').exists() and not residue(store), (phase, p)
        assert (store / 'lodge.json.bak').read_bytes() == original, phase
        if phase == 'directory_fsync':
            # Replacement already happened; only the parent fsync error is reported, as in Python.
            assert json.loads((store / 'lodge.json').read_bytes())['unknown']['a'] == 'edited'
            (store / 'lodge.json').write_bytes(original)
        else:
            assert (store / 'lodge.json').read_bytes() == original, phase
        (store / 'lodge.json.bak').unlink()
        count += 2
    assert snapshot(store) == before
    # Interop sequence: Python and native alternate on one store; each backup is the predecessor.
    store = parent / 'interop' / 'store'
    history = []
    for step, (who, ops) in enumerate([('python', []), ('native', [['set', ['host_settings', 'a'], 1]]), ('python', [['set', ['host_settings', 'b'], [1.5, 'x']]]), ('native', [['del', ['host_settings', 'a']]]), ('native', []), ('python', [['set', ['host_settings', 'c'], {'z': 1, 'a': 2}]])]):
        if who == 'python': python_transaction(store, ops)
        else:
            p = run([api, 'transaction', store], framed(ops)); assert p.returncode == 0, p.stderr
        current = (store / 'lodge.json').read_bytes()
        expected = copy.deepcopy(history[-1][1]) if history else empty_manifest()
        apply(expected, ops)
        assert Store(store).read() == expected and json.loads(current) == expected
        assert current == json.dumps(expected, indent=2, ensure_ascii=True, allow_nan=False).encode() + b'\n'
        if ops or step == 0:
            if history: assert (store / 'lodge.json.bak').read_bytes() == history[-1][0]
            history.append((current, expected))
        else: assert current == history[-1][0]
        count += 1
    # session_root: identity validation and safe path checks.
    store = parent / 'sessions-store'; write(store, base())
    for identity in [ids[0], ids[0].upper(), 'x', '', '../x', H]:
        expected, error = python_outcome(lambda: session_root(Store(store), identity))
        p = run([api, 'session-root', store], framed(identity))
        expect(p, error)
        if error is None: assert p.stdout == str(expected).encode() + b'\n', (identity, p.stdout, expected)
    hard = store / 'sessions'; hard.mkdir(); (hard / ids[1]).write_bytes(b'x'); os.link(hard / ids[1], store / 'other-link')
    _, error = python_outcome(lambda: session_root(Store(store), ids[1]))
    assert isinstance(error, FrontendError)
    expect(run([api, 'session-root', store], framed(ids[1])), error)
    # write_blobs: path rejection, normalization, nesting, overwrite and round trip.
    blob_sets = {
        'simple': [('trophy00.sav', b'save'), ('trophy00.sab', b'')],
        'nested': [('a/b/c.bin', b'1'), ('a/d.bin', b'2'), ('e.bin', b'3'), ('a/b/f/g.bin', b'4')],
        'unicode': [('獵人-🦖-é.sav', b'u'), ('diré/\U0001f985.bin', b'v')],
        'normalized': [('./x/.//y.bin', b'n'), ('z/', b'trailing')],
        'parent': [('../escape.bin', b'x')],
        'parent-inner': [('a/../b.bin', b'x')],
        'absolute': [(str(parent / 'escape.bin'), b'x')],
        'unc-absolute': [('//server/share/escape.bin', b'x')],
        'backslash': [('a\\b.bin', b'x')],
        'colon': [('a:b.bin', b'x')],
        'large': [('big.bin', os.urandom(2 * 1024 * 1024))],
        'none': [],
    }
    for name, blobs in blob_sets.items(): blob_case(parent, 'blobs-' + name, blobs)
    # Reviewed safety correction beyond parity: native validates every member
    # lexically and checks strict containment before any mkdir or write, so a
    # rooted drive-less name (which PureWindowsPath joins below the drive root),
    # an empty/dot name (the directory itself) or a late bad member creates
    # nothing. Python is deliberately not run on these: only the native driver.
    containment = {
        'rooted': [('/escaped.sav', b'x')], 'rooted-nested': [('/a/escaped.sav', b'x')],
        'rooted-dot': [('/./escaped.sav', b'x')], 'unc': [('//server/share/x', b'x')],
        'drive-colon': [('c:escaped.sav', b'x')], 'drive-rooted': [('c:/escaped.sav', b'x')],
        'empty': [('', b'x')], 'dot': [('.', b'x')], 'dot-slash': [('./', b'x')],
        'parent-only': [('..', b'x')], 'parent-deep': [('a/../../b', b'x')], 'parent-trailing': [('a/..', b'x')],
        'nul': [('a\x00b', b'x')], 'backslash-root': [('\\x', b'x')],
        'late-bad': [('ok.bin', b'first'), ('a/ok2.bin', b'second'), ('/escaped.sav', b'third')],
        'late-parent': [('ok.bin', b'first'), ('../escape.bin', b'second')],
    }
    sandbox = parent / 'containment'; sandbox.mkdir()
    outside = [Path(sandbox.anchor) / 'escaped.sav', sandbox.parent / 'escape.bin', sandbox.parent / 'escaped.sav']
    assert not any(q.exists() for q in outside)
    for name, blobs in containment.items():
        out = sandbox / name / 'out'
        before = snapshot(parent)
        header = json.dumps([[k, len(v)] for k, v in blobs], ensure_ascii=True).encode()
        p = run([api, 'write-blobs', out], framed(header) + b''.join(v for _, v in blobs))
        assert p.returncode == 2 and p.stderr == b'error: unsafe captured state path\n', (name, p)
        assert not out.exists() and not (sandbox / name).exists() and snapshot(parent) == before, name
        assert not any(q.exists() for q in outside), name
        count += 1
    # Legitimate capture-produced names are unaffected by containment: exact parity remains.
    blob_case(parent, 'blobs-contained', [('trophy00.sav', b'a'), ('work/state/trophy00.sab', b'b'), ('獵人/x.bin', b'c')])
    def existing(directory):
        (directory / 'out').mkdir(); (directory / 'out' / 'a').mkdir()
        (directory / 'out' / 'a' / 'd.bin').write_bytes(b'old'); (directory / 'out' / 'keep.bin').write_bytes(b'keep')
    blob_case(parent, 'blobs-overwrite', blob_sets['nested'], existing)
    def hardlinked(directory):
        (directory / 'out').mkdir(); (directory / 'out' / 'e.bin').write_bytes(b'old'); os.link(directory / 'out' / 'e.bin', directory / 'twin')
    assert isinstance(blob_case(parent, 'blobs-hardlink', blob_sets['nested'], hardlinked), FrontendError)
    def file_ancestor(directory): (directory / 'out').write_bytes(b'file')
    blob_case(parent, 'blobs-file-ancestor', blob_sets['simple'], file_ancestor)
    def directory_target(directory): (directory / 'out' / 'trophy00.sav').mkdir(parents=True)
    blob_case(parent, 'blobs-directory-target', blob_sets['simple'], directory_target)
    # Failure injection: earlier blobs persist, the failing target is untouched, no residue.
    for phase in phases + (['blob_directory_fsync'] if os.name == 'posix' else []):
        out = parent / f'blobs-fail-{phase}'
        header = json.dumps([[k, len(v)] for k, v in blob_sets['nested']]).encode()
        p = run([api, 'write-blobs', out, f'fail={phase}'], framed(header) + b''.join(v for _, v in blob_sets['nested']))
        assert p.returncode == 4 and not residue(out / 'a' / 'b') and not residue(out), (phase, p)
        written = sorted(str(q.relative_to(out).as_posix()) for q in out.rglob('*') if q.is_file())
        assert written == ([] if phase != 'directory_fsync' and phase != 'blob_directory_fsync' else ['a/b/c.bin'] if phase == 'directory_fsync' else ['a/b/c.bin', 'a/b/f/g.bin', 'a/d.bin', 'e.bin']), (phase, written)
        count += 1
    # Round trip: Python capture -> native write_blobs -> Python and native capture agree.
    source = parent / 'roundtrip-source'; source.mkdir()
    for relative, content in blob_sets['nested'] + blob_sets['unicode']:
        (source / relative).parent.mkdir(parents=True, exist_ok=True); (source / relative).write_bytes(content)
    (source / 'empty-dir').mkdir()
    entries, blobs = capture(source)
    header = json.dumps([[k, len(v)] for k, v in blobs.items()], ensure_ascii=True).encode()
    out = parent / 'roundtrip-out'
    p = run([api, 'write-blobs', out], framed(header) + b''.join(blobs.values()))
    assert p.returncode == 0, p.stderr
    again_entries, again_blobs = capture(out)
    assert again_blobs == blobs and [e for e in again_entries if e['type'] == 'file'] == [e for e in entries if e['type'] == 'file']
    expected = (json.dumps(again_entries, indent=2, ensure_ascii=True, allow_nan=False) + '\n').encode() + b''.join(str(len(b)).encode() + b'\n' + b for b in again_blobs.values())
    p = run([api, 'capture', out])
    assert p.returncode == 0 and p.stdout == expected, p.stderr
    count += 2
    # Default-directory code paths never touch the real home: environment isolated.
    env = {**os.environ, 'LOCALAPPDATA': str(parent / 'isolated-local'), ('USERPROFILE' if os.name == 'nt' else 'HOME'): str(parent / 'isolated-home')}
    p = run([api, 'transaction', '~/isolated-store'], framed([]), env=env)
    assert p.returncode == 0 and (parent / 'isolated-home' / 'isolated-store' / 'lodge.json').exists(), p.stderr
    count += 1


def file_link_mode(parent):
    global count
    probe = parent / 'probe-link'
    try:
        probe.symlink_to(parent / 'probe-target', target_is_directory=True)
    except OSError as error:
        print('Host symlink creation unavailable:', error)
        return 77
    data = base()
    # Store directory alias resolves at construction; a linked manifest is refused.
    real = parent / 'real'; write(real, data)
    linked = parent / 'linked'; linked.symlink_to(real, target_is_directory=True)
    transaction_case(parent, 'linked-root', None, [['set', ['host_settings', 'x'], 1]], lambda d: (d.parent.mkdir(parents=True, exist_ok=True), d.rmdir() if d.exists() else None, write(d.parent / 'target', copy.deepcopy(data)), d.symlink_to(d.parent / 'target', target_is_directory=True)))
    def linked_manifest(d):
        d.mkdir(parents=True); write(d.parent / 'elsewhere', copy.deepcopy(data)); (d / 'lodge.json').symlink_to(d.parent / 'elsewhere' / 'lodge.json')
    assert isinstance(transaction_case(parent, 'linked-manifest', None, [], linked_manifest), FrontendError)
    def dangling_lock(d): d.mkdir(parents=True); (d / 'lodge.lock').symlink_to(d / 'absent')
    error = transaction_case(parent, 'dangling-lock', None, [], dangling_lock)
    assert error is not None
    def linked_lock(d): d.mkdir(parents=True); (d / 'target.lock').write_bytes(b'x'); (d / 'lodge.lock').symlink_to(d / 'target.lock')
    assert transaction_case(parent, 'linked-lock', None, [], linked_lock) is not None
    def linked_bak(d): write(d, copy.deepcopy(data)); (d / 'lodge.json.bak').symlink_to(d / 'elsewhere.bak')
    transaction_case(parent, 'linked-bak', None, [['set', ['host_settings', 'x'], 1]], linked_bak)
    # write_blobs aliases: linked target, linked intermediate directory, linked ancestor.
    def linked_target(d): (d / 'out').mkdir(); (d / 'elsewhere').write_bytes(b'e'); (d / 'out' / 'e.bin').symlink_to(d / 'elsewhere')
    assert isinstance(blob_case(parent, 'link-target', [('a/b/c.bin', b'1'), ('e.bin', b'3')], linked_target), FrontendError)
    def linked_dir(d): (d / 'out').mkdir(); (d / 'elsewhere').mkdir(); (d / 'out' / 'a').symlink_to(d / 'elsewhere', target_is_directory=True)
    assert isinstance(blob_case(parent, 'link-dir', [('a/b/c.bin', b'1')], linked_dir), FrontendError)
    def linked_root(d): (d / 'elsewhere').mkdir(); (d / 'out').symlink_to(d / 'elsewhere', target_is_directory=True)
    assert isinstance(blob_case(parent, 'link-root', [('c.bin', b'1')], linked_root), FrontendError)
    def dangling_target(d): (d / 'out').mkdir(); (d / 'out' / 'e.bin').symlink_to(d / 'absent')
    assert isinstance(blob_case(parent, 'link-dangling', [('e.bin', b'3')], dangling_target), FrontendError)
    # session_root under a linked sessions directory.
    store = parent / 'session-store'; write(store, data); (store / 'elsewhere').mkdir(); (store / 'sessions').symlink_to(store / 'elsewhere', target_is_directory=True)
    identity = str(uuid.uuid4())
    _, error = python_outcome(lambda: session_root(Store(store), identity))
    assert isinstance(error, FrontendError)
    expect(run([api, 'session-root', store], framed(identity)), error)
    return 0


def posix_mode(parent):
    global count
    if os.name != 'posix':
        print('POSIX durability checks are not applicable on', os.name)
        return 77
    data = base()
    # Directory fsync coverage: the synced set equals the reference's formula, deepest first.
    out = parent / 'sync' / 'out'
    blobs = [('a/b/c.bin', b'1'), ('a/d.bin', b'2'), ('e.bin', b'3'), ('a/b/f/g.bin', b'4'), ('a/b/c.bin', b'5')]
    header = json.dumps([[k, len(v)] for k, v in blobs]).encode()
    p = run([api, 'write-blobs', out, 'trace'], framed(header) + b''.join(v for _, v in blobs))
    assert p.returncode == 0, p.stderr
    synced = [Path(l.split(' ', 2)[2]) for l in p.stdout.decode().splitlines() if l.startswith('phase blob_directory_fsync ')]
    expected = {out, out.parent}
    for relative, _ in blobs:
        path = out / relative
        expected.update(q for q in path.parents if q.is_relative_to(out))
    assert set(synced) == expected and len(synced) == len(expected), (synced, expected)
    assert [len(q.parts) for q in synced] == sorted((len(q.parts) for q in synced), reverse=True), synced
    file_syncs = [l for l in p.stdout.decode().splitlines() if l.startswith('phase directory_fsync ')]
    assert len(file_syncs) == len(blobs)
    assert (out / 'a/b/c.bin').read_bytes() == b'5'
    count += 1
    # Every atomic replacement fsyncs the temporary, then the parent after rename.
    p = run([api, 'atomic-write', parent / 'order.bin', 'trace'], framed(b'x'))
    names = [l.split(' ')[1] for l in p.stdout.decode().splitlines() if l.startswith('phase ')]
    assert names == ['temp_create', 'temp_write', 'temp_fsync', 'replace', 'directory_fsync'], names
    # Temporary and lock modes are 0600 regardless of umask; replaced files keep 0600 like Python.
    old = os.umask(0)
    try:
        target = parent / 'mode.bin'
        p = subprocess.Popen([api, 'atomic-write', str(target), 'hold=replace'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        p.stdin.write(framed(b'm')); p.stdin.flush(); assert p.stdout.readline() == b'ready\n'
        pending = residue(parent); assert len(pending) == 1 and stat.S_IMODE((parent / pending[0]).stat().st_mode) == 0o600
        p.communicate(b'go\n', timeout=60); assert p.returncode == 0 and stat.S_IMODE(target.stat().st_mode) == 0o600
        reference = parent / 'reference.bin'; atomic_write(reference, b'm')
        assert stat.S_IMODE(reference.stat().st_mode) == stat.S_IMODE(target.stat().st_mode)
        store = parent / 'modes'
        p = subprocess.Popen([api, 'lock', str(store), 'hold'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert p.stdout.readline() == b'ready\n' and stat.S_IMODE((store / 'lodge.lock').stat().st_mode) == 0o600
        p.communicate(b'go\n', timeout=60); assert p.returncode == 0
    finally: os.umask(old)
    count += 1
    # Special files: FIFO manifest, FIFO lock, FIFO blob target and FIFO session root.
    def fifo_manifest(d): d.mkdir(parents=True); os.mkfifo(d / 'lodge.json')
    assert isinstance(transaction_case(parent, 'fifo-manifest', None, [], fifo_manifest), FrontendError)
    def fifo_lock(d): d.mkdir(parents=True); os.mkfifo(d / 'lodge.lock')
    assert isinstance(transaction_case(parent, 'fifo-lock', None, [], fifo_lock), FrontendError)
    def fifo_bak(d): write(d, copy.deepcopy(data)); os.mkfifo(d / 'lodge.json.bak')
    transaction_case(parent, 'fifo-bak', None, [['set', ['host_settings', 'x'], 1]], fifo_bak)
    def fifo_target(d): (d / 'out').mkdir(); os.mkfifo(d / 'out' / 'e.bin')
    assert isinstance(blob_case(parent, 'fifo-target', [('e.bin', b'3')], fifo_target), FrontendError)
    store = parent / 'fifo-session'; write(store, data); (store / 'sessions').mkdir(); identity = str(uuid.uuid4()); os.mkfifo(store / 'sessions' / identity)
    _, error = python_outcome(lambda: session_root(Store(store), identity))
    expect(run([api, 'session-root', store], framed(identity)), error)
    assert isinstance(error, FrontendError)
    # Raw (undecodable) bytes in captured names survive the surrogateescape round trip.
    raw = os.fsdecode(b'raw-\xff\xfe.bin')
    blob_case(parent, 'blobs-raw-name', [(raw, b'raw'), ('sub-' + raw + '/x.bin', b'nested')])
    source = parent / 'raw-source'; source.mkdir(); (source / raw).write_bytes(b'r'); (source / ('d-' + raw)).mkdir(); (source / ('d-' + raw) / 'y').write_bytes(b'y')
    entries, blobs = capture(source)
    header = json.dumps([[k, len(v)] for k, v in blobs.items()], ensure_ascii=True).encode()
    out = parent / 'raw-out'
    p = run([api, 'write-blobs', out], framed(header) + b''.join(blobs.values()))
    assert p.returncode == 0 and capture(out) == (entries, blobs), p.stderr
    count += 2
    # Unwritable store directory: lock creation fails as OSError in both, nothing created.
    if os.geteuid() != 0:
        locked = parent / 'unwritable'; locked.mkdir(); locked.chmod(0o500)
        try:
            _, error = python_outcome(lambda: python_transaction(locked, []))
            assert isinstance(error, OSError) and not isinstance(error, FrontendError)
            p = run([api, 'transaction', locked], framed([]))
            assert p.returncode == 5 and not list(locked.iterdir()), p
            count += 1
        finally: locked.chmod(0o700)
    else: print('Running as root: permission-denied lock creation not exercised')
    return 0


with tempfile.TemporaryDirectory(prefix='c2-native-store-write-') as temporary:
    parent = Path(temporary).resolve()
    code = {'default': default_mode, 'file-link': file_link_mode, 'posix': posix_mode}[mode](parent) or 0
    if code == 77:
        print(f'{mode}: capability unavailable on this host; skipped')
        sys.exit(77)
print(f'{count} authoritative write-primitive comparisons and safety checks passed ({mode}) on {os.name}')
