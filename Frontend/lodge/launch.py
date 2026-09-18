"""Revision-bound launch intent. No game execution or progression writes."""
from .catalog import project
from .discovery import diagnostic, get_instance, inspect_instance
from .profiles import refresh_association
from .store import FrontendError, new_id, now


def prepare(store, data, association_id, area_id, licenses=(), weapons=(), equipment=(), mode='hunt', time_of_day=1, probe=None):
    if association_id not in data['associations']:
        raise FrontendError('unknown association ID')
    association = data['associations'][association_id]
    instance = get_instance(data, association['instance_id'])
    observation = inspect_instance(instance)
    diagnostics = list(observation['diagnostics'])
    capabilities = {'installation_recognized': 'yes' if observation['recognized'] else 'no',
                    'native_profile_associated': 'yes', 'native_save_format_readable': 'unknown',
                    'content_dialect_recognized': 'unknown', 'console_can_be_generated': 'unknown',
                    'modern_engine_compatibility': 'unknown', 'launch_tested': 'unknown',
                    'hunt_save_round_trip_validated': 'unknown', 'trophy_interpretation_validated': 'unknown',
                    'legacy_windows_fallback_available': 'candidate-files-only' if any(p.lower().endswith('.exe') for p in observation.get('executables', [])) else 'unknown'}
    request = {'schema_version': 1, 'id': new_id(), 'created_at': now(), 'kind': 'hunt-launch-dry-run',
               'hunter_id': association['hunter_id'], 'instance_id': instance['id'],
               'revision': instance['revision'], 'association_id': association_id,
               'state_authority': association['authority'], 'native_slot': association['filename_slot'],
               'selection': {'area': area_id, 'licenses': list(licenses), 'weapons': list(weapons),
                             'equipment': list(equipment), 'mode': mode, 'time_of_day': time_of_day},
               'host_settings': data['host_settings'], 'capabilities': capabilities,
               'diagnostics': diagnostics, 'process_launch_allowed': False, 'executable': None,
               'cwd': instance['path'], 'candidate_argv': [], 'selection_status': 'blocked',
               'result': 'dry-run-only'}
    if not observation['recognized']:
        return request
    catalog = project(instance['path'], instance['dialect_hint'])
    capabilities.update(catalog['capabilities'])
    # Broad parsing warnings remain visible even when not selection blockers.
    request['catalog_diagnostics'] = catalog['diagnostics']
    request['catalog_source'] = {'path': catalog['source'], 'sha256': catalog['source_sha256']}
    state = refresh_association(store, data, association_id, probe)
    request['state_observation'] = state
    saves = [f for f in state.get('files', []) if f['kind'] == 'sav']
    decoded = saves[0]['decoded'] if len(saves) == 1 else {}
    capabilities['native_save_format_readable'] = 'layout-candidate' if decoded.get('codec_roundtrip_exact') else 'unknown'
    if observation.get('revision_changed') or association['revision'] != instance['revision']:
        diagnostics.append(diagnostic('revision-review-required', 'Content no longer matches association provenance.'))
    if state['status'] != 'unchanged-state':
        diagnostics.append(diagnostic('native-state-review-required', 'Authoritative state missing or changed since association.'))
    if data['hunters'][association['hunter_id']].get('archived_at'):
        diagnostics.append(diagnostic('archived-hunter', 'Archived hunter cannot prepare a hunt.'))
    if association['origin'] != 'personal':
        diagnostics.append(diagnostic('unclaimed-personal-progression', 'Packaged or unknown source is not certified personal progression.'))
    if not decoded.get('codec_roundtrip_exact'):
        diagnostics.append(diagnostic('save-format-unresolved', 'No supported codec observation for the associated save.'))
    if any(d['code'] in ('registration-mismatch', 'slot-outside-menu', 'noncanonical-slot-name', 'non-root-state', 'unreadable-layout') for d in state.get('diagnostics', [])):
        diagnostics.append(diagnostic('native-state-ineligible', 'Native slot or state layout requires reconciliation.'))
    if catalog['dialect']['effective'] not in ('mee-newer', 'c2-classic'):
        diagnostics.append(diagnostic('dialect-launch-unresolved', 'This dialect has no candidate modern launch adapter.'))
    if any(d['code'] in ('unclosed-block', 'unmatched-brace', 'dialect-conflict') for d in catalog['diagnostics']):
        diagnostics.append(diagnostic('catalog-parse-review-required', 'Script ambiguity prevents validated selection.'))
    if mode not in ('hunt', 'observer') or type(time_of_day) is not int or time_of_day not in (0, 1, 2):
        diagnostics.append(diagnostic('unsupported-mode', 'Only normal/observer planning and dawn/day/night values are modeled.'))
    if mode == 'hunt' and (not licenses or not weapons):
        diagnostics.append(diagnostic('empty-hunt-selection', 'Normal hunt requires at least one license and weapon.'))
    chosen = {}
    for group, ids in (('areas', [area_id]), ('licenses', licenses), ('weapons', weapons), ('equipment', equipment)):
        known = {entry['id']: entry for entry in catalog[group]}
        chosen[group] = []
        if len(set(ids)) != len(ids):
            diagnostics.append(diagnostic('duplicate-selection', 'Selection repeats an entry ID.', category=group))
        for identity in ids:
            if identity not in known:
                diagnostics.append(diagnostic('unknown-selection', 'Entry is not in this revision catalog.', entry_id=identity))
            else:
                chosen[group].append(known[identity])
    if equipment:
        diagnostics.append(diagnostic('equipment-policy-unresolved', 'Equipment meanings/flags require a dialect-specific policy.'))
    for group in ('licenses', 'weapons'):
        if any(e['ordinal'] >= 10 for e in chosen[group]):
            diagnostics.append(diagnostic('selection-mask-limit', 'Prototype candidate adapter is bounded to the evidenced first ten entries.', category=group))
    if any(d['code'] == 'unusual-label' and d['entry_id'] in licenses for d in catalog['diagnostics']):
        diagnostics.append(diagnostic('license-meaning-unresolved', 'Instruction-like or blank entry requires edition-specific review.'))
    area = chosen['areas'][0] if chosen['areas'] else None
    if area is None or area['launch_stem'] is None:
        diagnostics.append(diagnostic('area-unlaunchable', 'Advertised area has no unambiguous resource pair.'))
    selected = [e for entries in chosen.values() for e in entries]
    known_cost = all(type(e['price']) is int and e['price'] >= 0 for e in selected)
    cost = sum(e['price'] for e in selected) if known_cost else None
    score = decoded.get('score')
    request['affordability'] = {'native_score': score, 'listed_selection_requirement': cost,
                                'meets_listed_requirement': cost <= score if cost is not None and score is not None else None,
                                'progression_mutation': 'none', 'rank_policy': 'unresolved',
                                'score_modifiers': catalog['score_modifier_observations']}
    if cost is None:
        diagnostics.append(diagnostic('price-policy-unresolved', 'Missing or unusual price; no guessed defaults.'))
    elif score is not None and cost > score:
        diagnostics.append(diagnostic('insufficient-listed-score', 'Selection exceeds the observed native score.'))
    if not diagnostics:
        request['selection_status'] = 'structurally-valid-policy-unverified'
        request['candidate_argv'] = [f"reg={association['filename_slot']}", f"prj=huntdat/areas/{area['launch_stem']}",
                                     'din=' + str(sum(1 << e['ordinal'] for e in chosen['licenses'])),
                                     'wep=' + str(sum(1 << e['ordinal'] for e in chosen['weapons'])), f'dtm={time_of_day}']
        if mode == 'observer':
            request['candidate_argv'].append('-observ')
    # Always explicit, even when the candidate intent is structurally valid.
    diagnostics.append(diagnostic('engine-state-root-seam-required', 'Execution disabled: no isolated writable state-root and session ownership/return transaction adapter.'))
    diagnostics.append(diagnostic('progression-policy-unverified', 'No rank/unlock/settlement equivalence has been certified.'))
    return request


def simulated_return(store, data, association_id, exit_code=0, probe=None):
    association = data['associations'].get(association_id)
    if association is None:
        raise FrontendError('unknown association ID')
    instance = get_instance(data, association['instance_id'])
    return {'kind': 'simulated-return', 'process_executed': False, 'exit_code': exit_code,
            'result': 'simulated-success' if exit_code == 0 else 'simulated-failure',
            'installation': inspect_instance(instance),
            'state': refresh_association(store, data, association_id, probe),
            'launch_tested': 'unknown', 'hunt_save_round_trip_validated': 'unknown',
            'presentation_action': 'resume-and-refresh'}
