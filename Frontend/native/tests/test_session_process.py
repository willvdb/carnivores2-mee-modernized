"""Native owned-child supervisor against lodge.session_runner's BoundedLog/stop_owned.

The reference run_session process portion is reproduced here verbatim over the
unchanged BoundedLog and stop_owned (the journal transitions are not involved),
driving the same compiled child; results, log dicts and log bytes are compared.
"""
import json
import os
from pathlib import Path
import re
import signal
import stat
import subprocess
from subprocess import Popen
import sys
import tempfile
import threading
import time
import unittest
import warnings

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge.session_runner import LOG_LIMIT, BoundedLog, stop_owned  # noqa: E402

DRIVER, CHILD = sys.argv[1:3]
POSIX = os.name == 'posix'
ISO = re.compile(r'^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(\.\d{6})?\+00:00$')


def reference(argv, executable, cwd, timeout, logs, cancel_after=None):
    """run_session after preflight: Popen, BoundedLog threads, poll loop, finish."""
    process = Popen(argv, executable=executable, cwd=cwd, shell=False, stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, close_fds=True,
                    start_new_session=(os.name == 'posix'))
    streams = [BoundedLog(process.stdout, logs / 'stdout.log'), BoundedLog(process.stderr, logs / 'stderr.log')]
    REFERENCE_READERS.extend(log.thread for log in streams)
    cancel = threading.Event()
    if cancel_after is not None:
        threading.Timer(cancel_after, cancel.set).start()
    reason = 'exited'
    for log in streams:
        log.thread.start()
    deadline = time.monotonic() + timeout
    while process.poll() is None:
        if cancel.is_set():
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
    output = {log.path.stem: log.finish() for log in streams}
    code = process.wait(timeout=5)
    return {'exit_code': code, 'reason': reason, 'logs': output}


# Reference BoundedLog daemon threads; one may outlive its run while a test
# descendant holds the pipe, keeping its log file open (Windows cannot delete it).
REFERENCE_READERS = []


def pattern(stream, count):
    return bytes(((i * 7 + stream) & 255) for i in range(count))


class SessionProcess(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        self.markers = []

    def tearDown(self):
        for marker in self.markers:
            if marker.exists():
                try:
                    os.kill(int(marker.read_text()), signal.SIGKILL if POSIX else signal.SIGTERM)
                except (OSError, ValueError):
                    pass  # already gone (Windows reports WinError 87 for a reaped pid)
        for thread in REFERENCE_READERS:
            thread.join(timeout=10)
        REFERENCE_READERS.clear()
        self.temp.cleanup()

    def marker(self, name):
        path = self.root / name
        self.markers.append(path)
        return path

    def logs(self, name):
        directory = self.root / name
        directory.mkdir()
        return directory

    def native(self, args, timeout=5, cwd=None, cancel_ms=None, started='ok', executable=None, name='native', logs=None):
        logs = logs or self.logs(name)
        command = [DRIVER, 'run', executable or CHILD, str(cwd or self.root), str(timeout),
                   str(logs / 'stdout.log'), str(logs / 'stderr.log'),
                   '-' if cancel_ms is None else str(cancel_ms), started, '--', CHILD, *args]
        done = subprocess.run(command, capture_output=True, timeout=60, check=True)
        report = json.loads(done.stdout.decode())
        report['dir'] = logs
        if report['kind'] == 'ok':
            self.assertTrue(report['child_gone'], report)
            self.assertTrue(report['started_pid_matches'] and report['started_at_matches'], report)
            self.assertEqual(report['started_calls'], 1)
            self.assertGreater(report['pid'], 0)
            for stamp in (report['started_at'], report['returned_at']):
                self.assertRegex(stamp, ISO)
            self.assertLessEqual(report['started_at'], report['returned_at'])
        return report

    def oracle(self, args, timeout=5, cwd=None, cancel_after=None, name='reference'):
        logs = self.logs(name)
        result = reference([CHILD, *args], CHILD, str(cwd or self.root), timeout, logs, cancel_after)
        result['dir'] = logs
        return result

    def same(self, args, timeout=5, cancel_ms=None, expect_reason='exited'):
        got = self.native(args, timeout, cancel_ms=cancel_ms)
        expected = self.oracle(args, timeout, cancel_after=None if cancel_ms is None else cancel_ms / 1000)
        self.assertEqual(got['kind'], 'ok', got)
        self.assertEqual((got['exit_code'], got['reason']), (expected['exit_code'], expected['reason']))
        self.assertEqual(got['reason'], expect_reason)
        self.assertEqual(got['logs'], expected['logs'])
        self.assertEqual(list(got['logs']['stdout']), list(expected['logs']['stdout']))
        for name in ('stdout.log', 'stderr.log'):
            self.assertEqual((got['dir'] / name).read_bytes(), (expected['dir'] / name).read_bytes(), name)
        return got, expected

    def test_exit_codes(self):
        got, _ = self.same(['exit', '0'])
        self.assertEqual(got['exit_code'], 0)
        self.assertEqual(got['logs']['stdout'], {'path': 'logs/stdout.log', 'total_bytes': 0,
                                                 'retained_bytes': 0, 'truncated': False, 'error': None})
        got = self.native(['exit', '3'], name='three')
        self.assertEqual((got['exit_code'], got['reason']), (3, 'exited'))

    @unittest.skipUnless(POSIX, 'signal exit codes are POSIX')
    def test_signal_exit_code(self):
        got, _ = self.same(['signal', str(signal.SIGTERM)])
        self.assertEqual(got['exit_code'], -signal.SIGTERM)
        self.assertEqual((got['dir'] / 'stdout.log').read_bytes(), b'before signal')

    def test_hang_reaches_timeout_and_stops_owned_child(self):
        marker = self.marker('hang.pid')
        start = time.monotonic()
        got, _ = self.same(['hang', str(marker)], timeout=0.4, expect_reason='timeout')
        self.assertLess(time.monotonic() - start, 10)
        self.assertEqual(got['exit_code'], -signal.SIGTERM if POSIX else 1)
        self.assertEqual((got['dir'] / 'stdout.log').read_bytes(), b'hanging')
        if POSIX:
            with self.assertRaises(ProcessLookupError):
                os.kill(int(marker.read_text()), 0)

    def test_term_ignored_escalates_to_kill(self):
        marker = self.marker('ignore.pid')
        start = time.monotonic()
        got, _ = self.same(['ignore-term', str(marker)], timeout=0.3, expect_reason='timeout')
        self.assertGreaterEqual(time.monotonic() - start, 0.3)
        self.assertLess(time.monotonic() - start, 15)
        self.assertEqual(got['exit_code'], -signal.SIGKILL if POSIX else 1)

    def test_cancellation_flag(self):
        marker = self.marker('cancel.pid')
        start = time.monotonic()
        got, _ = self.same(['hang', str(marker)], timeout=20, cancel_ms=250, expect_reason='cancelled')
        self.assertLess(time.monotonic() - start, 10)
        self.assertEqual(got['exit_code'], -signal.SIGTERM if POSIX else 1)

    def test_flood_both_streams_is_drained_and_bounded(self):
        size = 3 * LOG_LIMIT + 12345
        got, expected = self.same(['flood', str(size), str(size + 7)])
        self.assertEqual(got['exit_code'], 0)
        for stream, key, count in ((0, 'stdout', size), (1, 'stderr', size + 7)):
            self.assertEqual(got['logs'][key], {'path': f'logs/{key}.log', 'total_bytes': count,
                                                'retained_bytes': LOG_LIMIT, 'truncated': True, 'error': None})
            self.assertEqual((got['dir'] / f'{key}.log').read_bytes(), pattern(stream, LOG_LIMIT))
        self.assertEqual(expected['logs'], got['logs'])

    def test_flood_then_hang_counts_bytes_before_timeout(self):
        # Far more than any pipe buffer on both streams, then a hang: the
        # owner must keep draining or the child would never reach the sleep.
        size = 2 * 1024 * 1024
        got, _ = self.same(['flood-hang', str(size)], timeout=1.0, expect_reason='timeout')
        self.assertEqual(got['logs']['stdout']['total_bytes'], size)
        self.assertEqual(got['logs']['stderr']['total_bytes'], size)
        self.assertTrue(got['logs']['stdout']['truncated'])

    def test_binary_bytes(self):
        got, _ = self.same(['binary'])
        self.assertEqual((got['dir'] / 'stdout.log').read_bytes(), bytes(range(256)) * 4)
        self.assertEqual((got['dir'] / 'stderr.log').read_bytes(), b'err\0bin\0')
        self.assertEqual(got['logs']['stdout']['retained_bytes'], 1024)

    def test_missing_executable_is_spawn_failure_without_logs(self):
        got = self.native(['exit', '0'], executable=str(self.root / 'absent'))
        self.assertEqual(got['kind'], 'oserror', got)
        self.assertEqual(got['started_calls'], 0)
        self.assertFalse(got['stdout_log_exists'] or got['stderr_log_exists'])
        # The reference raises OSError from Popen for the same executable.
        with self.assertRaises(OSError) as caught:
            reference([CHILD, 'exit', '0'], str(self.root / 'absent'), str(self.root), 5, self.logs('oracle-missing'))
        self.assertEqual(got['code'], caught.exception.errno)
        self.assertFalse((self.root / 'oracle-missing' / 'stdout.log').exists())

    @unittest.skipUnless(POSIX, 'execute permission bits are POSIX')
    def test_non_executable_file_is_spawn_failure_without_logs(self):
        plain = self.root / 'plain'
        plain.write_bytes(b'#!/bin/sh\nexit 0\n')
        plain.chmod(stat.S_IRUSR | stat.S_IWUSR)
        got = self.native(['exit', '0'], executable=str(plain))
        self.assertEqual((got['kind'], got['started_calls']), ('oserror', 0), got)
        self.assertFalse(got['stdout_log_exists'] or got['stderr_log_exists'])
        with self.assertRaises(PermissionError) as caught:
            reference([CHILD, 'exit', '0'], str(plain), str(self.root), 5, self.logs('oracle-plain'))
        self.assertEqual(got['code'], caught.exception.errno)

    def test_missing_cwd_is_spawn_failure_without_logs(self):
        got = self.native(['exit', '0'], cwd=self.root / 'nowhere')
        self.assertEqual((got['kind'], got['started_calls']), ('oserror', 0), got)
        self.assertFalse(got['stdout_log_exists'] or got['stderr_log_exists'])
        with self.assertRaises(OSError) as caught:
            reference([CHILD, 'exit', '0'], CHILD, str(self.root / 'nowhere'), 5, self.logs('oracle-cwd'))
        # Native reports the OS code; on Windows that is winerror (errno is CPython's mapping).
        self.assertEqual(got['code'], caught.exception.errno if POSIX else caught.exception.winerror)
        if POSIX:
            self.assertEqual(got['path'], str(self.root / 'nowhere'))

    def test_existing_log_file_is_refused_exclusively(self):
        logs = self.logs('native')
        (logs / 'stdout.log').write_bytes(b'stale')
        got = self.native(['flood', '100', '20'], logs=logs)
        self.assertEqual((got['kind'], got['exit_code'], got['reason']), ('ok', 0, 'exited'), got)
        self.assertEqual((logs / 'stdout.log').read_bytes(), b'stale')
        self.assertEqual((logs / 'stderr.log').read_bytes(), pattern(1, 20))
        # The reference records str(OSError) for the failed exclusive open and
        # never reads that pipe (total 0); the native supervisor keeps draining
        # (documented difference) but records the same error text.
        oracle = self.logs('reference')
        (oracle / 'stdout.log').write_bytes(b'stale')
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', ResourceWarning)  # the reference leaves that pipe open
            expected = reference([CHILD, 'flood', '100', '20'], CHILD, str(self.root), 5, oracle)
        self.assertEqual(expected['logs']['stdout']['error'].replace(repr(str(oracle))[1:-1], repr(str(logs))[1:-1]),
                         got['logs']['stdout']['error'])
        self.assertEqual(got['logs']['stdout']['error'], f"[Errno 17] File exists: {str(logs / 'stdout.log')!r}")
        self.assertEqual(got['logs']['stdout']['total_bytes'], 100)
        self.assertEqual(expected['logs']['stdout']['total_bytes'], 0)
        self.assertEqual(got['logs']['stderr'], expected['logs']['stderr'])

    def test_on_started_failure_stops_child_and_finishes_logs(self):
        marker = self.marker('started.pid')
        start = time.monotonic()
        got = self.native(['hang', str(marker)], timeout=30, started='throw')
        self.assertEqual(got['kind'], 'started-threw', got)
        self.assertLess(time.monotonic() - start, 10)
        self.assertEqual(got['started_calls'], 1)
        self.assertTrue(got['child_gone'])
        # Logs were created before on_started; whether the child got to write
        # before SIGTERM is a race, so only the bounded stop is asserted.
        self.assertTrue(got['stdout_log_exists'] and got['stderr_log_exists'])
        self.assertGreater(got['pid'], 0)
        if POSIX:
            with self.assertRaises(ProcessLookupError):
                os.kill(got['pid'], 0)

    def test_arguments_reach_child_literally(self):
        args = ['args', '', 'a b', 'tabs\there', '"', '\\', 'space tail\\', 'x\\"y', '; $(never-a-shell) & | > out',
                "'single'", '*.sav', '~', '$HOME', '%PATH%', 'plain']
        got, expected = self.same(args)
        self.assertEqual((got['dir'] / 'stdout.log').read_bytes(), b''.join(a.encode() + b'\0' for a in [CHILD, *args]))

    def test_cwd_is_honoured(self):
        inner = self.root / 'work dir'
        inner.mkdir()
        got = self.native(['cwd'], cwd=inner)
        expected = self.oracle(['cwd'], cwd=inner)
        self.assertEqual(got['kind'], 'ok', got)
        self.assertEqual((got['dir'] / 'stdout.log').read_bytes(), (expected['dir'] / 'stdout.log').read_bytes())
        self.assertEqual(Path(os.fsdecode((got['dir'] / 'stdout.log').read_bytes())).resolve(), inner.resolve())

    def test_stdin_is_null(self):
        got, _ = self.same(['stdin'])
        self.assertEqual((got['dir'] / 'stdout.log').read_bytes(), b'eof')

    def test_closed_streams_still_wait_for_the_child(self):
        marker = self.marker('closed.pid')
        got, _ = self.same(['close-hang', str(marker)], timeout=0.4, expect_reason='timeout')
        self.assertEqual(got['logs']['stdout']['error'], None)
        self.assertEqual(got['logs']['stdout']['total_bytes'], 0)

    def test_descendant_holding_pipes_bounds_log_finish(self):
        marker = self.marker('descendant.pid')
        start = time.monotonic()
        got = self.native(['descendant', str(marker)], timeout=10)
        native_elapsed = time.monotonic() - start
        self.assertEqual((got['kind'], got['exit_code'], got['reason']), ('ok', 0, 'exited'), got)
        self.assertGreaterEqual(native_elapsed, 2)
        self.assertLess(native_elapsed, 8)
        for key in ('stdout', 'stderr'):
            self.assertEqual(got['logs'][key]['error'], 'log reader did not finish')
        # Both the exited child and its descendant wrote 13 bytes, but the
        # pipe never reached EOF: like the reference's read(4096), nothing
        # short of a whole chunk is accounted while the reader is unfinished.
        self.assertEqual(got['logs']['stdout']['total_bytes'], 0)
        self.assertEqual((got['dir'] / 'stdout.log').read_bytes(), b'')
        if POSIX:
            os.kill(int(marker.read_text()), signal.SIGKILL)
        marker.unlink()
        expected = self.oracle(['descendant', str(marker)], timeout=10)
        self.assertEqual(expected['logs'], got['logs'])
        self.assertEqual(expected['reason'], 'exited')


if __name__ == '__main__':
    unittest.main(argv=sys.argv[:1])
