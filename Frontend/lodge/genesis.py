"""One revision-pinned current-MEE/Genesis observer intent, not generic MEE."""
import re

from .catalog import project
from .sessions import executable_evidence, snapshot_pins
from .store import FrontendError, new_id, now

POLICY_ID = 'genesis-current-mee-observer-v1'
GENESIS_REVISION = {
    'algorithm': 'huntdat-sha256-v1',
    'sha256': '9c6fc5221744ad8e9a74689d308ba572b6aefe6cd6c317e030e5774757c2bf65',
    'file_count': 344, 'byte_count': 768073101,
}
EXPECTED_COUNTS = {'areas': 8, 'licenses': 9, 'weapons': 8, 'equipment': 4, 'physical_maps': 9}


def observer_policy(revision, catalog, slot, selection, score):
    """Validate static observations. Caller must obtain a fresh full fingerprint."""
    if revision != GENESIS_REVISION:
        raise FrontendError('Genesis policy refuses an unpinned content revision')
    if catalog['dialect']['observed'] != 'mee-newer' or catalog['dialect']['effective'] != 'mee-newer':
        raise FrontendError('Genesis policy requires observed current-MEE script evidence')
    if any(len(catalog[k]) != count for k, count in EXPECTED_COUNTS.items()):
        raise FrontendError('Genesis catalog differs from pinned audit structure')
    if (type(slot) is not int or slot not in range(8)
            or set(selection) != {'area', 'mode', 'time_of_day', 'licenses', 'weapons', 'equipment'}
            or selection['mode'] != 'observer' or selection['licenses']
            or selection['weapons'] or selection['equipment']
            or type(selection['time_of_day']) is not int or selection['time_of_day'] not in (0, 1, 2)):
        raise FrontendError('Genesis v1 permits only observer, no loadout, native slots 0..7 and dawn/day/night')
    if catalog['score_modifier_observations']:
        raise FrontendError('accessory overrides differ from pinned Genesis evidence')
    if any(d['code'] in ('unclosed-block', 'unmatched-brace', 'dialect-conflict',
                         'explicit-areas-uninterpreted') for d in catalog['diagnostics']):
        raise FrontendError('Genesis catalog ambiguity requires review')
    area = next((a for a in catalog['areas'] if a['id'] == selection['area']), None)
    if (not area or not isinstance(area['launch_stem'], str)
            or not re.fullmatch(r'area[1-8]|external', area['launch_stem'])):
        raise FrontendError('Genesis observer requires a single evidenced area resource pair')
    cost = area['price']
    if type(cost) is not int or cost < 0 or type(score) is not int or score < cost:
        raise FrontendError('Genesis observer does not meet the observed area score requirement')
    # Both pinned scripts have no accessories {} override. These six defaults
    # are evidenced in Menu/Resources.cpp and Hunt/Game/EngineInit.cpp at the
    # reviewed base, not inferred from positional accessory labels.
    argv = [f'reg={slot}', 'prj=huntdat/areas/' + area['launch_stem'],
            'din=0', 'wep=0', f"dtm={selection['time_of_day']}", '-observ',
            'smod=0.85,0.70,0.80,1.0,1.25,1.0']
    return {'adapter': POLICY_ID, 'revision': dict(GENESIS_REVISION),
            'selection': selection, 'native_slot': slot, 'candidate_argv': argv,
            'score_requirement': cost, 'score_mutation': 'none', 'rank_policy': 'unverified',
            'equipment_policy': 'all-disabled', 'mask_policy': 'observer-zero-only',
            'process_launch_allowed': False,
            'capabilities': {'genesis_policy': 'structurally-validated',
                'modern_engine_compatibility': 'unknown', 'engine_process_executed': False,
                'observer_session_launched': False, 'hunt_save_round_trip_validated': False},
            'diagnostics': [{'code': 'engine-session-seam-required'},
                            {'code': 'engine-build-not-certified'},
                            {'code': 'progression-semantics-unverified'}]}


def plan_observer(store, association_id, area_id, time_of_day=1, probe=None, engine=None):
    selection = {'area': area_id, 'mode': 'observer', 'time_of_day': time_of_day,
                 'licenses': [], 'weapons': [], 'equipment': []}
    with store.lock():
        pins, _ = snapshot_pins(store, association_id, selection, probe)
        instance = pins['instance']
        catalog = project(instance['path'], instance['dialect_hint'])
        save = pins['source_observation'][f"trophy{pins['native_slot']:02d}.sav"]
        policy = observer_policy(pins['revision'], catalog, pins['native_slot'], selection, save['score'])
        pins['adapter'] = POLICY_ID
        return {'kind': 'genesis-observer-blocked-plan', 'id': new_id(), 'created_at': now(),
                'pins': pins, **policy,
                'selected_engine': executable_evidence(engine) if engine else None,
                'engine_evidence_status': 'hash-only-not-build-certification',
                'cwd': None, 'executable': None}
