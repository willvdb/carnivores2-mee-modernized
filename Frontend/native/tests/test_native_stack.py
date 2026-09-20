"""Deep native representation on an owned process stack, with Python evidence.

POSIX caps the child stack at 256 KiB. Windows uses the executable's unchanged
default stack; CI must pass without /STACK overrides. No runtime source changes.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge.session_io import encode
from lodge.store import _unique_object


def limit_stack():
    import resource
    _, hard = resource.getrlimit(resource.RLIMIT_STACK)
    limit = 256 * 1024 if hard == resource.RLIM_INFINITY else min(256 * 1024, hard)
    resource.setrlimit(resource.RLIMIT_STACK, (limit, hard))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def run(executable, mode, source=b''):
    return subprocess.run([executable, mode], input=source, capture_output=True,
                          preexec_fn=limit_stack if os.name == 'posix' else None,
                          timeout=30)


def nested(object_kind, depth):
    return (('{"x":' if object_kind else '[') * depth + '0'
            + ('}' if object_kind else ']') * depth).encode()


def main(executable):
    lifecycle = run(executable, '--deep-stack')
    if lifecycle.returncode != 0:
        raise RuntimeError(f'deep lifecycle exit {lifecycle.returncode}: {lifecycle.stderr.decode(errors="replace")}')
    default_budget = sys.getrecursionlimit()
    for object_kind in (False, True):
        for depth in (900, 1000):
            source = nested(object_kind, depth)
            # 900 is accepted with baseline CPython's default budget. At the
            # native guard boundary CPython may exhaust that budget: temporarily
            # raise only the test oracle budget to compare the exact same bytes.
            if depth == 900:
                json.loads(source.decode(), object_pairs_hook=_unique_object)
            try:
                sys.setrecursionlimit(max(default_budget, 5000))
                value = json.loads(source.decode(), object_pairs_hook=_unique_object)
                expected = encode(value)
            finally:
                sys.setrecursionlimit(default_budget)
            result = run(executable, '--journal-stdin', source)
            if result.returncode != 0 or result.stdout != expected:
                raise RuntimeError(f'deep oracle mismatch: object={object_kind}, depth={depth}, '
                                   f'exit={result.returncode}, stderr={result.stderr!r}')
        for source in (nested(object_kind, 1001),
                       ('{"x":' if object_kind else '[').encode() * 1002):
            result = run(executable, '--journal-stdin', source)
            if result.returncode != 1 or b'JSON nesting resource limit' not in result.stderr:
                raise RuntimeError(f'expected safe depth Error, got exit={result.returncode}, stderr={result.stderr!r}')
    print('Deep copy/move/destruction/error unwind and Python evidence checks passed')


if __name__ == '__main__':
    main(sys.argv[1])
