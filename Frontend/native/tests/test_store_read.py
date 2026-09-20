"""Disposable filesystem/API/CLI differential tests against unchanged Python."""
import copy
import json
import os
from pathlib import Path, PureWindowsPath
import stat
import subprocess
import sys
import tempfile
from unittest.mock import patch

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root))
sys.path.insert(0, str(root / 'tools'))
from lodge.store import Store, empty_manifest
import frontend
from generate_schema_fixtures import base, H, I
cli, api = sys.argv[1:3]
commands = [('status',), ('hunter', 'list'), ('expedition', 'list'), ('host-settings',)]
count = 0

def snapshot(root):
    # Never follow symlinks or read a special file. Include exact regular bytes,
    # names, link targets and object types; reading itself may update atime.
    out = {}
    def visit(p):
        s = p.lstat()
        value = (s.st_mode, s.st_nlink, s.st_size, s.st_mtime_ns)
        if stat.S_ISLNK(s.st_mode): value += (os.readlink(p),)
        elif stat.S_ISREG(s.st_mode): value += (p.read_bytes(),)
        out[str(p.relative_to(root))] = value
        if stat.S_ISDIR(s.st_mode) and not getattr(s, 'st_file_attributes', 0) & 1024:
            for child in p.iterdir(): visit(child)
    visit(root)
    return out

def run(args, **kw):
    return subprocess.run(list(map(str, args)), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15, **kw)

def check(parent, directory, commands=commands, env=None, prefix=None):
    global count
    before = snapshot(parent)
    opts = ['--store', str(directory)] if prefix is None else prefix
    for command in commands:
        with patch.dict(os.environ, env or {}, clear=False):
            try:
                args = frontend.parser().parse_args([*opts, *command])
                expected = json.dumps(frontend.execute(args), indent=2, ensure_ascii=True, allow_nan=False).encode() + b'\n'
                error = None
            except (ValueError, OSError, KeyError, RuntimeError) as e:
                error = e
        result = run([cli, *opts, *command], env=({**os.environ, **env} if env else None))
        if error is None:
            assert result.returncode == 0 and result.stdout == expected, (directory, command, result.returncode, result.stdout, result.stderr, expected)
        else:
            assert isinstance(json.loads(result.stderr)["error"], str), result.stderr
            assert result.returncode == 2 and not result.stdout, (directory, command, type(error), str(error), result)
        count += 1
    assert snapshot(parent) == before, ('read mutated store', directory)

def write(directory, data):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'lodge.json').write_bytes(data if isinstance(data, bytes) else json.dumps(data, ensure_ascii=True).encode())

def summary(data):
    def cps(s): return ''.join(str(ord(c)) + ',' for c in s) + '\n'
    s = f"{data['schema_version']}\n{int('active_hunter' in data)}\n" + cps(data.get('active_hunter') or '')
    s += str(len(data['hunters'])) + '\n'
    for key, h in data['hunters'].items(): s += cps(key) + cps(h['name']) + str(int(bool(h.get('archived_at')))) + '\n'
    s += str(len(data['instances'])) + '\n'
    for key, e in data['instances'].items(): s += cps(key) + ''.join(cps(e[k]) for k in ('mode', 'path_flavor', 'path'))
    return s.encode()

# Alias comparison follows pathlib drive/root/parts and pinned Unicode lower,
# including equivalent UNC anchor trailing separators (no live share needed).
spellings = ['C:\\\\', 'c:/', '\\\\server\\share', '\\\\server\\share\\', '\\\\?\\C:\\', '\\\\?\\UNC\\server\\share', '\\\\?\\UNC\\server\\share\\', 'C:\\ΟΣ', 'c:\\ος', 'C:\\οσ', 'C:\\sigma-Σ', 'C:\\sigma-ς']
for a in spellings:
    for b in spellings:
        p = run([api, 'path-equal', a, b])
        assert p.returncode == 0 and p.stdout == (b'1\n' if PureWindowsPath(a) == PureWindowsPath(b) else b'0\n'), (a, b, p)

with tempfile.TemporaryDirectory(prefix='c2-native-store-') as temporary:
    parent = Path(temporary).resolve()
    check(parent, parent / 'missing' / 'nested')
    assert not (parent / 'missing').exists()
    directory = parent / 'store'
    for version, generations in [(1, 0), (2, 0), (2, 1), (2, 2)]:
        data = base(version, generations)
        data['unknown'] = {'z': [True, 1, 1.0, -0.0, 10**100], 'a': '\ud800\U0001f985'}
        write(directory, data)
        check(parent, directory)
        assert run([api, 'summary', directory]).stdout == summary(data)
        assert run([api, 'read', directory]).returncode == 0
    # Unknown fields, item order, names with preserved unpaired surrogates.
    data = base()
    data['hunters'][H]['name'] = 'S\ud800\U0001f985'
    for n in [22, 21]:
        key = f'00000000-0000-0000-0000-{n:012x}'
        data['hunters'][key] = {'id': key, 'name': str(n), 'archived_at': [1], 'forward': {'z': 3, 'a': 4}}
        item = copy.deepcopy(data['instances'][I]); item.update(id=key, path='/missing/' + str(n))
        data['instances'][key] = item
    data['host_settings'] = {'z': 1, 'a': {'q': 2, 'b': 3}}
    write(directory, data); check(parent, directory)
    assert run([api, 'summary', directory]).stdout == summary(data)
    del data['active_hunter']
    write(directory, data); check(parent, directory)
    assert run([api, 'summary', directory]).stdout == summary(data)
    data = base(); data['host_settings']['unknown'] = float('nan')
    write(directory, data); check(parent, directory)
    assert run([api, 'read', directory]).returncode == 0
    for value in [float('inf'), -float('inf')]:
        data['forward'] = value; write(directory, data); check(parent, directory)
    for raw in [b'\xff', b'{"schema_version":1,"schema_version":1}', b'{', b'[]', b'null', b'{}', b'\xef\xbb\xbf{}']:
        write(directory, raw); check(parent, directory)
    # Current wins; no read/validation of lock, backups or historical paths.
    write(directory, base(2, 2))
    (directory / 'lodge.lock').write_bytes(b'not a lock JSON')
    for backup in ['lodge.json.bak', 'lodge.schema-1.backup.json']:
        (directory / backup).write_bytes(b'not JSON')
    check(parent, directory)
    (directory / 'lodge.json.bak').write_text(json.dumps(base()))
    write(directory, b'{'); check(parent, directory)
    (directory / 'lodge.json').unlink()
    check(parent, directory)
    (directory / 'lodge.json.bak').unlink(); check(parent, directory)
    (directory / 'lodge.schema-1.backup.json').unlink()
    (directory / 'lodge.json.bak').write_bytes(b'{}'); check(parent, directory)
    (directory / 'lodge.json.bak').unlink(); check(parent, directory)
    # Native argv, roots and environment paths are not restricted to ASCII.
    unicode_dir = parent / '獵人-🦖-é'
    write(unicode_dir, base()); check(parent, unicode_dir)
    check(parent, directory, env={'LOCALAPPDATA': str(parent / 'local')}, prefix=[])
    default_dir = parent / 'local' / 'carnivores-lodge'
    write(default_dir, base()); check(parent, default_dir, env={'LOCALAPPDATA': str(parent / 'local')}, prefix=[])
    check(parent, unicode_dir, prefix=['--store=' + str(unicode_dir), '--probe', str(parent / 'never-run')])
    old = Path.cwd()
    try:
        os.chdir(parent)
        check(parent, unicode_dir, prefix=['--store', unicode_dir.name])
        check(parent, parent, prefix=['--store', ''])
        assert run([api, 'read', '']).returncode == 0
    finally: os.chdir(old)
    homekey = 'USERPROFILE' if os.name == 'nt' else 'HOME'
    check(parent, unicode_dir, env={homekey: str(parent)}, prefix=['--store', '~/' + unicode_dir.name])
    if os.name == 'posix':
        raw_dir = parent / os.fsdecode(b'native-\xff'); write(raw_dir, base()); check(parent, raw_dir)
    # Direct API policy and immutable lifetime, with no new default byte cap.
    write(directory, base())
    assert run([api, 'bytes', directory]).returncode == 3
    assert run([api, 'nul', directory]).returncode == 2
    data = empty_manifest(); data['forward'] = 'x' * (17 * 1024 * 1024)
    write(directory, data)
    assert run([api, 'read', directory]).returncode == 0
    shallow = json.dumps(empty_manifest())[:-1]
    deep = (shallow + ',"deep":' + '[' * 1200 + '0' + ']' * 1200 + '}').encode()
    write(directory, deep)
    before = snapshot(parent)
    assert run([api, 'read', directory]).returncode == 3
    assert run([cli, '--store', directory, 'status']).returncode == 3
    old_limit = sys.getrecursionlimit()
    try:
        sys.setrecursionlimit(5000)
        expected = json.dumps(Store(directory).read(), indent=2, ensure_ascii=True, allow_nan=False).encode() + b'\n'
    finally: sys.setrecursionlimit(old_limit)
    p = run([api, 'depth', directory]); assert p.returncode == 0 and p.stdout == expected, p.stderr
    assert snapshot(parent) == before
    write(directory, base())
    p = subprocess.Popen([api, 'owned', str(directory)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert p.stdout.readline() == b'ready\n'
    expected = json.dumps(Store(directory).read(), indent=2, ensure_ascii=True, allow_nan=False).encode() + b'\n'
    write(directory, b'broken')
    output, errors = p.communicate(b'go\n', timeout=15)
    assert p.returncode == 0 and output == expected, errors
    # Unsupported paths must never reach a write transaction or subprocess.
    write(directory, base()); before = snapshot(parent)
    for command in [('hunter', 'create', 'X'), ('recover-backup',), ('host-settings', '--json', '{}'), ('profiles', I), ('managed-state', 'upgrade'), ('expedition', 'discover', str(parent))]:
        p = run([cli, '--store', directory, *command]); assert p.returncode == 2
    assert snapshot(parent) == before
    for args in [['--store', '--probe', 'status'], ['--store', '--unknown', 'status'], ['--store'], ['--probe', '--store', str(directory), 'status']]:
        p = run([cli, *args]); assert p.returncode == 2 and not p.stdout
        assert 'error' in json.loads(p.stderr)
    assert snapshot(parent) == before
    # Isolated environment resolution compares paths without touching home roots.
    for local in [None, '', str(parent / 'env-local')]:
        env = dict(os.environ)
        if local is None: env.pop('LOCALAPPDATA', None)
        else: env['LOCALAPPDATA'] = local
        env[homekey] = str(parent)
        with patch.dict(os.environ, env, clear=True):
            expected = str(frontend.parser().parse_args(['status']).store).encode() + b'\n'
        p = run([api, 'default', '.'], env=env)
        assert p.returncode == 0 and p.stdout == expected, (local, p.stdout, expected)
    if os.name == 'posix':
        env = {**os.environ, 'HOME': ''}; env.pop('LOCALAPPDATA', None)
        assert run([api, 'directory', '~'], env=env).stdout == b'/\n'
        assert run([api, 'default', '.'], env=env).stdout == b'/.local/share/carnivores-lodge\n'
    # File ancestors, hardlinks, FIFOs and links are separately host-authoritative.
    fileparent = parent / 'file-parent'; fileparent.write_bytes(b'x')
    check(parent, fileparent / 'nested')
    hard = parent / 'hard'; hard.mkdir()
    os.link(directory / 'lodge.json', hard / 'lodge.json')
    check(parent, hard); check(parent, directory)
    (hard / 'lodge.json').unlink()
    if hasattr(os, 'mkfifo'):
        fifo = parent / 'fifo'; fifo.mkdir(); os.mkfifo(fifo / 'lodge.json'); check(parent, fifo)
    linked = parent / 'linked'
    try:
        linked.symlink_to(directory, target_is_directory=True)
    except OSError as error:
        print('Host symlink creation unavailable:', error)
    else:
        check(parent, linked)  # Deliberate constructor root resolution.
        dangling_root = parent / 'dangling-root'
        dangling_root.symlink_to(parent / 'missing-target', target_is_directory=True)
        check(parent, dangling_root)
        # Resolution processes .. after a link, rather than before it.
        check(parent, linked / '..' / unicode_dir.name)
        alias = parent / 'alias-current'; alias.mkdir()
        (alias / 'lodge.json').symlink_to(directory / 'lodge.json'); check(parent, alias)
        (alias / 'lodge.json').unlink(); (alias / 'lodge.json').symlink_to(parent / 'absent'); check(parent, alias)
        (alias / 'lodge.json').unlink(); (alias / 'lodge.json.bak').symlink_to(parent / 'absent'); check(parent, alias)
        # Store constructed before root replacement must reject the new alias.
        held = parent / 'held'; write(held, base()); reference = Store(held)
        p = subprocess.Popen([api, 'hold', str(held)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert p.stdout.readline() == b'ready\n'
        held.rename(parent / 'held-old'); held.symlink_to(directory, target_is_directory=True)
        try: reference.read()
        except ValueError: pass
        else: raise AssertionError('reference failed to reject replaced ancestor')
        before = snapshot(parent)
        output, errors = p.communicate(b'go\n', timeout=15)
        assert p.returncode == 2 and not output, errors
        assert snapshot(parent) == before
    if os.name == 'nt':
        from pathlib import PureWindowsPath
        for raw in ['C:\\x\\y', '\\\\server\\share\\x', '\\\\?\\C:\\x', '\\\\?\\UNC\\server\\share\\x', '\\\\?\\Volume{guid}\\x']:
            path = PureWindowsPath(raw)
            expected = [*reversed(path.parents), path]
            p = run([api, 'parents', raw])
            assert p.returncode == 0 and p.stdout.decode().splitlines() == list(map(str, expected)), (raw, p.stdout, expected)
        prefixed = Path('\\\\?\\' + str(unicode_dir))
        check(parent, prefixed)
        longpath = parent / ('long-' + 'x' * 90) / ('y' * 90) / ('z' * 90)
        longprefixed = Path('\\\\?\\' + str(longpath))
        write(longprefixed, base()); check(parent, longprefixed)
        # Python expands the current username to USERPROFILE without requiring
        # a matching basename; other-user expansion has that extra constraint.
        check(parent, unicode_dir, env={'USERNAME': 'different-basename', 'USERPROFILE': str(unicode_dir)}, prefix=['--store', '~different-basename'])
        env = {**os.environ, 'USERNAME': 'current-test-user', 'USERPROFILE': ''}
        old = Path.cwd()
        try:
            os.chdir(parent)
            for spelling in ['~', '~current-test-user']:
                with patch.dict(os.environ, env, clear=True):
                    expected = str(Store(spelling).directory).encode() + b'\n'
                p = run([api, 'directory', spelling], env=env)
                assert p.returncode == 0 and p.stdout == expected, (spelling, p.stderr, p.stdout, expected)
        finally: os.chdir(old)
        for before_name, after_name in [('mixed-case', 'MIXED-CASE'), ('sigma-Σ', 'sigma-ς'), ('positive-ΟΣ', 'positive-ος'), ('negative-ΟΣ', 'negative-οσ')]:
            before_dir = parent / before_name; write(before_dir, base())
            reference = Store(before_dir)
            p = subprocess.Popen([api, 'hold', str(before_dir)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert p.stdout.readline() == b'ready\n'
            intermediate = parent / 'renaming'; before_dir.rename(intermediate); intermediate.rename(parent / after_name)
            try:
                expected = json.dumps(reference.read(), indent=2, ensure_ascii=True, allow_nan=False).encode() + b'\n'
                failed = False
            except (ValueError, OSError): failed = True
            before = snapshot(parent)
            output, errors = p.communicate(b'go\n', timeout=15)
            assert (p.returncode == 2 and not output) if failed else (p.returncode == 0 and output == expected), (before_name, after_name, errors)
            assert snapshot(parent) == before
        junction = parent / 'junction'
        p = run(['cmd', '/c', 'mklink', '/J', str(junction), str(directory)])
        assert p.returncode == 0, p.stderr
        check(parent, junction)
        # Junction as final manifest is rejected even if is_symlink is false.
        target = parent / 'junction-current'; target.mkdir()
        p = run(['cmd', '/c', 'mklink', '/J', str(target / 'lodge.json'), str(directory)])
        assert p.returncode == 0, p.stderr
        check(parent, target)
        import ctypes
        buffer = ctypes.create_unicode_buffer(32768)
        n = ctypes.windll.kernel32.GetShortPathNameW(str(unicode_dir), buffer, len(buffer))
        if n and buffer.value != str(unicode_dir): check(parent, Path(buffer.value))
        else: print('Host has no distinct short-name alias for test root')
print(f'{count} authoritative store/CLI comparisons plus public API/resource/safety checks passed on {os.name}')
