#!/usr/bin/env python3
"""Authored read-only catalog projection oracles against unchanged lodge.catalog."""
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge import catalog as reference
from lodge.store import FrontendError

DRIVER = str(Path(sys.argv[1]).resolve())
CASES = 0
POSIX = os.name == 'posix'
HINTS = ('unknown', 'iceage-triassic', 'mee-older', 'mee-newer', 'mixed-unresolved', 'c2-classic', 'arbitrary hint', '')


def native(request, cwd=None):
    p = subprocess.run([DRIVER], input=(json.dumps(request) + '\n').encode(), capture_output=True, timeout=120, cwd=cwd)
    assert p.returncode == 0, (request, p.returncode, p.stderr)
    return json.loads(p.stdout)


def oracle(request):
    if request['op'] == 'project':
        return reference.project(request['root'], request.get('dialect_hint', 'unknown'))
    return reference.text_reference(request['root'], request['reference'])


def check(op, root, cwd=None, **args):
    global CASES
    request = {'op': op, 'root': str(root), **args}
    old = Path.cwd()
    try:
        if cwd is not None:
            os.chdir(cwd)
        try:
            expected = oracle(request)
            expected_json = json.dumps(expected, indent=2, ensure_ascii=True, allow_nan=False) + '\n'
        except FrontendError as e:
            expected = ('catalog', str(e))
        except ValueError as e:
            expected = ('scalar-conversion', str(e)) if str(e).startswith('Exceeds the limit') else ('unexpected', None)
        except (OSError, RuntimeError, KeyError):
            expected = ('unexpected', None)
    finally:
        os.chdir(old)
    actual = native(request, cwd=cwd)
    if isinstance(expected, tuple):
        kind, message = expected
        assert not actual['ok'], (request, expected, actual)
        if message is not None:
            assert (actual['kind'], actual['error']) == (kind, message), (request, expected, actual)
    else:
        assert actual['ok'], (request, expected, actual)
        # Exact presentation bytes establish key order, value kinds and LF.
        assert actual['json'] == expected_json, (request, expected_json, actual)
        assert json.dumps(actual['value'], ensure_ascii=True, separators=(',', ':')) == json.dumps(expected, ensure_ascii=True, separators=(',', ':')), (request, expected, actual)
    CASES += 1
    return actual


def write(root, relative, data=b'x'):
    p = root / relative
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)
    return p


def snapshot(root):
    out = []
    for parent, dirs, files in os.walk(root, followlinks=False):
        for name in dirs + files:
            p = Path(parent) / name
            s = p.lstat()
            out.append((str(p), s.st_mode, s.st_size, s.st_mtime_ns,
                        os.readlink(p) if p.is_symlink() else hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else None))
    return sorted(out)


def observed(root, hints=('unknown',), cwd=None):
    before = snapshot(root)
    results = [check('project', root, cwd=cwd, dialect_hint=hint)['value'] if hint != 'unknown' else check('project', root, cwd=cwd)['value'] for hint in hints]
    assert snapshot(root) == before, 'source modified by projection'
    return results[0]


def game(root, menu=None, res=None):
    for part in ('HUNTDAT/MENU/TXT', 'HUNTDAT/MENU/PICS', 'HUNTDAT/AREAS'):
        (root / part).mkdir(parents=True, exist_ok=True)
    if menu is not None:
        write(root, 'HUNTDAT/_MENU.TXT', menu)
    if res is not None:
        write(root, 'HUNTDAT/_RES.TXT', res)
    return root


MENU = b"""weapons {
{
 name = 'Rifle'
 file = 'HUNTDAT/MENU/PICS/WEAPON1.TGA'
 pic = 12
 thumbnail = ''
}
{
 name = 'Check the scope'
 file = 'HUNTDAT/MENU/PICS/missing.tga'
 file = 'duplicate declaration'
}
{
 name = 7
 file = 'HUNTDAT\\MENU\\PICS\\weapon1.tga'
 pic = 'HUNTDAT/../HUNTDAT/MENU'
}
}
weapons {
{
 name = "Second block's weapon"
 file = '../outside'
 pic = 'C:x'
 thumbnail = 'HUNTDAT/MENU/PICS'
}
}
characters {
{
 name = 'Skipped low ai'
 ai = 9
}
{
 name = 'Quoted ai'
 ai = '10'
}
{
 name = 'Text ai'
 ai = abc
}
{
 name = 'No ai'
}
{
 name = 'Duplicate ai attributes'
 ai = 10
 ai = 11
}
{
 name = 'First real'
 ai = 12
 addition area1 {
  name = 'nested'
 }
}
{
 name = ''
 ai = 010
}
{
 name = 'Uncheck this to blue'
 ai = 11
}
{
 ai = +10
}
{
 name = 'Negative'
 ai = -5
}
{
 name = 'Huge ai'
 ai = 123456789012345678901234567890
 pic = 'HUNTDAT/MENU/PICS/DINO1.TGA'
}
}
characters {
{
 name = 'Second characters block'
 ai = 12
}
{
 name = 'Latin \xff\xe9 label \xd7 check'
 ai = 13
}
}
prices {
 start = 0
 start = 'quoted'
 START = 5
 area = 20
 area = 999999
 area = abc
 area = '30'
 area = -1
 area = 6
 area = 7
 dino = 10
 dino = 15
 dino = 30
 dino = 99
 dino = 1.5
 dino = 'x'
 weapon = 0
 weapon = 999
 weapon = +7
 weapon = 007
 weapon = surplus
 acces = 15
 acces = 135
 acces = 20
 acces = 25
 acces = 30
}
Areas {
 x = 1
}
accessories {
 x = 1
}
ACCESSORIES {
}
.
ignored tail { x = 1
"""

SEPARATORS = b'a\nb\rc\r\nd\x0be\x0cf\x1cg\x1dh\x1ei\x85j\n\n\r\r\n'


def full_game(base, name='Game'):
    root = game(base / name, MENU, b'hunterinfo {\n}\nweapons {\n}\ncharacters {\n{\n name = \'Res animal\'\n ai = 10\n}\n}\naccessories {\n res = 1\n}\n')
    txt = root / 'HUNTDAT/MENU/TXT'
    pics = root / 'HUNTDAT/MENU/PICS'
    areas = root / 'HUNTDAT/AREAS'
    write(txt, 'AREA1.TXT', b'\nAn intentionally blank name\n')
    write(txt, 'AREA2.TXT', b'')
    write(txt, 'AREA3.TXT', b'first\r\nsecond\rthird')
    write(txt, 'AREA4.TXT', b'\xff\xfe high bytes\x85next\x1c')
    write(txt, 'AREA6.TXT', b'L' * (1024 * 1024 + 1))
    (txt / 'AREA7.TXT').mkdir()
    write(areas, 'area1.MAP'); write(areas, 'area1.RSC')
    write(areas, 'AREA2.MAP')
    write(areas, 'area3.rsc')
    write(areas, 'Area4.map'); write(areas, 'AREA4.RSC')
    write(areas, 'external.MAP'); write(areas, 'EXTERNAL.RSC')
    write(areas, 'area6.map'); write(areas, 'area6.rsc')
    write(areas, 'nested/deep.Map'); write(areas, 'nested/deeper/x.MAP'); write(areas, 'nested/n.c2map')
    write(areas, '.map'); write(areas, 'c.c2map'); write(areas, 'D.C2MAP'); write(areas, '\xff.map'); write(areas, 'other.txt')
    write(areas, 'Z.MAP'); write(areas, 'a.map'); write(areas, 'b.MAP.bak')
    write(pics, 'DINO1.TGA'); write(pics, 'dino3.tga'); write(pics, 'WEAPON1.TGA'); write(pics, 'AREA1.TGA'); write(pics, 'EQUIP2.tga')
    write(pics, 'nested/deep.nfo'); write(pics, 'skip.bin'); write(pics, '.txt'); write(pics, 'trailing.TXT')
    write(txt, 'DINO1.TXM', b'Dino one\nsecond line')
    write(txt, 'DINO3.TXM', b'Dino three')
    write(txt, 'DINO4.TXM', b'')
    write(txt, 'WEAPON1.TXT', SEPARATORS)
    write(txt, 'WEAPON2.TXT', b'M' * (1024 * 1024))
    write(txt, 'WEAPON3.TXT', b'M' * (1024 * 1024 + 1))
    write(txt, 'WEAPON4.TXT', b'\r\n\r\nline\xe9\r\n')
    write(txt, 'EQUIP1.NFO', b'one'); write(txt, 'CAMOFLAG.NFO', b'different')
    write(txt, 'EQUIP2.NFO', b'same'); write(txt, 'radar.nfo', b'same')
    write(txt, 'EQUIP3.NFO', b'three')
    write(txt, 'scent.nfo', b'scent only')
    write(txt, 'EQUIP5.NFO', b'five'); write(txt, 'TRANQ.NFO', b'tranq')
    write(txt, 'double.NFO', b'L' * (1024 * 1024 + 1))
    write(root, 'HUNTDAT/MENU/menu.txm'); write(root, 'HUNTDAT/MENU/README.TXT')
    if POSIX:
        write(areas, 'raw\udcff.map'); write(areas, 'back\\slash.map'); write(areas, 'x.map.')
        write(txt, 'raw\udcfe.txt')
        write(areas, 'AREA5.MAP'); write(areas, 'area5.map'); write(areas, 'AREA5.RSC')
    return root


def main(base):
    missing = base / 'absent'
    for root in (missing, str(missing) + '\0tail'):
        check('project', root)
        check('text_reference', root, reference='HUNTDAT/x')
    plain = base / 'plain-file'
    plain.write_bytes(b'not a directory')
    check('project', plain)
    empty = base / 'empty'
    empty.mkdir()
    check('project', empty)
    write(empty, 'HUNTDAT', b'file, not a directory')
    check('project', empty)
    check('text_reference', empty, reference='HUNTDAT')

    root = full_game(base)
    result = observed(root, HINTS)
    codes = [d['code'] for d in result['diagnostics']]
    for code in ('older-mee-engine-gap', 'unusual-label', 'duplicate-ai', 'surplus-price', 'area-resource-unresolved',
                 'explicit-areas-uninterpreted', 'description-conflict', 'equipment-semantics-unresolved', 'c2map-unvalidated'):
        assert code in codes, code
    assert result['source'] == 'HUNTDAT/_MENU.TXT' and result['dialect']['observed'] == 'mee-older'
    assert [e['ai'] for e in result['licenses']] == [12, 10, 11, 10, 123456789012345678901234567890, 12, 13]
    assert [e['label'] for e in result['weapons']] == ['Rifle', 'Check the scope', 7, "Second block's weapon"]
    assert [('price_source' in e, e['price']) for e in result['licenses']] == [(False, None), (True, 10), (True, 15), (True, 30), (True, 99), (True, None), (True, None)]
    assert [e['price'] for e in result['weapons']] == [0, 999, 7, 7]
    assert [e['launch_stem'] for e in result['areas']] == ['area1', None, None, 'area4', None, None, None]
    assert [e['label'] for e in result['areas']] == ['', None, 'first', '\xff\xfe high bytes', None, None, None]
    statuses = [r['status'] for e in result['weapons'] for r in e['references']]
    assert statuses == ['found', 'found', 'missing', 'found', 'missing', 'too-large', 'missing', 'found']
    assert result['weapons'][0]['references'][1]['lines'] == ['a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', '', '', '']
    assert [d['field'] for d in result['weapons'][0]['declared_references']] == ['file', 'thumbnail']
    assert len(result['equipment']) == 5 and len(result['equipment'][4]['references']) == 3
    assert result['physical_maps'][0] == 'HUNTDAT/AREAS/AREA2.MAP' and result['physical_maps'][-1] == 'HUNTDAT/AREAS/\xff.map' and 'HUNTDAT/AREAS/.map' not in result['physical_maps']
    assert 'HUNTDAT/AREAS/nested/deeper/x.MAP' in result['physical_maps'] and result['title_hints'] == ['Game']
    assert 'HUNTDAT/MENU/PICS/nested/deep.nfo' in result['presentation_references'] and 'HUNTDAT/MENU/PICS/.txt' not in result['presentation_references']
    assert list(result['scripts']) == ['_MENU.TXT', '_RES.TXT'] and len(result['score_modifier_observations']) == 3
    for spelling in ('.', './', 'HUNTDAT/..', str(root) + os.sep, os.path.join('..', root.name)):
        observed(root if spelling.startswith(str(root)) else spelling, cwd=root)
    refs = ['HUNTDAT/MENU/TXT/WEAPON1.TXT', 'HUNTDAT/MENU/TXT/WEAPON2.TXT', 'HUNTDAT/MENU/TXT/WEAPON3.TXT', 'HUNTDAT/MENU/TXT/weapon4.txt',
            'HUNTDAT/MENU/TXT/AREA7.TXT', 'HUNTDAT/MENU/TXT', 'HUNTDAT\\MENU\\TXT\\DINO1.TXM', 'HUNTDAT/MENU/TXT/DINO4.TXM',
            'HUNTDAT/MENU/TXT/absent.txt', 'HUNTDAT/../HUNTDAT/MENU/TXT/DINO1.TXM', '/HUNTDAT/MENU/TXT/DINO1.TXM', '', '.', 'C:x',
            'HUNTDAT/MENU/TXT/DINO1.TXM/', 'HUNTDAT//MENU/./TXT/DINO1.TXM', 'HUNTDAT/AREAS/\xff.map', 'HUNTDAT/MENU/TXT/\0']
    if POSIX:
        refs += ['HUNTDAT/AREAS/raw\udcff.map', 'HUNTDAT/AREAS/AREA5.MAP']
    for ref in refs:
        check('text_reference', root, reference=ref)
    for spelling in ('.', 'HUNTDAT/..', ''):
        check('text_reference', spelling, reference='HUNTDAT/MENU/TXT/WEAPON1.TXT', cwd=root)
    # Every script/dialect combination and the script precedence.
    for i, (menu, res) in enumerate([(MENU, None), (None, MENU), (MENU, b'spawntable {\n}\n'), (MENU, b'hunterinfo {\n}\ntrophytable {\n}\n'),
                                     (b'characters {\n}\n', b'weapons {\n}\n'), (b'', b''), (None, b'')]):
        observed(game(base / f'combo-{i}', menu, res), HINTS)
    for i, res in enumerate([b'HunterInfo {\n}\n', b'oldambients{}', b'CorpseAmbients {} MapAmbients {}', b'SPAWNTABLE {}', b'packtable {}',
                             b'trophytable {}', b'hunterinfo {} spawntable {}', b'x { hunterinfo {} }', b'hunterinfo = 1\n']):
        observed(game(base / f'dialect-{i}', None, res), HINTS)
    # Case-only spellings are retained in sources; lowercase directories still resolve.
    lower = base / 'lower'
    write(lower, 'huntdat/_menu.txt', MENU)
    write(lower, 'huntdat/menu/txt/area1.txt', b'lower label')
    write(lower, 'huntdat/areas/AREA1.map'); write(lower, 'huntdat/areas/area1.RSC')
    observed(lower)
    # Nesting/brace diagnostics propagate in script order without lines for unclosed blocks.
    observed(game(base / 'broken', b'characters {\n{\n name = \'open\'\n ai = 10\n', b'}\n}\nweapons {\n{\n'))
    observed(game(base / 'broken-res', None, b'prices {\n area = 1\n'))
    # Size limits: exactly 8 MiB parses; one more byte is rejected before reading.
    prefix = b'prices {\n area = 1\n}\n'
    limit = 8 * 1024 * 1024
    observed(game(base / 'exact-limit', prefix + b'#' + b'x' * (limit - len(prefix) - 1)))
    check('project', game(base / 'over-limit', prefix + b'#' + b'x' * (limit - len(prefix))))
    check('project', game(base / 'over-limit-res', None, b'x' * (limit + 1)))
    # Script references that are directories or unsafe/ambiguous fail with the reference errors.
    directory = game(base / 'script-directory')
    (directory / 'HUNTDAT/_MENU.TXT').mkdir()
    check('project', directory)
    (directory / 'HUNTDAT/_MENU.TXT').rmdir()
    write(directory, 'HUNTDAT/_RES.TXT', b'weapons {}')
    (directory / 'HUNTDAT/_MENU.TXT').mkdir()
    check('project', directory)
    if POSIX:
        ambiguous = game(base / 'ambiguous', MENU)
        write(ambiguous, 'HUNTDAT/_menu.txt', b'weapons {}')
        check('project', ambiguous)
        ambiguous_res = game(base / 'ambiguous-res', MENU, b'weapons {}')
        write(ambiguous_res, 'HUNTDAT/_res.txt', b'weapons {}')
        check('project', ambiguous_res)
        posix_links(base)
    # Conversion limits propagate from labels, ai and prices, including surplus prices.
    for i, script in enumerate([b'characters {\n{\n name = ' + b'9' * 4301 + b'\n ai = 10\n}\n}\n',
                                b'characters {\n{\n name = \'x\'\n ai = ' + b'9' * 4301 + b'\n}\n}\n',
                                b'prices {\n dino = ' + b'9' * 4301 + b'\n}\n',
                                b'prices {\n area = ' + b'9' * 4301 + b'\n}\n',
                                b'prices {\n acces = 0' + b'0' * 4300 + b'\n}\n',
                                b'characters {\n{\n name = ' + b'9' * 4300 + b'\n ai = ' + b'9' * 4300 + b'\n}\n}\nprices {\n dino = ' + b'0' * 4300 + b'\n weapon = -' + b'9' * 4300 + b'\n}\n']):
        check('project', game(base / f'conversion-{i}', script))
    # Every Latin-1 byte on both sides of an instruction word decides the Unicode boundary.
    def entry(label):
        return b'{\n name = ' + label + b'\n ai = 10\n}\n'
    labels = []
    for c in range(256):
        if c == 0x0a:
            continue
        quote = b"'" if c != 0x27 else b'"'
        labels += [quote + bytes([c]) + b'check' + quote, quote + b'Select' + bytes([c]) + quote, quote + b'a' + bytes([c]) + b'CLICK' + bytes([c]) + b'b' + quote]
    labels += [b"'uncheck'", b"'checked'", b"'unchecked'", b"'click.'", b"'clicks'", b"'check_'", b"'x-select'", b"'\xb2check'", b"'\xb5click'", b"'\xbfselect'",
               b"'\xd7check'", b"'\xf7click\xf7'", b"''", b"'\"'", b'123', b'check', b'abc', b'"un check"', b"'sel ect'", b"'Check'", b"'cHeCk'"]
    result = observed(game(base / 'labels', b'characters {\n' + b''.join(entry(l) for l in labels) + b'}\nprices {\n' + b' dino = 1\n' * 3 + b'}\n'))
    unusual = sum(d['code'] == 'unusual-label' for d in result['diagnostics'])
    assert 0 < unusual < len(labels) and result['diagnostics'][-2]['code'] == 'duplicate-ai' and len(result['diagnostics'][-2]['entries']) == len(labels)
    # Offsets and identities: first ai == 10 decides the dino price offset.
    for i, script in enumerate([b'characters {\n{\n ai = 11\n}\n{\n ai = 10\n}\n{\n ai = 10\n}\n}\nprices {\n dino = 1\n dino = 2\n dino = 3\n}\n',
                                b'characters {\n{\n ai = 11\n}\n{\n ai = 12\n}\n}\nprices {\n dino = 1\n dino = 2\n dino = 3\n}\n',
                                b'characters {\n{\n ai = 10\n}\n}\ncharacters {\n{\n ai = 10\n}\n}\ncharacters {\n}\nprices {\n dino = 1\n}\nprices {\n dino = 2\n dino = 3\n}\n',
                                b'weapons {\n{\n}\n{\n}\n}\nprices {\n weapon = 1\n weapon = 2\n weapon = 3\n acces = 1\n acces = 2\n acces = 3\n acces = 4\n}\n',
                                b'prices {\n acces = 1\n acces = 2\n acces = 3\n acces = 4\n acces = 5\n acces = 6\n}\n',
                                b'prices {\n acces = 1\n acces = 2\n acces = 3\n acces = 4\n acces = 5\n}\n',
                                b'prices {\n area = 1\n area = 2\n area = 3\n area = 4\n area = 5\n area = 6\n area = 7\n}\n',
                                b'prices {\n}\n', b'weapons {\n}\n', b'{\n ai = 10\n}\n', b'characters {\n{\n ai = 10\n ai = 10\n}\n}\n']):
        root = game(base / f'offset-{i}', script)
        for n in range(1, 8):
            write(root, f'HUNTDAT/MENU/TXT/EQUIP{n}.NFO', b'equip %d' % n)
            write(root, f'HUNTDAT/MENU/TXT/DINO{n}.TXM', b'dino %d' % n)
            write(root, f'HUNTDAT/MENU/TXT/WEAPON{n}.TXT', b'weapon %d' % n)
        for n in ('camoflag', 'radar', 'scent', 'double', 'tranq'):
            write(root, f'HUNTDAT/MENU/TXT/{n}.nfo', b'convention ' + n.encode())
        observed(root)
    # Slot six combinations and area directory shapes.
    for i, files in enumerate([('external.MAP', 'external.RSC'), ('AREA6.MAP', 'AREA6.RSC'), ('external.MAP', 'external.RSC', 'area6.map', 'area6.rsc'),
                               ('external.MAP', 'area6.rsc'), ()]):
        root = game(base / f'slot-six-{i}', b'prices {\n' + b' area = 1\n' * 6 + b'}\n')
        for f in files:
            write(root, 'HUNTDAT/AREAS/' + f)
        observed(root)
    areas_file = game(base / 'areas-file', b'prices {\n area = 1\n}\n')
    (areas_file / 'HUNTDAT/AREAS').rmdir()
    write(areas_file, 'HUNTDAT/AREAS', b'file')
    observed(areas_file)
    (areas_file / 'HUNTDAT/AREAS').unlink()
    (areas_file / 'HUNTDAT/MENU').rename(areas_file / 'HUNTDAT/menu-moved')
    observed(areas_file)
    # Ignored tails and raw observations still project; scripts keep all bytes.
    observed(game(base / 'tail', b'. prices { area = 1 }\n' + b'\xff' * 1000))
    random_roots(base)


def posix_links(base):
    unsafe = game(base / 'unsafe-link')
    outside = write(base, 'outside-script', MENU)
    (unsafe / 'HUNTDAT/_MENU.TXT').symlink_to(outside)
    check('project', unsafe)
    write(unsafe, 'HUNTDAT/_RES.TXT', b'weapons {}')
    check('project', unsafe)
    (unsafe / 'HUNTDAT/_MENU.TXT').unlink()
    (unsafe / 'HUNTDAT/_RES.TXT').unlink()
    (unsafe / 'HUNTDAT/_RES.TXT').symlink_to(outside)
    check('project', unsafe)
    inside = full_game(base, 'inside-link')
    (inside / 'HUNTDAT/_MENU.TXT').rename(inside / 'script-target')
    (inside / 'HUNTDAT/_MENU.TXT').symlink_to(inside / 'script-target')
    (inside / 'HUNTDAT/MENU/TXT/WEAPON1.TXT').rename(inside / 'text-target')
    (inside / 'HUNTDAT/MENU/TXT/WEAPON1.TXT').symlink_to(inside / 'text-target')
    (inside / 'HUNTDAT/MENU/TXT/DINO3.TXM').unlink()
    (inside / 'HUNTDAT/MENU/TXT/DINO3.TXM').symlink_to(base / 'outside-script')
    (inside / 'HUNTDAT/AREAS/linked').symlink_to(inside / 'HUNTDAT/MENU', target_is_directory=True)
    (inside / 'HUNTDAT/AREAS/link.map').symlink_to(inside / 'HUNTDAT/AREAS/area1.MAP')
    (inside / 'HUNTDAT/MENU/PICS/link.tga').symlink_to(inside / 'HUNTDAT/MENU/PICS/DINO1.TGA')
    (inside / 'HUNTDAT/AREAS/dangling.map').symlink_to(inside / 'absent')
    observed(inside)
    check('text_reference', inside, reference='HUNTDAT/MENU/TXT/WEAPON1.TXT')
    check('text_reference', inside, reference='HUNTDAT/MENU/TXT/DINO3.TXM')
    alias = base / 'root-alias'
    alias.symlink_to(inside, target_is_directory=True)
    observed(alias)
    linked_areas = game(base / 'linked-areas', b'prices {\n area = 1\n}\n')
    (linked_areas / 'HUNTDAT/AREAS').rmdir()
    write(base, 'areas-target/area1.map'); write(base, 'areas-target/area1.rsc')
    (linked_areas / 'HUNTDAT/AREAS').symlink_to(base / 'areas-target', target_is_directory=True)
    observed(linked_areas)
    (linked_areas / 'HUNTDAT/AREAS').unlink()
    (linked_areas / 'HUNTDAT/AREAS').symlink_to(linked_areas / 'HUNTDAT/MENU', target_is_directory=True)
    write(linked_areas, 'HUNTDAT/MENU/area1.map'); write(linked_areas, 'HUNTDAT/MENU/area1.rsc')
    observed(linked_areas)


def random_roots(base):
    rng = random.Random(20260921)
    names = ["'Alpha'", "''", "'Check here'", '12', 'abc', "'Select'", '"a\'b"', "'\xff\xd7 select'", "'uncheck-me'", '-0', '0007']
    references = ["'HUNTDAT/MENU/PICS/A.TGA'", '5', "''", "'../x'", "'HUNTDAT\\\\MENU'", "'HUNTDAT/MENU/PICS/a.tga'", "'huntdat'", "'C:x'"]
    prices = ['0', '10', '999', 'abc', "'5'", '-3', '1.5', '+8', '010', '-0']

    def script():
        lines = []

        def block(name, count):
            lines.append(name + ' {')
            for _ in range(count):
                lines.append('{')
                if rng.random() < 0.8:
                    lines.append(' name = ' + rng.choice(names))
                if rng.random() < 0.85:
                    lines.append(' ai = ' + rng.choice(['10', '11', '12', '9', "'10'", 'x', '010', '+13', '10\n ai = 11', '99', '-10']))
                for key in ('file', 'pic', 'thumbnail'):
                    if rng.random() < 0.3:
                        lines.append(f' {key} = ' + rng.choice(references))
                if rng.random() < 0.2:
                    lines.append(' addition { nested = 1 }')
                lines.append('}')
            lines.append('}')
        for _ in range(rng.randrange(0, 3)):
            block(rng.choice(['characters', 'CHARACTERS']), rng.randrange(0, 5))
        for _ in range(rng.randrange(0, 3)):
            block('weapons', rng.randrange(0, 4))
        if rng.random() < 0.3:
            block('areas', 1)
        if rng.random() < 0.5:
            lines.append('accessories { z = 1 }')
        for _ in range(rng.randrange(0, 3)):
            lines.append('prices {')
            for key in ('start', 'area', 'dino', 'weapon', 'acces'):
                for _ in range(rng.randrange(0, 8 if key != 'acces' else 7)):
                    lines.append(f' {key} = ' + rng.choice(prices))
            lines.append('}')
        for section in rng.sample(['hunterinfo', 'spawntable', 'oldambients', 'trophytable', 'other'], rng.randrange(0, 3)):
            lines.append(section + ' {\n}')
        if rng.random() < 0.15:
            lines.append('{')
        if rng.random() < 0.15:
            lines.append('}')
        return '\n'.join(lines).encode('latin1')

    def text():
        return bytes(rng.choice(b'ab \n\r\x0b\x0c\x1c\x1d\x1e\x85\xe9\xff') for _ in range(rng.randrange(0, 40)))

    for i in range(40):
        which = rng.choice(['menu', 'res', 'both'])
        root = game(base / f'random-{i}', script() if which != 'res' else None, script() if which != 'menu' else None)
        txt = root / 'HUNTDAT/MENU/TXT'
        for slot in range(1, 8):
            if rng.random() < 0.5:
                write(txt, f'AREA{slot}.TXT', text())
            for stem in ([f'area{slot}', 'external'] if slot == 6 else [f'area{slot}']):
                if rng.random() < 0.5:
                    write(root, f'HUNTDAT/AREAS/{stem}.MAP')
                if rng.random() < 0.5:
                    write(root, f'HUNTDAT/AREAS/{stem}.rsc')
            if rng.random() < 0.3:
                write(root, f'HUNTDAT/MENU/PICS/AREA{slot}.TGA')
        for n in range(1, 8):
            for template in ('DINO{}.TXM', 'WEAPON{}.TXT', 'EQUIP{}.NFO'):
                if rng.random() < 0.5:
                    write(txt, template.format(n), rng.choice([b'same', text()]))
            for template in ('DINO{}.TGA', 'WEAPON{}.TGA', 'EQUIP{}.TGA'):
                if rng.random() < 0.4:
                    write(root, 'HUNTDAT/MENU/PICS/' + template.format(n))
        for n in ('camoflag', 'radar', 'scent', 'double', 'tranq'):
            if rng.random() < 0.5:
                write(txt, n.upper() + '.NFO', rng.choice([b'same', text()]))
        for _ in range(rng.randrange(0, 6)):
            parent = rng.choice(['HUNTDAT/AREAS/', 'HUNTDAT/AREAS/sub/', 'HUNTDAT/MENU/', 'HUNTDAT/MENU/deep/deeper/'])
            name = rng.choice(['a', 'B', '\xe9', 'z']) + str(rng.randrange(10)) + rng.choice(['.map', '.MAP', '.c2map', '.tga', '.txt', '.nfo', '.txm', '.TXM', '', '.bin'])
            write(root, parent + name)
        observed(root, (rng.choice(HINTS),))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='c2-projection-') as temp:
        main(Path(temp))
    print(f'{CASES} catalog projection oracle cases; exact presentation bytes, typed handles and unchanged source trees')
