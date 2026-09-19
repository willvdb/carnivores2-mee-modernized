"""Experimental, explicitly trusted Genesis observer validation (journal v2).

This adapter never selects bundled executables or promotes returned state. Trust
is supplied again at run time; a journal alone cannot authorize an executable.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from .catalog import project
from .genesis import POLICY_ID, observer_policy
from .session_io import (capture, persist, read_journal, safe_path, session_root,
                         write_blobs)
from .sessions import executable_evidence, snapshot_pins
from .store import FrontendError, _unique_object, new_id, now

CAPABILITY = {'contract': 'c2-engine-session', 'version': 1, 'state': 'sav-sab-pair',
              'layout': 'state-config-output-v1', 'performance_capture': False}
KIND = 'experimental-native-observer-v1'
CONFIG = b'display_mode 0\nresolution 800x600\nfps_limit 1\nglperf_logging 0\n'


def trusted_engine(engine, digest, experimental):
    if experimental is not True:
        raise FrontendError('native observer requires the explicit experimental gate')
    if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
        raise FrontendError('explicit trusted engine SHA-256 is required')
    evidence = executable_evidence(engine)
    safe_path(Path(evidence['path']))
    if evidence['sha256'] != digest:
        raise FrontendError('selected engine does not match the explicitly trusted hash')
    return evidence


def query_contract(evidence):
    """Only call after explicit binary trust; capability text is not certification."""
    with tempfile.TemporaryDirectory(prefix='c2-contract-') as directory:
        with tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
            try:
                result = subprocess.run([evidence['path'], '--session-capabilities'],
                    executable=evidence['path'], cwd=directory, shell=False,
                    stdin=subprocess.DEVNULL, stdout=out, stderr=err, timeout=5)
            except subprocess.TimeoutExpired as error:
                raise FrontendError('trusted engine capability query timed out') from error
            out.seek(0); err.seek(0)
            response, errors = out.read(4097), err.read(4097)
    if result.returncode or errors or len(response) > 4096:
        raise FrontendError('trusted engine capability query failed')
    try:
        value = json.loads(response, object_pairs_hook=_unique_object)
    except (ValueError, UnicodeError) as error:
        raise FrontendError('invalid engine capability response') from error
    # Exact encoded types too: JSON true is not version 1.
    if (value != CAPABILITY or type(value.get('version')) is not int
            or type(value.get('performance_capture')) is not bool):
        raise FrontendError('unsupported engine session contract')
    if executable_evidence(evidence['path']) != evidence:
        raise FrontendError('engine changed during capability query')
    return value


def native_pins(store, association, selection, probe, expected_codec=None):
    pins, blobs = snapshot_pins(store, association, selection, probe, expected_codec)
    slot = pins['native_slot']
    if set(blobs) != {f'trophy{slot:02d}.sav', f'trophy{slot:02d}.sab'}:
        raise FrontendError('engine contract v1 requires an existing complete SAV/SAB pair')
    instance = pins['instance']
    catalog = project(instance['path'], instance['dialect_hint'])
    policy = observer_policy(pins['revision'], catalog, slot, selection,
                            pins['source_observation'][f'trophy{slot:02d}.sav']['score'])
    pins.update(adapter=POLICY_ID, observer_policy=policy)
    return pins, blobs


def disjoint(workspace, pins, executable, include_source=True):
    workspace = safe_path(workspace)
    protected_roots = [Path(pins['instance']['path']), Path(executable['path']).parent]
    if include_source:
        protected_roots.append(Path(pins['source_root']))
    for protected in protected_roots:
        protected = safe_path(protected)
        if workspace.is_relative_to(protected) or protected.is_relative_to(workspace):
            raise FrontendError('native workspace overlaps protected source/content/engine')


def execution_spec(root, pins, evidence, capability, timeout):
    if type(timeout) not in (int, float) or not 30 <= timeout <= 3600:
        raise FrontendError('developer observer validation timeout must be 30..3600 seconds')
    disjoint(root, pins, evidence)
    # observer_policy already validates these arguments. The slot is now supplied
    # exclusively by the versioned contract, never the legacy reg= parser.
    policy_args = pins['observer_policy']['candidate_argv']
    if policy_args[0] != f"reg={pins['native_slot']}":
        raise FrontendError('observer policy slot disagrees with native state')
    argv = [evidence['path'], '--session-contract=1',
            f"--session-slot={pins['native_slot']}", '--session-root=' + str(root / 'work'),
            '--session-source=' + pins['source_root'], '--session-baseline=' + str(root / 'baseline'),
            *policy_args[1:]]
    return {'kind': KIND, 'executable': evidence, 'contract': capability, 'argv': argv,
            'cwd': pins['instance']['path'], 'shell': False, 'timeout_seconds': timeout,
            'config_sha256': hashlib.sha256(CONFIG).hexdigest()}


def validate_workspace(root, journal, returning=False):
    work = safe_path(root / 'work')
    if {p.name for p in work.iterdir()} != {'state', 'config', 'output'}:
        raise FrontendError('unexpected native workspace entry')
    config, blobs = capture(work / 'config')
    if set(blobs) != {'config.cfg'} or len(config) != 1 or blobs['config.cfg'] != CONFIG:
        raise FrontendError('native session configuration changed')
    output, _ = capture(work / 'output')
    if not returning and output:
        raise FrontendError('native output already exists; no relaunch')
    for entry in output:
        if (entry['type'] != 'file' or not re.fullmatch(r'carnivor\.log|render\.log|HUNT[0-9]{4,}\.BMP', entry['path'])):
            raise FrontendError('unexpected native output; retained for review')


def prepare_native(store, association, area, engine, digest, experimental=False,
                   time_of_day=1, timeout=900, probe=None):
    evidence = trusted_engine(engine, digest, experimental)
    selection = {'area': area, 'mode': 'observer', 'time_of_day': time_of_day,
                 'licenses': [], 'weapons': [], 'equipment': []}
    # Read-only validation before creating even the store lock in an unsafe root.
    pins, _ = native_pins(store, association, selection, probe)
    disjoint(store.directory, pins, evidence, include_source=False)
    with store.lock():
        pins, blobs = native_pins(store, association, selection, probe)
        capability = query_contract(evidence)
        identity = new_id(); root = session_root(store, identity)
        spec = execution_spec(root, pins, evidence, capability, timeout)
        root.mkdir(parents=True, exist_ok=False)
        write_blobs(root / 'baseline', blobs)
        write_blobs(root / 'work/state', blobs)
        write_blobs(root / 'work/config', {'config.cfg': CONFIG})
        (root / 'work/output').mkdir(); (root / 'logs').mkdir()
        created = now()
        journal = {'schema_version': 2, 'id': identity, 'path_flavor': os.name,
            'state': 'prepared', 'created_at': created, 'prepared_at': created,
            'transitions': [{'state': 'prepared', 'at': created}],
            'pins': pins, 'baseline_members': pins['source_members'], 'execution': spec,
            'diagnostics': [], 'process': None,
            'reconciliation': {'authority': 'original-managed-snapshot', 'promotion': 'deferred', 'status': 'pending'},
            'process_launch_allowed': False, 'synthetic_process_launch_allowed': False,
            'experimental_native_process_launch_allowed': True,
            'capabilities': {'session_workspace': 'validated', 'engine_contract': capability,
                'synthetic_child_lifecycle': 'not-executed', 'native_observer_lifecycle': 'not-executed',
                'genesis_policy': 'structurally-validated', 'engine_process_executed': False,
                'observer_session_launched': False, 'returned_native_state_readable': 'unknown',
                'hunt_save_round_trip_validated': False, 'modern_engine_compatibility': 'unknown'}}
        persist(root, journal)
        if os.name == 'posix':
            for path in (root / 'work', root, root.parent, store.directory):
                fd = os.open(path, os.O_RDONLY)
                try:
                    os.fsync(fd)
                finally:
                    os.close(fd)
        return journal


def preflight_native(store, root, journal, probe, authorization):
    if not authorization:
        raise FrontendError('native sessions require the separate explicitly trusted developer run command')
    engine, digest, experimental = authorization
    evidence = trusted_engine(engine, digest, experimental)
    spec, pins = journal['execution'], journal['pins']
    if evidence != spec['executable']:
        raise FrontendError('selected engine differs from the prepared trusted engine')
    current, _ = native_pins(store, pins['association_id'], pins['selection'], probe, pins['codec'])
    if current != pins:
        raise FrontendError('native identity/content/policy/source pins changed')
    # Re-query only the same explicitly trusted, unchanged binary.
    expected = execution_spec(root, pins, evidence, query_contract(evidence), spec.get('timeout_seconds'))
    if expected != spec:
        raise FrontendError('native execution specification changed')
    for directory in (root / 'baseline', root / 'work/state'):
        entries, _ = capture(directory)
        if entries != journal['baseline_members'] or entries != pins['source_members']:
            raise FrontendError('native baseline/work state changed before launch')
    validate_workspace(root, journal)
    if list(safe_path(root / 'logs').iterdir()):
        raise FrontendError('session logs already exist; no relaunch')


def run_native(store, identity, engine, digest, experimental=False, probe=None, cancel=None):
    from .session_runner import run_session
    journal = read_journal(store, identity)
    if journal['schema_version'] != 2:
        raise FrontendError('developer native run requires a native journal')
    # Gate before changing the journal or acquiring its writer lock.
    trusted_engine(engine, digest, experimental)
    return run_session(store, identity, probe, cancel,
                       native_authorization=(engine, digest, experimental))
