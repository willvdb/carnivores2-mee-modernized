"""Worktree-local imports for verification tools; never used by the product.

Entry points outside tools/ add this directory using their own __file__ first.
Reference test modules are deliberately NOT on this shared import path.
"""
from pathlib import Path
import sys

FRONTEND = Path(__file__).resolve().parents[1]
REFERENCE = FRONTEND / 'reference' / 'python'
sys.path[:0] = [str(REFERENCE), str(FRONTEND / 'tests')]
