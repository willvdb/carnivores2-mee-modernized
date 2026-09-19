"""Owned, bounded subprocess lifecycle for the fixed synthetic fixture only."""
import os
import subprocess
from subprocess import Popen
import threading
import time

from .session_io import (capture, read_journal, safe_path, session_root,
                         transition)
from .sessions import execution_spec, snapshot_pins
from .store import FrontendError, now

LOG_LIMIT = 64 * 1024


def preflight(store, root, journal, probe):
    pins = journal['pins']
    current, _ = snapshot_pins(store, pins['association_id'], pins['selection'], probe, pins['codec'])
    if current != pins:
        raise FrontendError('pinned identity, content, engine, policy, codec or source evidence changed')
    spec = journal['execution']
    expected = execution_spec(root, spec.get('scenario'), pins['native_slot'], spec.get('timeout_seconds'))
    if spec != expected:
        raise FrontendError('execution evidence changed or command is not the fixed synthetic fixture')
    for directory in (root / 'baseline', root / 'work/state'):
        entries, _ = capture(directory)
        if entries != journal['baseline_members'] or entries != pins['source_members']:
            raise FrontendError('session baseline/workspace changed before launch')
    safe_path(root / 'work')
    if {p.name for p in (root / 'work').iterdir()} != {'state'}:
        raise FrontendError('unexpected workspace entry before launch')
    safe_path(root / 'logs')
    if list((root / 'logs').iterdir()):
        raise FrontendError('session logs already exist; no relaunch')


class BoundedLog:
    def __init__(self, pipe, path):
        self.pipe, self.path = pipe, path
        self.total = 0
        self.error = None
        self.thread = threading.Thread(target=self.drain, daemon=True)

    def drain(self):
        try:
            with self.path.open('xb') as output, self.pipe:
                while True:
                    chunk = self.pipe.read(4096)
                    if not chunk:
                        break
                    keep = max(0, LOG_LIMIT - self.total)
                    output.write(chunk[:keep])
                    self.total += len(chunk)
                    output.flush()
                os.fsync(output.fileno())
        except OSError as error:
            self.error = str(error)

    def finish(self):
        self.thread.join(timeout=2)
        return {'path': 'logs/' + self.path.name, 'total_bytes': self.total,
                'retained_bytes': min(self.total, LOG_LIMIT), 'truncated': self.total > LOG_LIMIT,
                'error': self.error or ('log reader did not finish' if self.thread.is_alive() else None)}


def stop_owned(process):
    """Never target journal PIDs. The fixed fixture cannot spawn descendants."""
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=0.5)
        except subprocess.TimeoutExpired:
            process.kill()
    return process.wait(timeout=5)


def run_session(store, identity, probe=None, cancel=None):
    """Launch once, with no caller-supplied executable or shell command surface."""
    with store.lock():
        root = session_root(store, identity)
        journal = read_journal(store, identity)
        if journal['state'] != 'prepared':
            raise FrontendError('only a prepared session can launch; recovery never relaunches')
        try:
            preflight(store, root, journal, probe)
        except (FrontendError, OSError, KeyError, TypeError) as error:
            journal['diagnostics'].append({'code': 'preflight-failed', 'message': str(error)})
            transition(root, journal, 'failed', failed_at=now())
            return journal
        log_paths = [safe_path(root / 'logs' / name) for name in ('stdout.log', 'stderr.log')]
        transition(root, journal, 'launching', launch_intent_at=now())
        spec = journal['execution']
        try:
            process = Popen(spec['argv'], executable=spec['executable']['path'],
                cwd=spec['cwd'], shell=False, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True,
                start_new_session=(os.name == 'posix'))
        except OSError as error:
            journal['diagnostics'].append({'code': 'spawn-failed', 'message': str(error)})
            transition(root, journal, 'failed', failed_at=now())
            return journal
        logs = [BoundedLog(process.stdout, log_paths[0]), BoundedLog(process.stderr, log_paths[1])]
        started = now()
        reason = 'exited'
        try:
            for log in logs:
                log.thread.start()
            transition(root, journal, 'running', launched_at=started,
                       process={'pid': process.pid, 'started_at': started, 'exit_code': None})
            deadline = time.monotonic() + spec['timeout_seconds']
            while process.poll() is None:
                if cancel is not None and cancel.is_set():
                    reason = 'cancelled'
                    stop_owned(process)
                    break
                if time.monotonic() >= deadline:
                    reason = 'timeout'
                    stop_owned(process)
                    break
                try:
                    process.wait(timeout=0.05)
                except subprocess.TimeoutExpired:
                    pass
        except KeyboardInterrupt:
            reason = 'cancelled'
            stop_owned(process)
        except BaseException:
            # Even a failed running-journal write must not leave an owned child.
            stop_owned(process)
            for log in logs:
                if log.thread.ident is not None:
                    log.finish()
            raise
        returned = now()
        output = {log.path.stem: log.finish() for log in logs}
        code = process.wait(timeout=5)
        if code != 0 or reason != 'exited':
            journal['diagnostics'].append({'code': 'process-' + reason, 'exit_code': code})
        if any(log['error'] for log in output.values()):
            journal['diagnostics'].append({'code': 'log-capture-failed'})
        journal['capabilities']['synthetic_child_lifecycle'] = 'completed'
        transition(root, journal, 'returned', returned_at=returned, logs=output,
                   process={'pid': process.pid, 'started_at': started, 'returned_at': returned,
                            'exit_code': code, 'stop_reason': reason})
        return journal


def recover_session(store, identity, probe=None):
    """Never infer child death from a PID or read potentially live returned state."""
    with store.lock():
        root = session_root(store, identity)
        journal = read_journal(store, identity)
        if journal['state'] in ('launching', 'running'):
            journal['diagnostics'].append({'code': 'process-ownership-lost',
                'message': 'Child may still exist. No signal, relaunch or state capture performed.'})
            transition(root, journal, 'interrupted', interrupted_at=now())
        elif journal['state'] in ('returned', 'inspecting'):
            from .reconciliation import reconcile_locked
            return reconcile_locked(store, root, journal, probe)
        return journal
