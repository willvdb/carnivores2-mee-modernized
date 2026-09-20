#!/usr/bin/env python3
"""Compact authored catalog goldens from unchanged Python; no game assets."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lodge import catalog
from lodge.store import FrontendError


def oracle(r):
    try:
        op = r['op']
        if op == 'scalar':
            value = catalog.scalar(r['raw'])
        else:
            script = catalog.parse_script(bytes.fromhex(r['bytes_hex']), r['source'])
            if op in ('parse', 'typed'):
                value = script
            elif op == 'blocks':
                value = catalog.blocks(script, r['name'])
            elif op == 'attribute':
                node = script['tree']
                for index in r['node_path']:
                    node = node['children'][index]
                value = catalog.attribute(node, r['key'])
            else:
                raise AssertionError(op)
        encoded = json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False) + '\n'
        return {'ok': True, 'value': value, 'json': encoded}
    except FrontendError as e:
        return {'ok': False, 'error': str(e), 'kind': 'catalog'}
    except ValueError as e:
        return {'ok': False, 'error': str(e), 'kind': 'scalar-conversion'}


def request(data, source='opaque/source', op='parse', **kwargs):
    return {'op': op, 'bytes_hex': data.hex(), 'source': source, **kwargs}


def cases():
    inputs = [b'', b'//comment\n#comment\r\n;comment\n', b'a//b c//d\nx=1//2\ny=2 //skip\n',
              b'"matched quote" \'next\' { name="A B"\n }', b'"unmatched\n\'other\rquote\n"{x=3}\n',
              b'pending\non next\r\nline { bare\nwords\n a = b = c\n=\n}',
              b'}\n}\na{ b{ x=1', b'a=1\r\nb=2\rc=3', b'root . ignored { invalid }\xff\x00',
              b'a { . x=1\n } . }', b'\x1c\x1d\x1e\x1f\x85\xa0 a\x00b=\xff\n',
              b'\"a\rb\"=\'\xff\x85\xa0\'\n', b'a = b { c = d }', b'a={}=}']
    result = [request(x, source='opaque\x00\ud800/\U0001f996') for x in inputs]
    for raw in ['', "''", '"123"', "'a\nb'", '123', '+00001', '-0', '-00012', '١٢３', '\U0001d7ce\U0001d7d7', '²', ' 1', '1\n', '+', '--1', '1_2', "'1\"", '\udfff']:
        result.append({'op': 'scalar', 'raw': raw})
    data = b'Stra\xdfe{ NAME=first\n name=second\n x=+0007\n \xb5=9\n raw tokens\n child{} } STRASSE{}'
    result += [request(data, op='blocks', name=n) for n in ['strasse', 'STRASSE', 'Straße', 'missing']]
    result += [request(data, op='attribute', node_path=[0], key=k) for k in ['name', 'NAME', 'x', 'μ', 'µ', 'missing']]
    result.append(request(b'} outer { child { x=1\n } bare } tail {', op='typed'))
    return result


def generate():
    return json.dumps([{'request': r, 'expected': oracle(r)} for r in cases()], indent=2, ensure_ascii=True) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    path = Path(__file__).resolve().parents[1] / 'tests/catalog/golden.json'
    data = generate()
    if args.check:
        if path.read_bytes() != data.encode('ascii'):
            raise SystemExit('Catalog golden differs from unchanged Python')
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode('ascii'))
