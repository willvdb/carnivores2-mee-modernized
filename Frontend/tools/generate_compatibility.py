"""Authored fixtures, generated only by the unchanged Python reference.

Run without flags to regenerate; --check compares committed bytes. No game assets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lodge import acceptance, discovery, session_io, store

DESTINATION = Path(__file__).resolve().parents[1] / 'tests/compatibility/golden.json'


def record(name, source):
    result = {'name': name, 'input_hex': source.encode('utf-8').hex()}
    try:
        value = json.loads(source, object_pairs_hook=store._unique_object)
    except (ValueError, RecursionError):
        result['parse_error'] = True
        return result
    result['kind'] = type(value).__name__
    compact = json.dumps(value, ensure_ascii=True, separators=(',', ':')).encode()
    result['compact_hex'] = compact.hex()
    try:
        payload = session_io.encode(value)
    except ValueError:
        result['journal_error'] = True
    else:
        result['journal_hex'] = payload.hex()
        result['journal_sha256'] = acceptance.candidate_digest(value)
        assert result['journal_sha256'] == hashlib.sha256(payload).hexdigest()
    return result


def generate():
    cases = [
        ('object-order', '{"z":{},"a":[1,{},[],[true,false,null]],"m":{"z":2,"a":1}}'),
        ('retained-unknown', '{"schema_version":4,"unknown":{"nested":[{"big":184467440737095516160001,"float":900.0,"negative_zero":-0.0,"text":"é🦖"}],"empty":[],"absent":null}}'),
        ('unicode-order', '{"\\ud800":1,"\\ue000":2,"🦖":3,"\\ud83e\\udd96":4}'),
        ('unicode-key-order', '{"\\ue000":1,"🦖":2,"a":3,"é":4,"\\ud800":5}'),
        ('escapes', '"\\u0000\\b\\f\\n\\r\\t\\u001f \\/\\\\\\\"é漢🦖\\u007f\\ud800x\\udfff"'),
        ('surrogate-pairs', '["\\ud83e\\udd96","🦖","\\ud800\\ud800\\udc00","\\udc00\\ud800"]'),
        ('duplicate-top', '{"a":1,"a":2}'),
        ('duplicate-nested', '{"outer":[{"x":1,"x":2}]}'),
        ('duplicate-escaped', '{"a":1,"\\u0061":2}'),
        ('duplicate-unicode', '{"🦖":1,"\\ud83e\\udd96":2}'),
        ('whitespace', ' \r\n { "b" : 2, "a" : 1 } \t'),
        ('numeric-kinds', '[true,false,0,1,-0,900,900.0,-0.0,1e0,1E+00,1000000000000000100.0,9007199254740993,18446744073709551616,-18446744073709551617]'),
        ('float-thresholds', '[1e-7,1e-6,1e-5,1e-4,1e15,1e16,1e17,1e20,1e21,1.2345678901234567,5e-324,2.2250738585072014e-308,1.7976931348623157e308]'),
        ('float-underflow', '[1e-9999,-1e-9999,2.4703282292062327e-324,2.4703282292062328e-324,0e9999,-0e9999]'),
        ('nonfinite', '[NaN,Infinity,-Infinity,1e9999,-1e9999]'),
        ('huge-exponent', '[1e' + '9' * 80 + ',1e-' + '9' * 80 + ']'),
        ('large-integer', '1' + '234567890' * 200),
    ]
    for value in ['true', 'false', '1', '1.0', '-0', '-0.0', 'null', '[]', '{}', '""']:
        cases.append(('scalar-' + value, value))
    for index, value in enumerate(['01', '+1', '1.', '.1', '1e', '--1', '[1,]', '{"x":1,}', '{1:2}', '"\n"', '"\\x00"', '"\\u12xz"', 'null true', 'nan', '-NaN', '\ufeff{}']):
        cases.append((f'invalid-{index}', value))
    # Fixed binary64 samples exercise formatting independently of source spelling.
    randomizer = random.Random(0xC2)
    for index in range(256):
        value = struct.unpack('>d', randomizer.getrandbits(64).to_bytes(8, 'big'))[0]
        cases.append((f'binary64-{index}', repr(value) if value == value and abs(value) != float('inf') else 'NaN'))

    # Capture the payload from the REAL fingerprint function, not a second encoder.
    # The tiny files are original test strings, never proprietary game content.
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        members = {'z.bin': b'authored fixture\x00\xff', 'a/é.bin': b'abc', '🦖.bin': b''}
        for name, content in members.items():
            path = root / 'HUNTDAT' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        original = json.dumps
        with patch.object(discovery.json, 'dumps', wraps=original) as observed:
            fingerprint = discovery.fingerprint(root)
        assert observed.call_count == 1
        arguments, keywords = observed.call_args
        payload = original(*arguments, **keywords).encode()
        entries = arguments[0]
        assert hashlib.sha256(payload).hexdigest() == fingerprint['sha256']
        fingerprint_case = {'entries': list(reversed(entries)), 'payload_hex': payload.hex(),
                            'sha256': fingerprint['sha256']}
    result = {'oracle_baseline': 'e742fb7ab85ba0c571d50e45ff0a410eb2a444dd',
              'cases': [record(*case) for case in cases], 'fingerprint': fingerprint_case}
    return (json.dumps(result, indent=2, ensure_ascii=True) + '\n').encode()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    options = parser.parse_args()
    generated = generate()
    if options.check:
        if DESTINATION.read_bytes() != generated:
            sys.exit('Python oracle differs from committed golden corpus; review compatibility before regeneration')
        print('Python compatibility corpus matches')
    else:
        DESTINATION.write_bytes(generated)
        print(f'Wrote {DESTINATION}')
