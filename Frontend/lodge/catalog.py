"""Conservative catalog observations, not an engine script interpreter."""
import hashlib
from pathlib import Path
import re

from .discovery import diagnostic, resolve_reference, walk_files
from .store import FrontendError

TOKEN = re.compile(r"//[^\n]*|[;#][^\n]*|'[^'\n]*'|\"[^\"\n]*\"|\n|[{}=]|[^\s{}=;#'\"]+")
OLDER = {'hunterinfo', 'oldambients', 'corpseambients', 'mapambients'}
NEWER = {'spawntable', 'packtable', 'trophytable'}


def parse_script(content, source):
    if len(content) > 8 * 1024 * 1024:
        raise FrontendError('script exceeds conservative 8 MiB observation limit')
    text = content.decode('latin1')
    tokens = []
    line = 1
    for match in TOKEN.finditer(text):
        token = match[0]
        if token.startswith(('//', ';', '#')):
            continue
        tokens.append((token, line))
        if token == '\n':
            line += 1
    tree = {'name': '', 'line': 1, 'attributes': [], 'children': [], 'raw': []}
    stack = [tree]
    pending = []
    diagnostics = []

    def flush():
        if not pending:
            return
        values = [v for v, _ in pending]
        if '=' in values:
            equal = values.index('=')
            key = ' '.join(values[:equal])
            raw = ' '.join(values[equal + 1:])
            stack[-1]['attributes'].append({'key': key, 'raw': raw, 'line': pending[0][1], 'source': source})
        else:
            stack[-1]['raw'].append({'raw': ' '.join(values), 'line': pending[0][1], 'source': source})
        pending.clear()

    for token, line in tokens:
        if token == '.' and len(stack) == 1:
            flush()
            break
        if token == '\n':
            # Named blocks may put their opening brace on the following line.
            if any(t == '=' for t, _ in pending):
                flush()
        elif token == '{':
            if len(stack) >= 128:
                raise FrontendError('script nesting exceeds observation limit')
            node = {'name': ' '.join(t for t, _ in pending), 'line': pending[0][1] if pending else line,
                    'attributes': [], 'children': [], 'raw': []}
            pending.clear()
            stack[-1]['children'].append(node)
            stack.append(node)
        elif token == '}':
            flush()
            if len(stack) == 1:
                diagnostics.append(diagnostic('unmatched-brace', 'Unmatched closing brace.', source=source, line=line))
            else:
                stack.pop()
        else:
            pending.append((token, line))
    flush()
    if len(stack) != 1:
        diagnostics.append(diagnostic('unclosed-block', 'Unclosed script block.', source=source))
    return {'source': source, 'sha256': hashlib.sha256(content).hexdigest(), 'tree': tree, 'diagnostics': diagnostics}


def scalar(raw):
    if len(raw) >= 2 and raw[0] == raw[-1] and raw[0] in "'\"":
        return raw[1:-1]
    if re.fullmatch(r'[+-]?\d+', raw):
        return int(raw)
    return raw


def attribute(node, key):
    values = [v for v in node['attributes'] if v['key'].casefold() == key]
    # Duplicate assignments are observations, not a guess at interpreter precedence.
    return scalar(values[0]['raw']) if len(values) == 1 else None


def blocks(script, name):
    return [node for node in script['tree']['children'] if node['name'].casefold() == name]


def text_reference(root, reference):
    result = resolve_reference(root, reference)
    if result['status'] == 'found' and (Path(root) / result['path']).is_file():
        path = Path(root) / result['path']
        if path.stat().st_size > 1024 * 1024:
            return {**result, 'status': 'too-large'}
        raw = path.read_bytes()
        result.update(sha256=hashlib.sha256(raw).hexdigest(), encoding='latin1-byte-projection',
                      lines=raw.decode('latin1').splitlines())
    return result


def project(root, dialect_hint='unknown'):
    root = Path(root).resolve()
    scripts = {}
    diagnostics = [diagnostic('static-projection-only', 'Catalog observations do not certify engine, save, rank or mode semantics.'),
                   diagnostic('artwork-semantics-unread', 'Presentation assets may contain instructions not recovered by text projection.'),
                   diagnostic('text-encoding-unverified', 'Text uses a reversible Latin-1 byte projection; original code page is unknown.')]
    for name in ('_MENU.TXT', '_RES.TXT'):
        ref = resolve_reference(root, 'HUNTDAT/' + name)
        if ref['status'] == 'found':
            path = root / ref['path']
            if path.stat().st_size > 8 * 1024 * 1024:
                raise FrontendError('script too large')
            scripts[name] = parse_script(path.read_bytes(), ref['path'])
            diagnostics.extend(scripts[name]['diagnostics'])
        elif ref['status'] != 'missing':
            raise FrontendError('ambiguous or unsafe script: ' + name)
    if not scripts:
        raise FrontendError('no recognizable content/menu script')
    script = scripts.get('_MENU.TXT', scripts.get('_RES.TXT'))
    game = scripts.get('_RES.TXT', script)
    sections = {n['name'].casefold() for n in game['tree']['children']}
    if sections & OLDER and sections & NEWER:
        detected = 'mixed-unresolved'
    elif sections & OLDER:
        detected = 'mee-older'
    elif sections & NEWER:
        detected = 'mee-newer'
    else:
        detected = 'classic-syntax-family-unknown'
    if dialect_hint == 'iceage-triassic':
        dialect = dialect_hint
    elif dialect_hint != 'unknown':
        dialect = dialect_hint
        if detected in ('mee-older', 'mee-newer', 'mixed-unresolved') and detected != dialect_hint:
            diagnostics.append(diagnostic('dialect-conflict', 'Declared dialect conflicts with script evidence.'))
    else:
        dialect = detected
    if detected == 'mee-older':
        diagnostics.append(diagnostic('older-mee-engine-gap', 'Audited modern engine does not dispatch older split character blocks.'))
    prices = [a for node in blocks(script, 'prices') for a in node['attributes']]
    by_price = lambda key: [p for p in prices if p['key'].casefold() == key]
    entries = {'areas': [], 'licenses': [], 'weapons': [], 'equipment': []}
    for section, group in (('characters', 'licenses'), ('weapons', 'weapons')):
        for section_index, parent in enumerate(blocks(script, section)):
            for source_index, node in enumerate(parent['children']):
                ai = attribute(node, 'ai')
                if group == 'licenses' and (type(ai) is not int or ai < 10):
                    continue
                index = len(entries[group])
                entry = {'id': f'{group}:{index}', 'kind': 'license' if group == 'licenses' else 'weapon',
                         'ordinal': index, 'label': attribute(node, 'name'),
                         'source': script['source'], 'line': node['line'], 'source_ordinal': source_index,
                         'section_ordinal': section_index, 'attributes': node['attributes'],
                         'nested_observations': node['children'], 'price': None}
                entry['declared_references'] = [
                    {'field': key, **resolve_reference(root, attribute(node, key))}
                    for key in ('file', 'pic', 'thumbnail') if isinstance(attribute(node, key), str)]
                if group == 'licenses':
                    entry.update(ai=ai, species_resolution='unresolved-license-may-cover-multiple-species')
                    number = ai - 9
                    entry['references'] = [resolve_reference(root, f'HUNTDAT/MENU/PICS/DINO{number}.TGA'),
                                           text_reference(root, f'HUNTDAT/MENU/TXT/DINO{number}.TXM'),
                                           text_reference(root, f'HUNTDAT/MENU/TXT/DINO{index + 1}.TXM')]
                    entry['reference_policy'] = 'AI-based legacy candidate and ordinal candidate; neither establishes species identity'
                else:
                    entry['references'] = [resolve_reference(root, f'HUNTDAT/MENU/PICS/WEAPON{index + 1}.TGA'),
                                           text_reference(root, f'HUNTDAT/MENU/TXT/WEAPON{index + 1}.TXT')]
                entries[group].append(entry)
                if entry['label'] is None or entry['label'] == '' or re.search(r'\b(uncheck|check|select|click)\b', str(entry['label']), re.I):
                    diagnostics.append(diagnostic('unusual-label', 'Blank, ambiguous or instruction-like label retained verbatim.', entry_id=entry['id']))
    for ai in sorted({e['ai'] for e in entries['licenses']}):
        identities = [e['id'] for e in entries['licenses'] if e['ai'] == ai]
        if len(identities) > 1:
            diagnostics.append(diagnostic('duplicate-ai', 'Ordered license identities are independent of AI.', ai=ai, entries=identities))
    for key, group in (('dino', 'licenses'), ('weapon', 'weapons')):
        offset = next((i for i, e in enumerate(entries[group]) if e.get('ai') == 10), 0) if key == 'dino' else 0
        for index, price in enumerate(by_price(key)):
            target = index + offset
            value = scalar(price['raw'])
            if target < len(entries[group]):
                entries[group][target]['price'] = value if type(value) is int else None
                entries[group][target]['price_source'] = price
            else:
                diagnostics.append(diagnostic('surplus-price', 'Price has no corresponding selectable definition; retained without indexing past catalog.', category=group, ordinal=index, observation=price))
    for index, price in enumerate(by_price('area'), 1):
        stem = f'area{index}'
        candidates = ['external', stem] if index == 6 else [stem]
        pairs = [{'stem': candidate,
                  'map': resolve_reference(root, f'HUNTDAT/AREAS/{candidate}.MAP'),
                  'rsc': resolve_reference(root, f'HUNTDAT/AREAS/{candidate}.RSC')} for candidate in candidates]
        complete = [p for p in pairs if p['map']['status'] == p['rsc']['status'] == 'found']
        description = text_reference(root, f'HUNTDAT/MENU/TXT/AREA{index}.TXT')
        entry = {'id': f'areas:{index - 1}', 'kind': 'advertised-area-slot', 'ordinal': index - 1,
                 'slot': index, 'label': description.get('lines', [None])[0] if description.get('lines') else None,
                 'price': scalar(price['raw']) if type(scalar(price['raw'])) is int else None,
                 'price_source': price, 'source': script['source'], 'line': price['line'],
                 'map_candidates': pairs, 'launch_stem': complete[0]['stem'] if len(complete) == 1 else None,
                 'references': [description, resolve_reference(root, f'HUNTDAT/MENU/PICS/AREA{index}.TGA')]}
        entries['areas'].append(entry)
        if len(complete) != 1:
            diagnostics.append(diagnostic('area-resource-unresolved', 'Advertised slot has missing or ambiguous MAP/RSC pair.', entry_id=entry['id']))
    if blocks(script, 'areas'):
        diagnostics.append(diagnostic('explicit-areas-uninterpreted', 'Explicit area declarations retained in observations; adapter not established.'))
    conventions = ['camoflag', 'radar', 'scent', 'double']
    for index, price in enumerate(by_price('acces')):
        refs = [text_reference(root, f'HUNTDAT/MENU/TXT/EQUIP{index + 1}.NFO'),
                resolve_reference(root, f'HUNTDAT/MENU/PICS/EQUIP{index + 1}.TGA')]
        if index < len(conventions):
            refs.append(text_reference(root, f'HUNTDAT/MENU/TXT/{conventions[index]}.NFO'))
        elif len(by_price('acces')) == 5 and index == 4:
            refs.append(text_reference(root, 'HUNTDAT/MENU/TXT/TRANQ.NFO'))
        entries['equipment'].append({'id': f'equipment:{index}', 'kind': 'native-accessory-slot',
                                     'ordinal': index, 'label': None, 'meaning': 'unresolved',
                                     'price': scalar(price['raw']) if type(scalar(price['raw'])) is int else None,
                                     'price_source': price, 'source': script['source'], 'line': price['line'], 'references': refs})
        texts = {r['sha256'] for r in refs if 'sha256' in r}
        if len(texts) > 1:
            diagnostics.append(diagnostic('description-conflict', 'Competing accessory descriptions retained without precedence.', entry_id=f'equipment:{index}'))
    diagnostics.append(diagnostic('equipment-semantics-unresolved', 'Accessory slots are observations; no extra equipment or mode is granted.'))
    physical_maps = []
    area_dir = resolve_reference(root, 'HUNTDAT/AREAS')
    if area_dir['status'] == 'found':
        physical_maps = [p.relative_to(root).as_posix() for p in walk_files(root / area_dir['path']) if p.suffix.lower() == '.map']
        descriptors = [p.relative_to(root).as_posix() for p in walk_files(root / area_dir['path']) if p.suffix.lower() == '.c2map']
        if descriptors:
            diagnostics.append(diagnostic('c2map-unvalidated', 'Descriptor files inventoried but not promoted to launchable hunts.', paths=descriptors))
    presentation = []
    menu_dir = resolve_reference(root, 'HUNTDAT/MENU')
    if menu_dir['status'] == 'found':
        presentation = [p.relative_to(root).as_posix() for p in walk_files(root / menu_dir['path']) if p.suffix.lower() in ('.tga', '.nfo', '.txt', '.txm')]
    return {'projection_version': 1, 'source': script['source'], 'source_sha256': script['sha256'],
            'dialect': {'hint': dialect_hint, 'observed': detected, 'effective': dialect, 'engine_build': 'unknown'},
            **entries, 'starting_score_observations': by_price('start'),
            'score_modifier_observations': [node for s in scripts.values() for node in blocks(s, 'accessories')],
            'physical_maps': sorted(physical_maps), 'presentation_references': sorted(presentation),
            'title': None, 'title_hints': [root.name], 'scripts': scripts, 'diagnostics': diagnostics,
            'capabilities': {'content_dialect_recognized': 'partial', 'console_can_be_generated': 'partial',
                             'modern_engine_compatibility': 'known-dispatch-gap' if detected == 'mee-older' else 'unknown',
                             'launch_tested': 'unknown', 'hunt_save_round_trip_validated': 'unknown',
                             'trophy_interpretation_validated': 'unknown'}}
