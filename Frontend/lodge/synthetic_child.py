"""Trusted process fixture. Only mutates state in its disposable explicit cwd.

No engine, content loader, subprocess, native save discovery or profile creation.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import struct
import sys
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scenario', required=True)
    parser.add_argument('--slot', type=int, required=True, choices=range(8))
    parser.add_argument('--literal', required=True)
    args = parser.parse_args()
    print(json.dumps({'cwd': os.getcwd(), 'argv': sys.argv[1:], 'pid': os.getpid()}), flush=True)
    print('synthetic fixture stderr', file=sys.stderr, flush=True)
    sav = Path('state') / f'trophy{args.slot:02d}.sav'
    sab = sav.with_suffix('.sab')
    scenario = args.scenario
    if scenario in ('sav', 'pair', 'changed-nonzero', 'missing-sab'):
        value = bytearray(sav.read_bytes())
        struct.pack_into('<i', value, 132, 175)
        sav.write_bytes(value)
    if scenario == 'pair':
        value = bytearray(sab.read_bytes())
        value[-1] ^= 1
        sab.write_bytes(value)
    if scenario == 'corrupt-sav':
        sav.write_bytes(sav.read_bytes()[:20])
    if scenario == 'corrupt-sab':
        sab.write_bytes(sab.read_bytes()[:20])
    if scenario == 'missing-sab':
        sab.unlink()
    if scenario == 'deleted-sav':
        sav.unlink()
    if scenario == 'extra':
        sav.with_suffix('.extra').write_bytes(b'unknown companion, preserve me')
    if scenario == 'registration':
        value = bytearray(sav.read_bytes())
        struct.pack_into('<i', value, 128, (args.slot + 1) % 8)
        sav.write_bytes(value)
    if scenario == 'hang':
        time.sleep(60)
    if scenario == 'terminated':
        os.kill(os.getpid(), signal.SIGTERM)
    if scenario == 'logs':
        sys.stdout.write('x' * 200000)
        sys.stderr.write('y' * 200000)
    return 7 if scenario in ('nonzero', 'changed-nonzero') else 0


if __name__ == '__main__':
    raise SystemExit(main())
