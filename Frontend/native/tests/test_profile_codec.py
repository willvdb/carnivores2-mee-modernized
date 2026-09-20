"""Pure byte API versus unchanged Python + original portable codec helper."""
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from lodge.profiles import codec_inspect

driver, probe = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
os.environ.pop('C2_PROFILE_PROBE', None)
count = 0
with tempfile.TemporaryDirectory() as directory:
    # A selected embedded codec must not consult a helper path/environment or cwd.
    env = {**os.environ, 'C2_PROFILE_PROBE': str(Path(directory) / 'must-not-run'), 'PATH': ''}
    subprocess.run([driver], check=True, cwd=directory, env=env)

    def check(blob, kind='sav', native=True, dialect='unknown'):
        global count
        expected = codec_inspect(blob, kind, probe if native else None, dialect)
        exact = (json.dumps(expected, indent=2) + '\n').encode('ascii')
        process = subprocess.run([driver, kind, 'native' if native else 'unavailable', dialect],
                                 input=blob, capture_output=True, cwd=directory, env=env, check=True)
        assert process.stdout == exact, (count, len(blob), kind, native, dialect, process.stderr)
        count += 1

    for kind in ('sav', 'sab', 'SAV', '', 'save', 'room', 'sav ', 'Sav'):
        for length in (0, 1, 1659, 1660, 1661, 7175, 7176, 7177):
            for native in (False, True):
                for dialect in ('unknown', 'iceage-triassic', 'Iceage-triassic', 'iceage-triassic ', ''):
                    check(bytes(length), kind, native, dialect)
    rng = random.Random(0x1B2)
    for size, kind in ((1660, 'sav'), (7176, 'sab')):
        for blob in (bytes(size), b'\xff' * size, bytes(i % 256 for i in range(size))):
            check(blob, kind)
        for _ in range(32):
            check(rng.randbytes(size), kind)
        # Every word/position varies, including options omitted from the projection.
        bits = (0, 0x7fffffff, 0x80000000, 0xffffffff, 0x7f800000, 0xff800000,
                0x7fc00001, 0x7f800001, 0xffc12345, 1, 0x3f800000)
        for shift in range(len(bits)):
            blob = b''.join(struct.pack('<I', bits[(i + shift) % len(bits)]) for i in range(size // 4))
            check(blob, kind)
        # Unique index in every raw word detects swaps across fields/items/reserved.
        check(b''.join(struct.pack('<I', 0x80000000 + i) for i in range(size // 4)), kind)
    # Every Latin-1 non-NUL byte appears in a decoded name; NUL at every position
    # proves that display truncates while all trailing name bytes remain in hex.
    for start in (1, 128):
        name = bytes((start + i) % 256 for i in range(128))
        check(name + bytes(1532))
    for position in range(128):
        name = bytearray([255] * 128)
        name[position] = 0
        check(bytes(name) + bytes(1532))
    check(bytes(range(128)) + b'\xff' * 1532)
    assert list(Path(directory).iterdir()) == [], 'pure inspection wrote files'
print(f'{count} exact profile codec oracle comparisons; typed ownership/API checks passed')
