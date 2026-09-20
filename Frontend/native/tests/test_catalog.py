#!/usr/bin/env python3
"""Exact bytes/value/type oracles against unchanged lodge.catalog."""
import json
from pathlib import Path
import random
import subprocess
import sys
import unicodedata

FRONTEND = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(FRONTEND / 'tools'))
from generate_catalog_fixtures import oracle, request

DRIVER = str(Path(sys.argv[1]).resolve())
COUNT = 0
assert unicodedata.unidata_version == '15.0.0', 'pinned CPython 3.12 Unicode oracle required'
assert sys.get_int_max_str_digits() == 4300, 'pinned default integer conversion limit required'


def check_many(requests, expected=None):
    global COUNT
    if expected is None:
        expected = [oracle(r) for r in requests]
    data = ''.join(json.dumps(r, ensure_ascii=True) + '\n' for r in requests).encode('ascii')
    process = subprocess.run([DRIVER], input=data, capture_output=True, timeout=180)
    assert process.returncode == 0, (process.returncode, process.stderr)
    lines = process.stdout.splitlines()
    assert len(lines) == len(requests), (len(lines), len(requests), process.stderr)
    for i, (r, want, line) in enumerate(zip(requests, expected, lines)):
        got = json.loads(line)
        # Exact presentation bytes also establish key order, value kinds and LF.
        assert got == want and json.dumps(got, ensure_ascii=True) == json.dumps(want, ensure_ascii=True), (COUNT + i, repr(r)[:300], repr(want)[:800], repr(got)[:800])
    COUNT += len(requests)


goldens = json.loads((FRONTEND / 'tests/catalog/golden.json').read_bytes())
check_many([v['request'] for v in goldens], [v['expected'] for v in goldens])
rs = []
for byte in range(256):
    b = bytes([byte])
    for data in [b'a' + b + b'b=1\n', b + b'{x=2}\n', b'x="' + b + b'"\n',
                 b'raw' + b + b'//tail\ny=3\n', b'\'' + b + b'\nq=4\n']:
        rs.append(request(data, source='byte\x00\ud800/\udfff\U0001f996'))
for data in [b'a\nb\nc', b'a=b\nc\nd', b'=\n', b'a=\n', b'=x=y\n', b'}\r}\n{\n',
             b'"x\ny"', b'\'"x\'"', b'///comment\r{\nq=1', b'a//comment\r{\nq=1',
             b'.x { }', b'x. y', b'. { { {', b'{ . } . ignored', b'x \n .\ny=1',
             b'\r\v\f\x85\xa0name\r\v\f\x85\xa0{ x=1\r y=2\n }']:
    rs.append(request(data))
for n in [126, 127, 128, 129]:
    for closed in [False, True]:
        rs.append(request(b'{' * n + b'a=1\n' + (b'}' * n if closed else b'')))
# Every Unicode decimal block and its immediate neighbors, all ten digit values.
starts = [cp for cp in range(0x110000) if unicodedata.decimal(chr(cp), -1) == 0]
for cp in starts:
    digits = ''.join(chr(cp + i) for i in range(10))
    for raw in [digits, '+' + digits, '-' + digits, chr(cp - 1), chr(cp + 10)]:
        rs.append({'op': 'scalar', 'raw': raw})
for n in [4299, 4300, 4301]:
    for digit in ['0', '9', '٠', '９', '\U0001d7ce']:
        for sign in ['', '+', '-']:
            rs.append({'op': 'scalar', 'raw': sign + digit * n})
    for suffix in ['x', '²', ' ', '\n', '_1', '\ud800']:
        rs.append({'op': 'scalar', 'raw': '9' * n + suffix})
    for quote in ['"', "'"]:
        rs.append({'op': 'scalar', 'raw': quote + '9' * n + quote})
    data = b'x=' + b'9' * n + b'\n'
    rs += [request(data), request(data, op='attribute', node_path=[], key='x'),
           request(data + b'X=2\n', op='attribute', node_path=[], key='x')]
for raw in ['', '-', '+', ' -1', '1 ', '\n1', '1\n', '²', 'Ⅻ', '½', '1.0', '1e2', '-000', '＋1', '−1', '１２\x851', '١２3', '\ud800', '"\n123\n"', "'1\""]:
    rs.append({'op': 'scalar', 'raw': raw})
for stored, requested in [(b'Stra\xdfe', 'strasse'), (b'\xb5', 'μ'), (b'\xc0', 'à'), (b'NAME', 'name')]:
    data = stored + b'{ ' + stored + b'=12\n raw\n child{} } ' + stored + b'{}'
    for key in [requested, stored.decode('latin1'), requested.upper(), 'missing']:
        rs += [request(data, op='blocks', name=key), request(data, op='attribute', node_path=[0], key=key)]
rs += [request(b'} A { name="kept"\n child { n=-0000007\n } raw\n } B{\n', source='opaque\ud800\0', op='typed'),
       request(b'{' * 127 + b'x=9\n' + b'}' * 127, op='typed')]
rng = random.Random(2081)
alphabet = b'abcXYZ012 .{}=\n\r\t\x85\xa0;#\'"/\xff\x00'
for _ in range(450):
    rs.append(request(bytes(rng.choice(alphabet) for _ in range(rng.randrange(0, 240)))))
# Keep each child bounded; process multiple independent line requests per child.
for i in range(0, len(rs), 100):
    check_many(rs[i:i + 100])
# Runtime-created bounds, never megabytes of checked-in fixture data.
limit = 8 * 1024 * 1024
for n in [limit - 1, limit, limit + 1]:
    check_many([request(b'#' + b'x' * (n - 1))])
# The full input (including data ignored after '.') participates in the digest.
check_many([request(b'. ' + b'\xff' * (limit - 2))])
# Long raw assignment parses successfully; conversion is separate and fallible.
check_many([request(b'x=' + b'9' * 100000 + b'\n'),
            request(b'x=' + b'9' * 100000 + b'\n', op='attribute', node_path=[], key='x')])
check_many([request(b'a\n' * 1000 + b'{ x=1\n }')])
print(f'{COUNT} catalog oracle cases passed; exact presentation bytes, arbitrary integers, owned raw trees')
