"""CTest entry point: the production codec is mandatory, never an optional skip."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

probe = os.environ.get('C2_PROFILE_PROBE')
if not probe or not Path(probe).is_file():
    raise SystemExit('C2_PROFILE_PROBE must name the built c2-profile-probe')
# Check the real executable and codec before test discovery evaluates skip guards.
result = subprocess.run([probe, 'save'], input=bytes(1660), capture_output=True, timeout=10)
if result.returncode or not json.loads(result.stdout).get('codec_roundtrip_exact'):
    raise SystemExit('c2-profile-probe failed its complete synthetic SAV round trip')
suite = unittest.defaultTestLoader.discover(str(Path(__file__).parent / 'tests'))
result = unittest.TextTestRunner(verbosity=2).run(suite)
probe_skips = [reason for _, reason in result.skipped if 'codec' in reason or 'probe' in reason]
if probe_skips:
    raise SystemExit('codec-dependent tests were skipped: ' + repr(probe_skips))
sys.exit(0 if result.wasSuccessful() else 1)
