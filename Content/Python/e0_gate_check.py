"""Compatibility entry point after E1: validate the new route, not obsolete E0 coordinates.

Run in live PIE/SIE. The replacement retains physical frame probes and expands
25 old sweeps to 45 oriented sweeps plus terrain, pads and corridor checks.
"""
from pathlib import Path
import runpy
import unreal
runpy.run_path(str(Path(unreal.Paths.project_content_dir()) / "Python/stage01_check.py"))
