#!/usr/bin/env python3
"""Optional CLI entry point; no UI toolkit or engine dependency."""
import argparse
import json
import os
from pathlib import Path
import sys

from lodge.catalog import project
from lodge.discovery import (DIALECTS, discover, get_instance, move_candidates,
                             refresh_instance, register, relocate)
from lodge.launch import prepare, simulated_return
from lodge.profiles import associate, inspect_set, inventory, refresh_association
from lodge.store import FrontendError, Store, hunter
from lodge.genesis import plan_observer
from lodge.reconciliation import reconcile_session
from lodge.session_io import read_journal
from lodge.session_runner import recover_session, run_session
from lodge.sessions import SCENARIOS, prepare_session


def parser():
    root = argparse.ArgumentParser(description='Carnivores lodge backend prototype: identity, discovery, lossless inspection, dry-run planning.')
    default = Path(os.environ.get('LOCALAPPDATA', Path.home() / '.local/share')) / 'carnivores-lodge'
    root.add_argument('--store', type=Path, default=default, help='Frontend JSON/snapshot directory; never a game save folder')
    root.add_argument('--probe', type=Path, help='Path to the separately built c2-profile-probe')
    commands = root.add_subparsers(dest='command', required=True)
    commands.add_parser('status')
    commands.add_parser('recover-backup')
    h = commands.add_parser('hunter').add_subparsers(dest='action', required=True)
    h.add_parser('list')
    h.add_parser('create').add_argument('name')
    for name in ('select', 'rename', 'archive'):
        p = h.add_parser(name)
        p.add_argument('id')
        if name == 'rename':
            p.add_argument('name')
    exp = commands.add_parser('expedition').add_subparsers(dest='action', required=True)
    exp.add_parser('list')
    d = exp.add_parser('discover')
    d.add_argument('path', type=Path)
    d.add_argument('--register-managed', action='store_true', help='Explicitly register coherent roots below this managed Expeditions directory')
    r = exp.add_parser('register')
    r.add_argument('path', type=Path)
    r.add_argument('--dialect', choices=sorted(DIALECTS), default='unknown')
    r.add_argument('--family')
    r.add_argument('--release')
    m = exp.add_parser('relocate')
    m.add_argument('id')
    m.add_argument('path', type=Path)
    exp.add_parser('refresh').add_argument('id')
    profiles = commands.add_parser('profiles')
    profiles.add_argument('instance')
    a = commands.add_parser('associate')
    a.add_argument('hunter')
    a.add_argument('instance')
    a.add_argument('state_key', help='Exact inventory key, e.g. trophy00 or MODDAT/trophy00')
    a.add_argument('--origin', required=True, choices=['personal', 'bundled-example', 'unknown'])
    a.add_argument('--ownership', choices=['referenced', 'managed'])
    a.add_argument('--import-copy', action='store_true', help='Explicit import: independent, lossless managed snapshot')
    commands.add_parser('refresh-state').add_argument('association')
    c = commands.add_parser('catalog')
    source = c.add_mutually_exclusive_group(required=True)
    source.add_argument('--instance')
    source.add_argument('--path', type=Path, help='Read observations from partial audit material; does not register a game')
    l = commands.add_parser('launch-dry-run')
    l.add_argument('association')
    l.add_argument('--area', required=True, help='Catalog ID, e.g. areas:0')
    for category in ('license', 'weapon', 'equipment'):
        l.add_argument('--' + category, action='append', default=[])
    l.add_argument('--mode', choices=['hunt', 'observer'], default='hunt')
    l.add_argument('--time', type=int, choices=[0, 1, 2], default=1)
    ret = commands.add_parser('simulate-return')
    ret.add_argument('association')
    ret.add_argument('--exit-code', type=int, default=0)
    session = commands.add_parser('session', help='Controlled synthetic sessions; no native engine execution')
    actions = session.add_subparsers(dest='action', required=True)
    s = actions.add_parser('prepare-synthetic')
    s.add_argument('association')
    s.add_argument('--area', required=True)
    s.add_argument('--scenario', choices=SCENARIOS, default='unchanged')
    s.add_argument('--time', type=int, choices=[0, 1, 2], default=1)
    s.add_argument('--timeout', type=float, default=5)
    for name in ('inspect', 'run', 'reconcile', 'recover'):
        actions.add_parser(name).add_argument('id')
    genesis = commands.add_parser('genesis-observer-plan', help='Pinned, blocked Genesis observer plan; never launches')
    genesis.add_argument('association')
    genesis.add_argument('--area', required=True)
    genesis.add_argument('--time', type=int, choices=[0, 1, 2], default=1)
    genesis.add_argument('--engine', type=Path, help='Optional binary for hash-only evidence; never executed')
    settings = commands.add_parser('host-settings')
    settings.add_argument('--json', help='JSON object with display, audio and/or input preferences; no native save mutation')
    return root


def execute(args):
    store = Store(args.store)
    if args.command == 'session':
        if args.action == 'prepare-synthetic':
            return prepare_session(store, args.association, args.area, args.scenario,
                                   args.time, args.timeout, args.probe)
        if args.action == 'inspect':
            return read_journal(store, args.id)
        if args.action == 'recover':
            return recover_session(store, args.id, args.probe)
        if args.action == 'reconcile':
            return reconcile_session(store, args.id, args.probe)
        if args.action == 'run':
            result = run_session(store, args.id, args.probe)
            return reconcile_session(store, args.id, args.probe) if result['state'] == 'returned' else result
    if args.command == 'genesis-observer-plan':
        return plan_observer(store, args.association, args.area, args.time, args.probe, args.engine)
    if args.command == 'recover-backup':
        store.restore_backup()
        return {'result': 'backup-restored', 'store': str(store.path)}
    data = store.read()
    if args.command == 'status':
        return data
    if args.command == 'hunter' and args.action == 'list':
        return {'active_hunter': data['active_hunter'], 'hunters': list(data['hunters'].values())}
    if args.command == 'expedition' and args.action == 'list':
        return list(data['instances'].values())
    if args.command == 'profiles':
        instance = get_instance(data, args.instance)
        if instance['path_flavor'] != os.name:
            raise FrontendError('foreign installation path requires relocation')
        return [inspect_set(instance['path'], state, args.probe, instance['dialect_hint']) for state in inventory(instance['path'])]
    if args.command == 'catalog':
        if args.path:
            return project(args.path)
        instance = get_instance(data, args.instance)
        if instance['path_flavor'] != os.name:
            raise FrontendError('foreign installation path requires relocation')
        return project(instance['path'], instance['dialect_hint'])
    if args.command == 'host-settings' and args.json is None:
        return data['host_settings']
    if args.command == 'expedition' and args.action == 'discover' and not args.register_managed:
        results = discover(args.path)
        for result in results:
            if result['recognized']:
                result['possible_moves'] = move_candidates(data, result['path'])
        return results
    with store.transaction() as data:
        if args.command == 'hunter':
            return hunter(data, args.action, getattr(args, 'id', None), getattr(args, 'name', None))
        if args.command == 'expedition':
            if args.action == 'register':
                return register(data, args.path, dialect=args.dialect, family=args.family, release=args.release)
            if args.action == 'relocate':
                return relocate(data, args.id, args.path)
            if args.action == 'refresh':
                return refresh_instance(get_instance(data, args.id))
            if args.action == 'discover':
                return [register(data, result['path'], 'managed', managed_root=args.path)
                        for result in discover(args.path) if result['recognized']]
        if args.command == 'associate':
            if args.import_copy and args.ownership == 'referenced':
                raise FrontendError('--import-copy conflicts with referenced ownership')
            return associate(store, data, args.hunter, args.instance, args.state_key, args.origin,
                             'managed' if args.import_copy else args.ownership, args.probe)
        if args.command == 'refresh-state':
            return refresh_association(store, data, args.association, args.probe)
        if args.command == 'launch-dry-run':
            return prepare(store, data, args.association, args.area, args.license, args.weapon, args.equipment, args.mode, args.time, args.probe)
        if args.command == 'simulate-return':
            return simulated_return(store, data, args.association, args.exit_code, args.probe)
        if args.command == 'host-settings':
            value = json.loads(args.json)
            if not isinstance(value, dict) or set(value) - {'display', 'audio', 'input'} or any(not isinstance(v, dict) for v in value.values()):
                raise FrontendError('settings must be objects keyed by display, audio and/or input')
            data['host_settings'].update(value)
            return data['host_settings']
    raise FrontendError('unsupported command')


def main():
    args = parser().parse_args()
    try:
        result = execute(args)
        print(json.dumps(result, indent=2, ensure_ascii=True, allow_nan=False))
        return 0
    except (FrontendError, OSError, ValueError) as error:
        print(json.dumps({'error': str(error)}), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
