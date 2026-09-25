"""Reference CLI driver for differential tests: unchanged frontend.main().

With --fixture-policy it installs the SAME labelled asset-free policy doubles
that the test-only c2-frontend-native-fixture executable installs
(native/tests/fixture_cli.cpp). Nothing here certifies Genesis content.
"""
import copy
from pathlib import Path
import sys
from unittest.mock import patch

FRONTEND = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(FRONTEND))
import frontend  # noqa: E402
from lodge.genesis_hunt import POLICY_ID  # noqa: E402

SMOD = 'smod=0.85,0.70,0.80,1.0,1.25,1.0'


def candidate(slot, selection, observer):
    argv = [f'reg={slot}', 'prj=huntdat/areas/area1', 'din=0' if observer else 'din=1',
            'wep=0' if observer else 'wep=1', f"dtm={selection['time_of_day']}"]
    return argv + (['-observ'] if observer else []) + [SMOD]


def observer_double(revision, catalog, slot, selection, score):
    return {'adapter': 'asset-free-policy-double', 'candidate_argv': candidate(slot, selection, True)}


def hunt_double(revision, catalog, slot, selection, score):
    return {'adapter': POLICY_ID, 'fixture_only': 'authored disposable state, NOT Genesis',
            'selection': copy.deepcopy(selection), 'candidate_argv': candidate(slot, selection, False)}


def main(argv):
    fixture = argv[:1] == ['--fixture-policy']
    if fixture:
        argv = argv[1:]
    sys.argv = ['frontend.py', *argv]
    if not fixture:
        return frontend.main()
    with patch('lodge.genesis.observer_policy', observer_double), \
            patch('lodge.native_observer.observer_policy', observer_double), \
            patch('lodge.native_hunt.hunt_policy', hunt_double), \
            patch('lodge.native_continuation.hunt_policy', hunt_double):
        return frontend.main()


if __name__ == '__main__':
    raise SystemExit(main(sys.argv[1:]))
