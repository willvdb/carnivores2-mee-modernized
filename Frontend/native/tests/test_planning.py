#!/usr/bin/env python3
"""Genesis policy oracles against the unchanged lodge modules.

Every catalog is projected from an authored temporary tree; revisions, slots,
selections and scores are supplied values. No game assets,
installs, default lodge or profiles are used.
"""
import copy
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import catalog, genesis, genesis_hunt
from lodge.discovery import register
from lodge.genesis import GENESIS_REVISION
from lodge.store import FrontendError, Store, hunter, validate

DRIVER = str(Path(sys.argv[1]))
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


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='c2-planning-') as temp:
        genesis_cases(Path(temp))
    NATIVE.close()
    print(f'{CASES} planning oracle cases; exact presentation bytes, error kinds/messages and typed handles')
