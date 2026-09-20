"""Normal-hunt selection policy for the one pinned Genesis revision.

Catalog licenses are ordered selections, possibly groups, never species/AI IDs.
This module does not mutate native score/rank/options or authorize a binary.
"""
import copy
import re

from .genesis import GENESIS_REVISION, EXPECTED_COUNTS
from .store import FrontendError

POLICY_ID = 'genesis-current-mee-hunt-v1'
SCORE_MODIFIERS = 'smod=0.85,0.70,0.80,1.0,1.25,1.0'


def hunt_policy(revision, catalog, slot, selection, score):
    if revision != GENESIS_REVISION:
        raise FrontendError('Genesis hunt refuses an unpinned content revision')
    if catalog['dialect']['observed'] != 'mee-newer' or catalog['dialect']['effective'] != 'mee-newer':
        raise FrontendError('Genesis hunt requires observed current-MEE script evidence')
    if any(len(catalog[k]) != count for k, count in EXPECTED_COUNTS.items()):
        raise FrontendError('Genesis hunt catalog differs from pinned audit structure')
    if (type(slot) is not int or slot not in range(8)
            or not isinstance(selection, dict)
            or set(selection) != {'area', 'mode', 'time_of_day', 'licenses', 'weapons', 'equipment'}
            or selection['mode'] != 'hunt' or type(selection['area']) is not str
            or type(selection['time_of_day']) is not int or selection['time_of_day'] not in (0, 1, 2)
            or type(selection['equipment']) is not list or selection['equipment']):
        raise FrontendError('hunt v1 requires one area, dawn/day/night, slot 0..7 and no equipment or flags')
    for group in ('licenses', 'weapons'):
        ids = selection[group]
        if type(ids) is not list or len(ids) != 1 or type(ids[0]) is not str:
            raise FrontendError('hunt v1 requires exactly one catalog license and one weapon')
    if catalog['score_modifier_observations'] or any(d['code'] in (
            'unclosed-block', 'unmatched-brace', 'dialect-conflict',
            'explicit-areas-uninterpreted', 'surplus-price') for d in catalog['diagnostics']):
        raise FrontendError('ambiguous Genesis catalog/price/modifier policy requires review')
    chosen = {}
    for group in ('areas', 'licenses', 'weapons'):
        entries = catalog[group]
        # A fresh projection must retain canonical contiguous positional identities.
        for i, entry in enumerate(entries):
            if (not isinstance(entry, dict) or entry.get('id') != f'{group}:{i}'
                    or type(entry.get('ordinal')) is not int or entry['ordinal'] != i):
                raise FrontendError('ambiguous or stale catalog ordering')
        identity = selection['area'] if group == 'areas' else selection[group][0]
        matches = [e for e in entries if e['id'] == identity]
        if len(matches) != 1:
            raise FrontendError('unknown or ambiguous catalog selection')
        chosen[group] = matches[0]
    area, license, weapon = (chosen[k] for k in ('areas', 'licenses', 'weapons'))
    if (not isinstance(area.get('launch_stem'), str)
            or not re.fullmatch(r'area[1-8]|external', area['launch_stem'])
            or (area['launch_stem'] != f"area{area['ordinal'] + 1}"
                and not (area['ordinal'] == 5 and area['launch_stem'] == 'external'))):
        raise FrontendError('selected area has no unambiguous supported resource pair')
    for entry in (license, weapon):
        if not isinstance(entry.get('label'), str) or not entry['label'].strip():
            raise FrontendError('selected catalog entry has an unresolved label')
    if type(license.get('ai')) is not int or license['ai'] < 10:
        raise FrontendError('selected catalog entry is not a huntable license')
    prices = [e.get('price') for e in chosen.values()]
    if any(type(p) is not int or not 0 <= p <= 2147483647 for p in prices):
        raise FrontendError('unresolved or out-of-range listed price')
    cost = sum(prices)
    if type(score) is not int or not 0 <= score <= 2147483647 or cost > score:
        raise FrontendError('selection exceeds the native score requirement')
    # Menu/Menu.cpp emits unshifted positional bits. ProcessCommandLine applies
    # the *only* 1024 multiplication; ScriptParser charN predicates use N+10.
    din, wep = 1 << license['ordinal'], 1 << weapon['ordinal']
    return {'adapter': POLICY_ID, 'revision': dict(GENESIS_REVISION),
            'selection': copy.deepcopy(selection), 'native_slot': slot,
            'candidate_argv': [f'reg={slot}', 'prj=huntdat/areas/' + area['launch_stem'],
                f'din={din}', f'wep={wep}', f"dtm={selection['time_of_day']}", SCORE_MODIFIERS],
            'license_mask': din, 'weapon_mask': wep, 'score_requirement': cost,
            'score_mutation': 'none', 'rank_policy': 'native-observation-no-rank-gate',
            'equipment_policy': 'all-disabled', 'mask_policy': 'single-ordinal-bit',
            'process_launch_allowed': False,
            'diagnostics': [{'code': 'experimental-native-validation-required'},
                            {'code': 'native-menu-rank-disagreement-preserved'}]}
