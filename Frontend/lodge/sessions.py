"""Explicit managed-state sessions. Native engine execution is not enabled."""
import copy
import os
from pathlib import Path
import sys

from .catalog import project
from .discovery import hash_file, inspect_instance
from .profiles import association_root
from .session_io import (capture, inspect_bytes, persist, safe_path, session_root,
                         write_blobs)
from .store import FrontendError, new_id, now

SYNTHETIC_POLICY = 'frontend-synthetic-observer-v1'
SCENARIOS = ('unchanged', 'sav', 'pair', 'nonzero', 'changed-nonzero',
             'corrupt-sav', 'corrupt-sab', 'missing-sab', 'deleted-sav',
             'extra', 'registration', 'hang', 'terminated', 'logs')
FIXTURE = Path(__file__).with_name('synthetic_child.py').resolve()
LITERAL_ARGUMENT = 'literal ; $(never-a-shell) & spaces'


def executable_evidence(path):
    path = Path(path).expanduser().resolve(strict=True)
    if not path.is_file():
        raise FrontendError('explicit executable must be a regular file')
    return {'path': str(path), 'sha256': hash_file(path)}


def codec_evidence(probe):
    path = probe or os.environ.get('C2_PROFILE_PROBE')
    if not path:
        raise FrontendError('session requires an explicit profile codec helper')
    return executable_evidence(path)


def snapshot_pins(store, association_id, selection, probe):
    data = store.read()
    association = data['associations'].get(association_id)
    if association is None:
        raise FrontendError('unknown association ID')
    if association['ownership'] != 'managed' or association['origin'] != 'personal':
        raise FrontendError('sessions require managed personal state; referenced/bundled/unknown rejected')
    hunter = data['hunters'][association['hunter_id']]
    if hunter.get('archived_at'):
        raise FrontendError('archived hunter cannot start a session')
    instance = data['instances'][association['instance_id']]
    observation = inspect_instance(instance)
    if (not observation['recognized'] or observation.get('revision_changed')
            or association['revision'] != instance['revision']):
        raise FrontendError('session requires exact unchanged content revision')
    if observation['engine_review_required']:
        raise FrontendError('engine evidence review blocks session')
    if instance['dialect_hint'] not in ('mee-newer', 'c2-classic'):
        raise FrontendError('synthetic selection requires an explicit supported catalog hint')
    catalog = project(instance['path'], instance['dialect_hint'])
    if any(d['code'] in ('unclosed-block', 'unmatched-brace', 'dialect-conflict',
                         'explicit-areas-uninterpreted') for d in catalog['diagnostics']):
        raise FrontendError('ambiguous catalog cannot prepare a session')
    if (set(selection) != {'area', 'mode', 'time_of_day', 'licenses', 'weapons', 'equipment'}
            or selection['mode'] != 'observer' or type(selection['time_of_day']) is not int
            or selection['time_of_day'] not in (0, 1, 2)
            or selection['licenses'] or selection['weapons'] or selection['equipment']):
        raise FrontendError('session policy permits observer with no loadout only')
    area = next((a for a in catalog['areas'] if a['id'] == selection['area']), None)
    if not area or not area['launch_stem']:
        raise FrontendError('session requires an unambiguous advertised area')
    slot = association['filename_slot']
    if slot not in range(8) or association['state_key'] != f'trophy{slot:02d}':
        raise FrontendError('session requires a canonical root native slot')
    root = safe_path(association_root(store, instance, association))
    entries, blobs = capture(root)
    expected = sorted([{'path': f['path'], 'size': f['size'], 'sha256': f['sha256'],
                        'type': 'file'} for f in association['files']], key=lambda e: e['path'])
    allowed = {f'trophy{slot:02d}.sav', f'trophy{slot:02d}.sab'}
    if (entries != expected or not blobs or f'trophy{slot:02d}.sav' not in blobs
            or set(blobs) - allowed):
        raise FrontendError('managed source membership/bytes differ or require review')
    codec = codec_evidence(probe)
    decoded, diagnostics = inspect_bytes(blobs, slot, codec['path'])
    if diagnostics:
        raise FrontendError('managed source is unreadable or has a registration mismatch')
    # Observations may change without changing identity/provenance.
    provenance = lambda item: {k: v for k, v in item.items() if k != 'last_observation'}
    pins = {'hunter_id': hunter['id'], 'instance_id': instance['id'],
            'association_id': association_id, 'hunter': hunter,
            'instance': provenance(instance), 'association': provenance(association),
            'revision': instance['revision'], 'engine_evidence': observation['engine_evidence'],
            'native_slot': slot, 'source_root': str(root), 'source_members': entries,
            'source_observation': decoded, 'selection': selection,
            'area_stem': area['launch_stem'], 'codec': codec,
            'adapter': SYNTHETIC_POLICY}
    return copy.deepcopy(pins), blobs


def execution_spec(root, scenario, slot, timeout):
    if scenario not in SCENARIOS:
        raise FrontendError('unknown controlled synthetic scenario')
    if type(timeout) not in (int, float) or not 0.05 <= timeout <= 30:
        raise FrontendError('synthetic timeout must be between 0.05 and 30 seconds')
    executable = executable_evidence(sys.executable)
    fixture = executable_evidence(FIXTURE)
    return {'kind': 'controlled-synthetic', 'executable': executable,
            'fixture': fixture, 'argv': [executable['path'], '-I', fixture['path'],
                '--scenario', scenario, '--slot', str(slot), '--literal', LITERAL_ARGUMENT],
            'cwd': str(root / 'work'), 'timeout_seconds': timeout,
            'scenario': scenario, 'shell': False}


def prepare_session(store, association_id, area_id, scenario='unchanged',
                    time_of_day=1, timeout=5, probe=None):
    selection = {'area': area_id, 'mode': 'observer', 'time_of_day': time_of_day,
                 'licenses': [], 'weapons': [], 'equipment': []}
    with store.lock():
        pins, blobs = snapshot_pins(store, association_id, selection, probe)
        identity = new_id()
        root = session_root(store, identity)
        spec = execution_spec(root, scenario, pins['native_slot'], timeout)
        root.mkdir(parents=True, exist_ok=False)
        write_blobs(root / 'baseline', blobs)
        write_blobs(root / 'work' / 'state', blobs)
        (root / 'logs').mkdir()
        created = now()
        journal = {'schema_version': 1, 'id': identity, 'path_flavor': os.name,
                   'state': 'prepared', 'created_at': created, 'prepared_at': created,
                   'transitions': [{'state': 'prepared', 'at': created}],
                   'pins': pins, 'baseline_members': pins['source_members'],
                   'execution': spec, 'diagnostics': [], 'process': None,
                   'reconciliation': {'authority': 'original-managed-snapshot',
                                      'promotion': 'deferred', 'status': 'pending'},
                   'capabilities': {'session_workspace': 'validated',
                       'synthetic_child_lifecycle': 'not-executed',
                       'genesis_policy': 'not-evaluated', 'engine_process_executed': False,
                       'observer_session_launched': False, 'returned_native_state_readable': 'unknown',
                       'hunt_save_round_trip_validated': False,
                       'modern_engine_compatibility': 'unknown'},
                   'process_launch_allowed': False,
                   'synthetic_process_launch_allowed': True}
        persist(root, journal)
        if os.name == 'posix':
            fd = os.open(root.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
        return journal
