"""Reader for Anymaker's compiled geocode program (bin/game.gcl), for the tools in this folder.

The reader itself lives in sdk/experimental/tools/gcl.py, which reads both the full-metadata
format (up to 0.1.28) and the stripped format 0.2.0 ships (names recovered from the previous
SDK through ANYMAKER_GCL_NAMES). See that file's docstring and gcl_format.md for the layouts.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'sdk', 'experimental', 'tools'))
from gcl import REL_KINDS, Function, Program, Reader, Type  # noqa: E402,F401
