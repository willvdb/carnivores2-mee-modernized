#!/usr/bin/env python3
"""Read-only content observations against the unchanged discovery reference."""
import hashlib
import json
import os
from pathlib import Path
import random
import queue
import subprocess
import sys
import tempfile
import threading
import time
import traceback
from unittest import mock

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
    check_writer_retry()
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
    synchronized_changes(base / 'synchronized-changes')


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


class ObservationLifecycle:
    """Test-owned reader lifetime; its clock advances only while no child lives."""
    def __init__(self):
        self.lock = threading.Lock()
        self.started = None
        self.completed_seconds = 0.0

    def begin(self):
        with self.lock:
            assert self.started is None
            self.started = time.monotonic()

    def end(self):
        with self.lock:
            assert self.started is not None
            self.completed_seconds += time.monotonic() - self.started
            self.started = None

    def state(self):
        with self.lock:
            now = time.monotonic()
            active = self.started is not None
            excluded = self.completed_seconds + (now - self.started if active else 0.0)
            return now - excluded, active


def replace_for_race(source, target, stop, progress, *, windows=None, observation=None):
    # The actual Windows CI writer hit ERROR_ACCESS_DENIED at os.replace while
    # observation handles were active. Only this operation/code is retried.
    # The caller owns/bounds observation to one fully awaited child. Once that
    # child exits, persistent idle denial still fails within two seconds.
    if windows is None:
        windows = os.name == 'nt'
    def clock():
        return observation.state() if observation is not None else (time.monotonic(), False)
    deadline = clock()[0] + 2.0
    while not stop.is_set():
        try:
            os.replace(source, target)
            return True
        except OSError as error:
            if not windows or getattr(error, 'winerror', None) != 5:
                raise
            idle_now, active = clock()
            if idle_now >= deadline:
                raise
            counter = 'active_reader_retries' if active else 'idle_retries'
            progress[counter] = progress.get(counter, 0) + 1
            progress['replace_winerror5_retries'] += 1
            if stop.wait(0.002):
                return False
    return False


def check_writer_retry():
    # Check only the harness policy on every host; actual Win32 coverage remains
    # the Windows race gate. Never label these injected exceptions OS evidence.
    def error(code):
        value = PermissionError(13, 'injected test denial')
        value.winerror = code
        return value
    def state():
        return threading.Event(), {'replace_winerror5_retries': 0}
    stop, progress = state()
    with mock.patch.object(os, 'replace', side_effect=[error(5), None]) as replace:
        assert replace_for_race('source', 'target', stop, progress, windows=True)
        assert replace.call_count == 2 and progress['replace_winerror5_retries'] == 1
    for windows, code in ((False, 5), (True, 32), (True, 2), (True, None)):
        stop, progress = state()
        failure = error(code)
        with mock.patch.object(os, 'replace', side_effect=failure) as replace:
            try:
                replace_for_race('source', 'target', stop, progress, windows=windows)
            except OSError as caught:
                assert caught is failure
            else:
                raise AssertionError('unexpected retry-policy success')
            assert replace.call_count == 1 and progress['replace_winerror5_retries'] == 0
    stop, progress = state()
    failure = error(5)
    with mock.patch.object(os, 'replace', side_effect=failure) as replace, \
            mock.patch.object(time, 'monotonic', side_effect=[0.0, 1.0, 2.0]):
        try:
            replace_for_race('source', 'target', stop, progress, windows=True)
        except OSError as caught:
            assert caught is failure
        else:
            raise AssertionError('persistent denial did not fail at deadline')
        assert replace.call_count == 2 and progress['replace_winerror5_retries'] == 1
    stop, progress = state()
    stop.set()
    with mock.patch.object(os, 'replace') as replace:
        assert not replace_for_race('source', 'target', stop, progress, windows=True)
        replace.assert_not_called()
    stop, progress = state()
    with mock.patch.object(os, 'replace', side_effect=error(5)) as replace, \
            mock.patch.object(stop, 'wait', side_effect=lambda timeout: (stop.set() or True)):
        assert not replace_for_race('source', 'target', stop, progress, windows=True)
        assert replace.call_count == 1

    # Exclude a long owned-child interval even if the writer is descheduled
    # across the entire begin/end transition. Idle denial must still expire.
    for missed_active_interval in (False, True):
        stop, progress = state()
        observation = ObservationLifecycle()
        calls = 0
        def lifecycle_replace(source, target):
            nonlocal calls
            calls += 1
            if calls == (1 if missed_active_interval else 2):
                observation.end()
            if calls < 3:
                raise error(5)
        samples = [10.0, 10.0, 210.0, 211.0, 211.5] if missed_active_interval else [10.0, 10.0, 110.0, 210.0, 211.0]
        with mock.patch.object(os, 'replace', side_effect=lifecycle_replace), \
                mock.patch.object(time, 'monotonic', side_effect=samples):
            observation.begin()
            assert replace_for_race('source', 'target', stop, progress, windows=True, observation=observation)
        assert calls == 3 and observation.completed_seconds == 200.0
        assert progress['idle_retries'] == (2 if missed_active_interval else 1)


def active_writer(root):
    # Atomic same-sized replacements yield only two valid complete member hashes.
    p = write(root, 'HUNTDAT/a', b'A' * (4 * 1024 * 1024))
    a = oracle('fingerprint', root)
    p.write_bytes(b'B' * (4 * 1024 * 1024))
    b = oracle('fingerprint', root)
    stop = threading.Event()
    observation = ObservationLifecycle()
    advanced = threading.Condition()
    errors = []
    rejected = 0
    changed_rejections = 0
    progress = {'phase': 'starting', 'replacements': 0, 'replace_winerror5_retries': 0}
    overlap = []
    handoffs = []
    observation_seconds = []
    def writer():
        try:
            i = 0
            while not stop.is_set():
                temp = root / 'replacement'
                progress['phase'] = 'write replacement'
                temp.write_bytes((b'A' if i % 2 else b'B') * (4 * 1024 * 1024))
                progress['phase'] = 'replace member'
                if not replace_for_race(temp, p, stop, progress, observation=observation):
                    break
                i += 1
                with advanced:
                    progress['replacements'] = i
                    advanced.notify_all()
            progress['phase'] = 'stopped'
        except Exception as e:
            errors.append({'type': type(e).__name__, 'repr': repr(e),
                           'errno': getattr(e, 'errno', None), 'winerror': getattr(e, 'winerror', None),
                           'traceback': traceback.format_exc()})
            with advanced:
                advanced.notify_all()
    thread = threading.Thread(target=writer)
    thread.start()
    try:
        for _ in range(16):
            started = progress['replacements']
            observation.begin()
            began = time.monotonic()
            try:
                result = native('fingerprint', root)  # owned child bounded to90s, fully awaited
            finally:
                observation.end()
                observation_seconds.append(time.monotonic() - began)
            finished = progress['replacements']
            overlap.append((started, finished))
            # A fresh replacement with no reader alive must complete before the
            # next observation. Persistent denial cannot hide in new readers.
            with advanced:
                moved = advanced.wait_for(lambda: progress['replacements'] > finished or errors, timeout=30)
                assert moved and not errors and progress['replacements'] > finished, (progress, errors, observation_seconds)
                handoffs.append((finished, progress['replacements']))
            if result['ok']:
                assert result['value'] in (a, b), result
            else:
                rejected += 1
                changed_rejections += 'changed while hashing' in result['error']
    finally:
        stop.set()
        join_started = time.monotonic()
        thread.join(timeout=30)
        join_elapsed = time.monotonic() - join_started
    alive = thread.is_alive()
    frame = sys._current_frames().get(thread.ident) if alive else None
    stack = traceback.format_stack(frame) if frame else []
    evidence = {'alive': alive, 'progress': progress, 'errors': errors,
                'replacement_counts_during_observations': overlap, 'rejected': rejected, 'changed_rejections': changed_rejections,
                'join_seconds': join_elapsed, 'writer_stack': stack,
                'observation_seconds': observation_seconds, 'idle_handoffs': handoffs}
    assert not alive and not errors, evidence
    assert len(handoffs) == 16 and all(end > start for start, end in handoffs), evidence
    assert progress['replacements'] >= 2, evidence
    # Windows may serialize rename against a live read. Force both relevant
    # observation windows, with no read handle held at these existing phases.
    # Mutation retries remain idle-bounded even though the child is paused.
    controlled_overlap = []
    for phase, payload in (('initial_inventory', b'A'), ('member_hashed', b'B')):
        temp = root / 'controlled-replacement'
        temp.write_bytes(payload * (4 * 1024 * 1024))
        completed = []
        def replace_at_phase():
            assert replace_for_race(temp, p, threading.Event(), progress)
            completed.append(1)
        result = barrier_change(root, phase, replace_at_phase)
        assert completed == [1] and 'changed while hashing' in result['error'], result
        controlled_overlap.append(phase)
    # Unconstrained rejection/overlap counts are diagnostics: Windows may
    # serialize those operations. Both forced observation windows must prove
    # successful replacement and specific detection, independent of scheduling.
    assert controlled_overlap == ['initial_inventory', 'member_hashed'], evidence
    evidence['controlled_replacement_overlap'] = controlled_overlap
    print('active writer evidence:', evidence, flush=True)
    tree(root)


def barrier_change(root, phase, mutation):
    global CASES
    p = subprocess.Popen([DRIVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    request = {'op': 'fingerprint_barrier', 'root': str(root), 'phase': phase}
    ready = queue.Queue()
    reader = threading.Thread(target=lambda: ready.put(p.stdout.readline()))
    reader.start()
    try:
        p.stdin.write((json.dumps(request) + '\n').encode())
        p.stdin.flush()
        assert ready.get(timeout=30) == b'{"ready":true}\n'
        reader.join(timeout=5)
        assert not reader.is_alive()
        mutation()
        out, err = p.communicate(b'continue\n', timeout=30)
        assert p.returncode == 0, err
        result = json.loads(out)
        assert not result['ok'], (phase, result)
    finally:
        if p.poll() is None:
            p.kill()
        reader.join(timeout=5)
        assert not reader.is_alive()
        p.wait(timeout=5)
        for stream in (p.stdin, p.stdout, p.stderr):
            stream.close()
    CASES += 1
    return result


def synchronized_changes(root):
    member = write(root, 'HUNTDAT/a', b'original')
    def run(phase, mutation):
        return barrier_change(root, phase, mutation)
    # Metadata is changed after the hash and before its reference restat.
    stamp = member.stat().st_mtime_ns + 2000000000
    run('member_hashed', lambda: os.utime(member, ns=(stamp, stamp)))
    run('initial_inventory', lambda: write(root, 'HUNTDAT/added', b'new'))
    run('final_inventory', lambda: (root / 'HUNTDAT/added').unlink())
    # Substitution after inventory never enters a blocking special-file read.
    if os.name == 'posix':
        def replace_fifo():
            member.unlink()
            os.mkfifo(member)
        run('initial_inventory', replace_fifo)
        member.unlink()
        member.write_bytes(b'original')
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
