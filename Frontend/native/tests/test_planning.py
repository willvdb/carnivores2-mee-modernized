#!/usr/bin/env python3
"""Genesis policy and launch dry-run oracles against the unchanged lodge modules.

Every catalog is projected from an authored temporary tree; revisions, slots,
selections, scores and refreshed states are supplied values. No game assets,
installs, default lodge or profiles are used.
"""
import copy
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import catalog, genesis, genesis_hunt, launch, profiles
from lodge.discovery import register
from lodge.genesis import GENESIS_REVISION
from lodge.profiles import associate
from lodge.store import FrontendError, Store, hunter, validate

DRIVER, PROBE = map(str, map(Path, sys.argv[1:3]))
ENV = dict(os.environ)
ENV.pop('C2_PROFILE_PROBE', None)
os.environ.pop('C2_PROFILE_PROBE', None)
CASES = 0
ID = str(uuid.UUID(int=7))
CREATED = '2026-09-21T00:00:00+00:00'


class Driver:
    def __init__(self):
        self.process = None
        self.start()

    def start(self):
        self.process = subprocess.Popen([DRIVER], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=ENV)

    def __call__(self, request):
        self.process.stdin.write((json.dumps(request) + '\n').encode())
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            code = self.process.wait(timeout=60)
            error = self.process.stderr.read().decode(errors='replace')
            self.start()
            raise AssertionError(f'driver exited {code}: {error}\n{request}')
        return json.loads(line)

    def close(self):
        self.process.stdin.close()
        assert self.process.wait(timeout=120) == 0, self.process.stderr.read()


NATIVE = Driver()


def outcome(function):
    try:
        return function()
    except FrontendError as e:
        return ('frontend', str(e))
    except (TypeError, AttributeError):
        return ('type', None)
    except Exception:
        return ('unexpected', None)


def compare(request, expected):
    global CASES
    actual = NATIVE(request)
    if isinstance(expected, tuple):
        kind, message = expected
        assert not actual['ok'], (request, expected, actual)
        if kind == 'frontend':
            assert (actual['kind'], actual['error']) == (kind, message), (request, expected, actual)
        elif kind == 'type':
            assert actual['kind'] == 'type', (request, expected, actual)
    else:
        expected_json = json.dumps(expected, indent=2, ensure_ascii=True, allow_nan=False) + '\n'
        assert actual['ok'], (request, expected_json, actual)
        assert actual['json'] == expected_json, (request, expected_json, actual)
        assert json.dumps(actual['value'], ensure_ascii=True, separators=(',', ':')) == json.dumps(expected, ensure_ascii=True, separators=(',', ':')), (request, expected, actual)
    CASES += 1
    return actual


def write(root, relative, data=b'x'):
    p = root / relative
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p


# --- Genesis policies -------------------------------------------------------

MEE_NEWER = 'spawntable {\n}\n'
OBSERVER = {'area': 'areas:0', 'mode': 'observer', 'time_of_day': 1, 'licenses': [], 'weapons': [], 'equipment': []}
HUNT = {'area': 'areas:0', 'licenses': ['licenses:0'], 'weapons': ['weapons:0'], 'equipment': [], 'mode': 'hunt', 'time_of_day': 1}


def script(licenses=9, weapons=8, header=MEE_NEWER, labels=None, ais=None, weapon_labels=None,
           area_prices=None, dino_prices=None, weapon_prices=None, acces_prices=None, accessories=False, tail=''):
    labels = labels or {}
    weapon_labels = weapon_labels or {}
    ais = ais or {}
    lines = [header, 'characters {']
    for i in range(licenses):
        lines.append('{')
        label = labels.get(i, f"'Dino {i}'")
        if label is not None:
            lines.append(f' name = {label}')
        lines.append(f' ai = {ais.get(i, 10 + i)}')
        lines.append('}')
    lines += ['}', 'weapons {']
    for i in range(weapons):
        label = weapon_labels.get(i, f"'Weapon {i}'")
        lines += ['{'] + ([f' name = {label}'] if label is not None else []) + ['}']
    lines += ['}', 'prices {']
    for key, prices in (('area', area_prices or [5] * 8), ('dino', dino_prices or [10] * 9),
                        ('weapon', weapon_prices or [15] * 8), ('acces', acces_prices or [20] * 4)):
        lines += [f' {key} = {p}' for p in prices]
    lines.append('}')
    if accessories:
        lines.append('accessories {\n x = 1\n}')
    lines.append(tail)
    return '\n'.join(lines).encode('latin1')


def genesis_root(base, name, text=None, pairs=None, extra_maps=('spare.map',), **kw):
    root = base / name
    for part in ('HUNTDAT/MENU/TXT', 'HUNTDAT/MENU/PICS', 'HUNTDAT/AREAS'):
        (root / part).mkdir(parents=True)
    write(root, 'HUNTDAT/_RES.TXT', text if text is not None else script(**kw))
    if pairs is None:
        pairs = {slot: ['external' if slot == 6 else f'area{slot}'] for slot in range(1, 9)}
    for stems in pairs.values():
        for stem in stems:
            write(root, f'HUNTDAT/AREAS/{stem}.MAP')
            write(root, f'HUNTDAT/AREAS/{stem}.RSC')
    for name in extra_maps:
        write(root, 'HUNTDAT/AREAS/' + name)
    return root


def typed_shape(selection, hunt):
    keys = ['area', 'licenses', 'weapons', 'equipment', 'mode', 'time_of_day'] if hunt else ['area', 'mode', 'time_of_day', 'licenses', 'weapons', 'equipment']
    if not isinstance(selection, dict) or list(selection) != keys or type(selection['time_of_day']) is not int or type(selection['area']) is not str:
        return False
    if selection['mode'] != ('hunt' if hunt else 'observer'):
        return False
    lists = ('licenses', 'weapons') if hunt else ()
    for key in lists:
        if not isinstance(selection[key], list) or not all(type(x) is str for x in selection[key]):
            return False
    return all(selection[key] == [] for key in ('licenses', 'weapons', 'equipment') if key not in lists)


def policy(op, root, revision=GENESIS_REVISION, slot=0, selection=None, score=100, hint=None, store=None):
    hunt = op == 'hunt'
    if selection is None:
        selection = HUNT if hunt else OBSERVER
    request = {'op': op, 'root': str(root), 'revision': revision, 'slot': slot, 'selection': selection, 'score': score,
               'typed': typed_shape(selection, hunt)}
    if hint is not None:
        request['dialect_hint'] = hint
    if store is not None:
        request['store'], request['identity'] = str(store[0].directory), store[1]
    reference = genesis_hunt.hunt_policy if hunt else genesis.observer_policy
    expected = outcome(lambda: reference(request['revision'], catalog.project(request['root'], request.get('dialect_hint', 'unknown')),
                                         request['slot'], copy.deepcopy(request['selection']), request['score']))
    return compare(request, expected)


def pinned_store(base, root, name='pinned-store', current=None):
    store = Store(base / name)
    with store.transaction() as data:
        identity = register(data, root)['id']
    data = json.loads(store.path.read_text(encoding='utf-8'))
    current = current or dict(GENESIS_REVISION)
    data['instances'][identity]['revision'] = current
    data['instances'][identity]['revisions'] = [dict(GENESIS_REVISION), {**GENESIS_REVISION, 'file_count': 343}, current]
    validate(data)
    store.path.write_text(json.dumps(data, indent=2), encoding='utf-8')
    return store, identity


def genesis_cases(base):
    root = genesis_root(base, 'genesis')
    store = pinned_store(base, root)
    near_miss = {**GENESIS_REVISION, 'byte_count': 768073100, 'extra': {'nested': [1]}}
    stale = pinned_store(base, root, 'stale-store', near_miss)
    for op in ('observer', 'hunt'):
        result = policy(op, root)['value']
        assert result['candidate_argv'][:2] == ['reg=0', 'prj=huntdat/areas/area1'] and not result['process_launch_allowed']
        assert policy(op, root, store=store)['ok']
        assert not policy(op, root, revision=near_miss, store=stale)['ok']
        # Revision equality is Python dict equality: numeric kinds and key sets.
        for revision in [{**GENESIS_REVISION, 'sha256': '0' * 64}, {**GENESIS_REVISION, 'sha256': GENESIS_REVISION['sha256'].upper()},
                         {**GENESIS_REVISION, 'file_count': 343}, {**GENESIS_REVISION, 'file_count': 345},
                         {**GENESIS_REVISION, 'byte_count': 768073100}, {**GENESIS_REVISION, 'byte_count': 0},
                         {**GENESIS_REVISION, 'algorithm': 'huntdat-sha256-v2'}, {**GENESIS_REVISION, 'extra': 1},
                         {k: v for k, v in GENESIS_REVISION.items() if k != 'byte_count'}, dict(reversed(list(GENESIS_REVISION.items()))),
                         {**GENESIS_REVISION, 'file_count': 344.0}, {**GENESIS_REVISION, 'byte_count': 768073101.0},
                         {**GENESIS_REVISION, 'byte_count': 7.68073101e8}, {**GENESIS_REVISION, 'file_count': True},
                         {**GENESIS_REVISION, 'file_count': '344'}, {**GENESIS_REVISION, 'file_count': 344.5},
                         {**GENESIS_REVISION, 'sha256': None}, None, [], 'x', 5, True, list(GENESIS_REVISION.items()), {}]:
            policy(op, root, revision=revision)
        # Dialect hints and script families.
        for hint in ('mee-older', 'c2-classic', 'mee-newer', 'iceage-triassic', 'unknown', ''):
            policy(op, root, hint=hint)
        for i, header in enumerate(['', 'hunterinfo {\n}\n', 'hunterinfo {\n}\nspawntable {\n}\n', 'packtable {\n}\n', 'TROPHYTABLE {}\n']):
            policy(op, genesis_root(base, f'{op}-header-{i}', header=header))
        # Pinned counts, each one off in both directions.
        for i, kw in enumerate([dict(licenses=8), dict(licenses=10), dict(weapons=7), dict(weapons=9),
                                dict(area_prices=[5] * 7), dict(area_prices=[5] * 9), dict(acces_prices=[20] * 3), dict(acces_prices=[20] * 5),
                                dict(extra_maps=()), dict(extra_maps=('spare.map', 'other.MAP')), dict(dino_prices=[10] * 8), dict(weapon_prices=[15] * 9)]):
            policy(op, genesis_root(base, f'{op}-count-{i}', **kw))
        # Modifier overrides and ambiguity diagnostics; surplus prices bind only hunts.
        for i, kw in enumerate([dict(accessories=True), dict(tail='weapons {'), dict(tail='}'), dict(tail='areas {\n x = 1\n}'),
                                dict(weapon_prices=[15] * 9, weapons=8), dict(dino_prices=[10] * 10)]):
            policy(op, genesis_root(base, f'{op}-ambiguity-{i}', **kw))
        # Slots and scores.
        for slot in (-1, 0, 1, 7, 8, True, False, 0.0, '0', None, 10 ** 30, -(10 ** 30), [0]):
            policy(op, root, slot=slot)
        for score in (-1, 0, 4, 5, 29, 30, 31, 100, 2147483647, 2147483648, True, False, 30.0, 100.0, '100', None, 10 ** 30, [], {}):
            policy(op, root, score=score)
        # Every area ordinal, the slot-six external candidate and unresolved pairs.
        for ordinal in range(8):
            selection = {**(HUNT if op == 'hunt' else OBSERVER), 'area': f'areas:{ordinal}'}
            for time in (0, 1, 2):
                policy(op, root, selection={**selection, 'time_of_day': time}, score=1000)
        area6 = genesis_root(base, f'{op}-area6', pairs={**{s: [f'area{s}'] for s in range(1, 9)}})
        both = genesis_root(base, f'{op}-both', pairs={**{s: [f'area{s}'] for s in range(1, 9)}, 6: ['area6', 'external']}, extra_maps=())
        missing = genesis_root(base, f'{op}-missing', pairs={**{s: [f'area{s}'] for s in range(2, 9)}, 6: ['external']}, extra_maps=('spare.map', 'area1.MAP'))
        for tree in (area6, both, missing):
            for ordinal in (0, 5):
                policy(op, tree, selection={**(HUNT if op == 'hunt' else OBSERVER), 'area': f'areas:{ordinal}'})
    # Observer selection shape and truthiness checks in reference order.
    for field, value in [('mode', 'hunt'), ('mode', 'survival'), ('mode', None), ('licenses', ['licenses:0']), ('weapons', ['weapons:0']),
                         ('equipment', ['equipment:0']), ('licenses', None), ('licenses', 0), ('licenses', {}), ('licenses', ''), ('licenses', 'x'),
                         ('licenses', 1), ('weapons', False), ('weapons', 0.0), ('equipment', {'a': 1}), ('time_of_day', 3), ('time_of_day', -1),
                         ('time_of_day', True), ('time_of_day', 0.0), ('time_of_day', '1'), ('time_of_day', None), ('area', None), ('area', 5),
                         ('area', 'areas:8'), ('area', ['areas:0']), ('area', 'AREAS:0'), ('argv', ['-debug']), ('flags', [])]:
        policy('observer', root, selection={**OBSERVER, field: value})
    for selection in [{k: v for k, v in OBSERVER.items() if k != 'equipment'}, list(OBSERVER), list(OBSERVER) + ['area'], list(OBSERVER)[:-1] + [1],
                      [[]] + list(OBSERVER), list(OBSERVER) + [[]], 'area', None, 5, True, {}, []]:
        policy('observer', root, selection=selection)
        policy('observer', root, selection=selection, slot=9)  # slot failure precedes iteration
    policy('observer', root, selection=dict(reversed(list(OBSERVER.items()))))
    # Hunt selection shape checks.
    for field, value in [('area', None), ('area', 'areas:8'), ('area', ['areas:0']), ('area', 5), ('licenses', []), ('licenses', ['licenses:0'] * 2),
                         ('licenses', ['licenses:9']), ('licenses', [True]), ('licenses', 'licenses:0'), ('licenses', None), ('licenses', [None]),
                         ('weapons', ['weapons:0', 'weapons:7']), ('weapons', ['weapons:8']), ('weapons', {'weapons:0': 1}), ('equipment', ['equipment:0']),
                         ('equipment', False), ('equipment', None), ('equipment', {}), ('equipment', ''), ('mode', 'observer'), ('mode', 'survival'),
                         ('mode', None), ('time_of_day', True), ('time_of_day', 3), ('time_of_day', '1'), ('time_of_day', 1.0), ('argv', ['-debug']), ('flags', [])]:
        policy('hunt', root, selection={**HUNT, field: value})
    for selection in [{k: v for k, v in HUNT.items() if k != 'equipment'}, list(HUNT), None, 'hunt', 5, {}, []]:
        policy('hunt', root, selection=selection)
    policy('hunt', root, selection=dict(reversed(list(HUNT.items()))))
    # Every license/weapon ordinal bit and the summed requirement.
    for license in range(9):
        for weapon in range(8):
            result = policy('hunt', root, selection={**HUNT, 'licenses': [f'licenses:{license}'], 'weapons': [f'weapons:{weapon}'],
                                                     'time_of_day': (license + weapon) % 3})['value']
            assert result['license_mask'] == 1 << license and result['weapon_mask'] == 1 << weapon and result['score_requirement'] == 30
    # Labels: blank, whitespace (including Latin-1 NBSP), integer and absent.
    for i, label in enumerate(["''", "' \t'", "'\xa0'", "'\xa0x'", '7', None, "'Check this'", "' 0 '"]):
        policy('hunt', genesis_root(base, f'hunt-label-{i}', labels={0: label}))
        policy('hunt', genesis_root(base, f'hunt-weapon-label-{i}', weapon_labels={0: label}))
    # Prices: unresolved, negative, the int32 boundary and arbitrary magnitude.
    big = 10 ** 30
    for i, (kw, scores) in enumerate([(dict(dino_prices=['abc'] + [10] * 8), (100,)), (dict(dino_prices=[-1] + [10] * 8), (100,)),
                                      (dict(weapon_prices=[0] * 8, dino_prices=[0] * 9, area_prices=[2147483647] + [5] * 7), (2147483646, 2147483647, 2147483648)),
                                      (dict(area_prices=[2147483648] + [5] * 7), (2147483648, big)), (dict(area_prices=[big] + [5] * 7), (big - 1, big, big + 1)),
                                      (dict(area_prices=[0] + [5] * 7, dino_prices=[0] * 9, weapon_prices=[0] * 8), (0, -1, 1)),
                                      (dict(dino_prices=[1000] + [10] * 8), (1019, 1020, 999)), (dict(area_prices=["'5'"] + [5] * 7), (100,)),
                                      (dict(area_prices=['-0'] + [5] * 7, dino_prices=['+0'] * 9, weapon_prices=['000'] * 8), (0, 1))]):
        tree = genesis_root(base, f'prices-{i}', **kw)
        for score in scores:
            policy('observer', tree, score=score)
            policy('hunt', tree, score=score)
    # Observer with a hunt-shaped selection and vice versa.
    policy('observer', root, selection=HUNT)
    policy('hunt', root, selection=OBSERVER)


# --- Launch dry-run ---------------------------------------------------------

def save_bytes(slot=0, score=100):
    result = bytearray((i * 73 + 19) % 256 for i in range(1660))
    result[:128] = b'Test hunter\0' + bytes(range(116))
    struct.pack_into('<iii', result, 128, slot, score, 1000)
    return bytes(result)


def room_bytes():
    return bytes((i * 37) % 256 for i in range(7176))


CLASSIC = b"""weapons {
{
 name = 'Synthetic weapon'
}
{
 name = 'Second weapon'
}
}
characters {
{
 name = 'Synthetic group'
 ai = 10
}
{
 name = 'Second group'
 ai = 11
}
}
prices {
 area = 5
 area = 6
 dino = 10
 dino = 11
 weapon = 20
 weapon = 21
 acces = 30
}
"""


def wide_script(licenses=12, weapons=11):
    lines = ['characters {']
    for i in range(licenses):
        lines += ['{', f" name = {repr('Dino ' + str(i)) if i != 3 else repr('')}", f' ai = {10 + i}', '}']
    lines += ['}', 'weapons {']
    for i in range(weapons):
        lines += ['{', f" name = 'Weapon {i}'", '}']
    lines += ['}', 'prices {', ' area = 5', ' area = 6'] + [' dino = 1'] * licenses + [' weapon = 2'] * weapons + [' acces = 3'] * 11 + ['}']
    return '\n'.join(lines).encode()


def launch_store(base, name, res=CLASSIC, menu=None, dialect='c2-classic', origin='personal', engine='CARN2.EXE', score=100, slot=0, hint_pairs=(('area1',),)):
    root = base / name / 'Game'
    for part in ('HUNTDAT/MENU/TXT', 'HUNTDAT/MENU/PICS', 'HUNTDAT/AREAS'):
        (root / part).mkdir(parents=True)
    write(root, 'HUNTDAT/_RES.TXT', res)
    if menu is not None:
        write(root, 'HUNTDAT/_MENU.TXT', menu)
    for stems in hint_pairs:
        for stem in stems:
            write(root, f'HUNTDAT/AREAS/{stem}.MAP', b'synthetic map evidence')
            write(root, f'HUNTDAT/AREAS/{stem}.RSC', b'synthetic resource evidence')
    if engine:
        write(root, engine, b'synthetic executable evidence - never run')
    write(root, f'trophy{slot:02d}.sav', save_bytes(slot, score))
    write(root, f'trophy{slot:02d}.sab', room_bytes())
    store = Store(base / name / 'Lodge')
    with store.transaction() as data:
        hunter_id = hunter(data, 'create', name='Hunter')['id']
        instance = register(data, root, dialect=dialect)
        association = associate(store, data, hunter_id, instance['id'], f'trophy{slot:02d}', origin, probe=PROBE)['id']
    return {'root': root, 'store': store, 'hunter': hunter_id, 'instance': instance['id'], 'association': association}


def edit_manifest(store, mutate):
    data = json.loads(store.path.read_text(encoding='utf-8'))
    mutate(data)
    validate(data)
    store.path.write_text(json.dumps(data, indent=2, ensure_ascii=True), encoding='utf-8')


def dry_run(fixture, area='areas:0', licenses=(), weapons=(), equipment=(), mode='hunt', time_of_day=1, association=None, state=None):
    store = fixture['store']
    association = association or fixture['association']
    arguments = {'area': area, 'licenses': list(licenses) if isinstance(licenses, tuple) else licenses,
                 'weapons': list(weapons) if isinstance(weapons, tuple) else weapons,
                 'equipment': list(equipment) if isinstance(equipment, tuple) else equipment, 'mode': mode, 'time_of_day': time_of_day}
    typed = all(isinstance(arguments[k], list) and all(type(x) is str for x in arguments[k]) for k in ('licenses', 'weapons', 'equipment'))
    typed = typed and type(area) is str and type(mode) is str and type(time_of_day) is int
    request = {'op': 'launch', 'store': str(store.directory), 'association': association, 'arguments': arguments,
               'id': ID, 'created_at': CREATED, 'typed': typed}
    supplied = state
    if supplied is None:
        try:
            supplied = profiles.refresh_association(store, store.read(), association, PROBE)
        except Exception:
            supplied = None
    request['state'] = supplied

    def reference():
        launch.new_id, launch.now = (lambda: ID), (lambda: CREATED)
        launch.refresh_association = (lambda *a, **k: copy.deepcopy(state)) if state is not None else profiles.refresh_association
        data = store.read()
        return launch.prepare(store, data, association, arguments['area'], arguments['licenses'], arguments['weapons'],
                              arguments['equipment'], arguments['mode'], arguments['time_of_day'], PROBE)
    return compare(request, outcome(reference))


def codes(result):
    return [d['code'] for d in result['value']['diagnostics']]


def launch_cases(base):
    fixture = launch_store(base, 'launch')
    dry_run(fixture, association=str(uuid.UUID(int=99)))
    result = dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'])
    assert result['value']['candidate_argv'] == ['reg=0', 'prj=huntdat/areas/area1', 'din=1', 'wep=1', 'dtm=1']
    assert result['value']['affordability'] == {'native_score': 100, 'listed_selection_requirement': 35, 'meets_listed_requirement': True,
                                                'progression_mutation': 'none', 'rank_policy': 'unresolved', 'score_modifiers': []}
    for time in (0, 1, 2):
        assert dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], time_of_day=time)['value']['candidate_argv'][-1] == f'dtm={time}'
        assert dry_run(fixture, mode='observer', time_of_day=time)['value']['candidate_argv'][-1] == '-observ'
    assert dry_run(fixture, licenses=['licenses:1', 'licenses:0'], weapons=['weapons:1', 'weapons:0'])['value']['candidate_argv'][2:4] == ['din=3', 'wep=3']
    assert 'empty-hunt-selection' in codes(dry_run(fixture))
    assert 'empty-hunt-selection' in codes(dry_run(fixture, licenses=['licenses:0']))
    bad = dry_run(fixture, area='areas:999', licenses=['licenses:0', 'licenses:0'], weapons=['weapons:999'], equipment=['equipment:0', 'equipment:0'])
    for code in ('unknown-selection', 'duplicate-selection', 'equipment-policy-unresolved', 'area-unlaunchable'):
        assert code in codes(bad)
    assert 'area-unlaunchable' in codes(dry_run(fixture, area='areas:1', mode='observer'))
    assert 'equipment-policy-unresolved' in codes(dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], equipment=['equipment:0']))
    for mode, time in (('survival', 1), ('hunt', 3), ('observer', -1), ('hunt', True), ('hunt', 1.0), ('hunt', '1'), ('hunt', None), (None, 1), (5, 1), ('HUNT', 1),
                       ([], 1), ({}, 1), ('hunt', 10 ** 30), ('hunt', -(10 ** 30)), ('hunt', [1])):
        assert 'unsupported-mode' in codes(dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], mode=mode, time_of_day=time))
    # Supplied-kind arguments: strings iterate as characters, dicts as keys, others fail.
    for licenses in ('licenses:0', {'licenses:0': 1}, ['licenses:0', 1], [True], [None], [1.5], 5, None, True):
        dry_run(fixture, licenses=licenses, weapons=['weapons:0'])
    for equipment in ('', 'ab', {}, {'x': 1}, 0, [['equipment:0']], [{'a': 1}]):
        dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], equipment=equipment)
    for area in (None, 5, ['areas:0'], {'areas:0': 1}, 'AREAS:0'):
        dry_run(fixture, area=area, licenses=['licenses:0'], weapons=['weapons:0'])
    # Mask limit, blank labels and multiple bits.
    wide = launch_store(base, 'wide', res=wide_script())
    assert dry_run(wide, licenses=['licenses:10'], weapons=['weapons:10'])['value']['diagnostics'][0]['code'] == 'selection-mask-limit'
    assert codes(dry_run(wide, licenses=['licenses:11', 'licenses:9'], weapons=['weapons:0']))[0] == 'selection-mask-limit'
    result = dry_run(wide, licenses=['licenses:9', 'licenses:0', 'licenses:5'], weapons=['weapons:9', 'weapons:1'])
    assert result['value']['candidate_argv'][2:4] == ['din=545', 'wep=514']
    assert 'license-meaning-unresolved' in codes(dry_run(wide, licenses=['licenses:3'], weapons=['weapons:0']))
    assert 'selection-mask-limit' not in codes(dry_run(wide, licenses=['licenses:0'], weapons=['weapons:0'], equipment=['equipment:10']))
    assert 'license-meaning-unresolved' not in codes(dry_run(wide, licenses=['licenses:0'], weapons=['weapons:3']))
    dry_run(wide, licenses='licenses:3', weapons=['weapons:0'])
    dry_run(wide, licenses={'licenses:3': None}, weapons=['weapons:0'])
    dry_run(wide, licenses=5, weapons=['weapons:0'])
    # Prices and scores.
    priced = launch_store(base, 'priced', res=CLASSIC.replace(b' dino = 10', b' dino = abc').replace(b' area = 6', b' area = -6'))
    assert 'price-policy-unresolved' in codes(dry_run(priced, licenses=['licenses:0'], weapons=['weapons:0']))
    assert 'price-policy-unresolved' in codes(dry_run(priced, area='areas:1', mode='observer'))
    assert 'price-policy-unresolved' not in codes(dry_run(priced, licenses=['licenses:1'], weapons=['weapons:0']))
    poor = launch_store(base, 'poor', score=34)
    assert 'insufficient-listed-score' in codes(dry_run(poor, licenses=['licenses:0'], weapons=['weapons:0']))
    assert 'insufficient-listed-score' not in codes(dry_run(poor, mode='observer'))
    assert 'insufficient-listed-score' in codes(dry_run(poor, licenses=['licenses:0'], weapons=['weapons:0'], area='areas:1'))
    exact = launch_store(base, 'exact', score=35)
    assert dry_run(exact, licenses=['licenses:0'], weapons=['weapons:0'])['value']['affordability']['meets_listed_requirement'] is True
    negative = launch_store(base, 'negative', score=-1)
    assert 'insufficient-listed-score' in codes(dry_run(negative, mode='observer'))
    # Supplied state observations with other decoded kinds and shapes.
    state = profiles.refresh_association(fixture['store'], fixture['store'].read(), fixture['association'], PROBE)
    assert state['status'] == 'unchanged-state'

    save_index = next(i for i, f in enumerate(state['files']) if f['kind'] == 'sav')

    def decoded(**changes):
        patched = copy.deepcopy(state)
        patched['files'][save_index]['decoded'].update(changes)
        return patched
    for score in (100.0, 35.0, 34.5, 35.5, -0.5, True, False, '100', 10 ** 30, -(10 ** 30), None, [], {}, 2 ** 53 + 1, float(2 ** 53), 1e308):
        dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], state=decoded(score=score))
    dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], state=decoded(codec_roundtrip_exact=1))
    dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], state=decoded(codec_roundtrip_exact=0))
    dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], state=decoded(codec_roundtrip_exact=None))
    without = copy.deepcopy(state)
    del without['files'][save_index]['decoded']['score']
    dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], state=without)
    for mutate in [lambda s: s.update(status='changed-state'), lambda s: s.update(status=None), lambda s: s.pop('status'),
                   lambda s: s['files'].append(copy.deepcopy(s['files'][save_index])), lambda s: s['files'].clear(), lambda s: s.pop('files'),
                   lambda s: s['diagnostics'].append({'code': 'slot-outside-menu', 'message': 'x'}),
                   lambda s: s['diagnostics'].append({'code': 'non-root-state'}), lambda s: s.pop('diagnostics'),
                   lambda s: s.update(files='sav'), lambda s: s.update(files=[{'kind': 'sav'}]), lambda s: s.update(files=['sav']),
                   lambda s: s.update(files=[{'kind': 'sav', 'decoded': None}]), lambda s: s.update(files={'kind': 'sav'}),
                   lambda s: s['files'][save_index].update(kind='SAV'), lambda s: s['files'][save_index].update(kind=None), lambda s: s.update(diagnostics=[5])]:
        patched = copy.deepcopy(state)
        mutate(patched)
        dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], state=patched)
    for shape in ({'status': 'missing-state', 'diagnostics': []}, {'status': 'unchanged-state'}, [], None, 'state', 5):
        dry_run(fixture, licenses=['licenses:0'], weapons=['weapons:0'], state=shape)
    # Provenance and hunter diagnostics through the manifest.
    for origin in ('bundled-example', 'unknown'):
        f = launch_store(base, 'origin-' + origin, origin=origin)
        assert 'unclaimed-personal-progression' in codes(dry_run(f, mode='observer'))
    archived = launch_store(base, 'archived')
    with archived['store'].transaction() as data:
        hunter(data, 'archive', archived['hunter'])
    assert 'archived-hunter' in codes(dry_run(archived, mode='observer'))
    for falsy in ('', None, 0, [], False):
        edit_manifest(archived['store'], lambda d: d['hunters'][archived['hunter']].update(archived_at=falsy))
        assert 'archived-hunter' not in codes(dry_run(archived, mode='observer'))
    edit_manifest(archived['store'], lambda d: d['hunters'][archived['hunter']].update(archived_at=1))
    assert 'archived-hunter' in codes(dry_run(archived, mode='observer'))
    drifted = launch_store(base, 'drifted')
    edit_manifest(drifted['store'], lambda d: d['associations'][drifted['association']]['revision'].update(sha256='f' * 64))
    assert 'revision-review-required' in codes(dry_run(drifted, mode='observer'))
    edit_manifest(drifted['store'], lambda d: d['associations'][drifted['association']]['revision'].update(sha256=d['instances'][drifted['instance']]['revision']['sha256'], extra=None))
    assert 'revision-review-required' in codes(dry_run(drifted, mode='observer'))
    settings = launch_store(base, 'settings')
    edit_manifest(settings['store'], lambda d: d.update(host_settings={'display': {'mode': 'borderless'}, 'x': [1, 2.5, None, '\xe9\u2603', 10 ** 40, True, -0.0]}))
    assert dry_run(settings, mode='observer')['value']['host_settings']['x'][4] == 10 ** 40
    # Content and state drift observed on disk.
    changed = launch_store(base, 'changed')
    write(changed['root'], 'HUNTDAT/MENU/TXT/AREA1.TXT', b'Added description')
    assert 'revision-review-required' in codes(dry_run(changed, mode='observer'))
    (changed['root'] / 'trophy00.sav').write_bytes(save_bytes(0, 175))
    result = dry_run(changed, mode='observer')
    assert 'native-state-review-required' in codes(result) and result['value']['affordability']['native_score'] == 175
    (changed['root'] / 'trophy00.sav').unlink()
    result = dry_run(changed, mode='observer')
    assert {'native-state-review-required', 'save-format-unresolved'} <= set(codes(result)) and result['value']['affordability']['native_score'] is None
    ineligible = launch_store(base, 'ineligible')
    write(ineligible['root'], 'trophy00.txt', b'companion')
    assert 'native-state-ineligible' in codes(dry_run(ineligible, mode='observer'))
    (ineligible['root'] / 'trophy00.txt').unlink()
    (ineligible['root'] / 'trophy00.sav').write_bytes(save_bytes(1, 100))
    assert 'native-state-ineligible' in codes(dry_run(ineligible, mode='observer'))
    (ineligible['root'] / 'trophy00.sav').write_bytes(b'short')
    assert {'native-state-ineligible', 'save-format-unresolved'} <= set(codes(dry_run(ineligible, mode='observer')))
    # Dialects and script ambiguity.
    for dialect in ('unknown', 'iceage-triassic', 'mee-older', 'mee-newer'):
        f = launch_store(base, 'dialect-' + dialect, dialect=dialect)
        result = dry_run(f, mode='observer')
        assert ('dialect-launch-unresolved' in codes(result)) == (dialect not in ('mee-newer',)), dialect
    newer = launch_store(base, 'newer', res=b'spawntable {\n}\n' + CLASSIC, dialect='unknown')
    assert dry_run(newer, licenses=['licenses:0'], weapons=['weapons:0'])['value']['candidate_argv']
    for i, kw in enumerate([dict(menu=b'weapons {\n{\n name = \'open\'\n'), dict(menu=b'}\n' + CLASSIC), dict(res=b'hunterinfo {\n}\n' + CLASSIC)]):
        f = launch_store(base, f'broken-{i}', **kw)
        assert 'catalog-parse-review-required' in codes(dry_run(f, mode='observer'))
    # Unrecognized installations end at stage one: nothing projected or refreshed.
    gone = launch_store(base, 'gone')
    (gone['root'] / 'HUNTDAT/AREAS/area1.MAP').unlink()
    assert 'incomplete-root' not in codes(dry_run(gone, mode='observer')) and 'missing-map-pair' in codes(dry_run(gone, mode='observer'))
    (gone['root'] / 'HUNTDAT/_RES.TXT').unlink()
    result = dry_run(gone, mode='observer')
    assert result['value']['capabilities']['installation_recognized'] == 'no' and 'affordability' not in result['value']
    import shutil
    shutil.rmtree(gone['root'])
    assert codes(dry_run(gone, mode='observer')) == ['missing-installation']
    foreign = launch_store(base, 'foreign')
    edit_manifest(foreign['store'], lambda d: d['instances'][foreign['instance']].update(
        path='C:\\Games\\Carnivores' if os.name != 'nt' else '/games/carnivores', path_flavor='nt' if os.name != 'nt' else 'posix'))
    assert codes(dry_run(foreign, mode='observer')) == ['foreign-path']
    # Engine evidence and the legacy fallback capability.
    for i, engine in enumerate(('CARN2.EXE', 'game.exe', 'x.ExE', 'Carnivores2', 'tool.ren', '\xe9\u2603.EXE', None)):
        f = launch_store(base, f'engine-{i}', engine=engine)
        result = dry_run(f, mode='observer')
        assert result['value']['capabilities']['legacy_windows_fallback_available'] == ('candidate-files-only' if engine and engine.lower().endswith('.exe') else 'unknown')
    engineless = launch_store(base, 'engineless')
    (engineless['root'] / 'CARN2.EXE').unlink()
    result = dry_run(engineless, mode='observer')
    assert {'engine-evidence-changed', 'engine-review-required'} <= set(codes(result)) and result['value']['capabilities']['bundled_engine_evidence'] == 'none'


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='c2-planning-') as temp:
        genesis_cases(Path(temp))
        launch_cases(Path(temp))
    NATIVE.close()
    print(f'{CASES} planning oracle cases; exact presentation bytes, error kinds/messages and typed handles')
