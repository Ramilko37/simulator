"""Check standalone, editor and Python commandlet smoke-test logs (stdlib only).

Usage: python3 check_standalone_startup.py standalone.log editor.log commandlet.log
"""
import json
from pathlib import Path
import re
import sys

assert len(sys.argv) == 4, __doc__
game, editor, commandlet = [Path(p).read_text(errors="replace") for p in sys.argv[1:]]
for name, log in zip(("standalone", "editor", "commandlet"), (game, editor, commandlet)):
    assert not re.search(r"\b(?:Error|Fatal):|Traceback \(most recent call last\)", log), name

assert "DroneTrainer: editor Python startup disabled in game mode" in game
assert "Python disabled via CVar 'Engine.Python.IsEnabledByDefault'" in game
assert "LoadMap: /Game/TrainingField/Maps/TrainingField" in game
assert "Game class is 'DroneTrainerGameMode'" in game
assert "Engine is initialized" in game and "Game engine shut down" in game
assert "-DisablePython" not in game and "-DisablePlugins" not in game
for log in (editor, commandlet):
    assert "Python enabled via CVar 'Engine.Python.IsEnabledByDefault'" in log
    assert "editor Python startup disabled in game mode" not in log
    assert "Registering Toolset editor_toolset.toolsets.scene.SceneTools" in log
assert "Engine is initialized" in editor
assert "E0_EDITOR_PYTHON_COMMANDLET_PASS" in commandlet
print(json.dumps({"standalone": "passed", "editor": "passed", "python_commandlet": "passed"}))
