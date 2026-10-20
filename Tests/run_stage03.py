"""Run E3 Automation predicates/state fixtures, separately from physical/manual flights."""
import json
import os
from pathlib import Path
import re
import signal
import subprocess

root = Path(__file__).resolve().parents[3]
project = root / 'Unreal/DroneTrainer/DroneTrainer.uproject'
evidence = root / 'docs/prototype-level/evidence/stage03'
evidence.mkdir(exist_ok=True)
engine = '/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor'
existing = subprocess.run(['pgrep', '-x', 'DroneTrainer'], capture_output=True)
assert existing.returncode == 1, 'Close existing game first (or process inventory unavailable)'
existing = subprocess.run(['pgrep', '-x', 'UnrealEditor'], capture_output=True)
assert existing.returncode == 1, 'Close existing editor first (PIE state otherwise unknown)'
log = evidence / 'automation.log'
cmd = [engine, str(project), '-game', '-nullrhi', '-nosound', '-unattended',
       '-ExecCmds=Automation RunTests DroneTrainer.Gates+DroneTrainer.Lesson',
       '-TestExit=Automation Test Queue Empty', f'-abslog={log}']
with (evidence / 'automation-process.log').open('w') as output:
    process = subprocess.Popen(cmd, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        code = process.wait(timeout=175)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=4)
        raise SystemExit('TIMEOUT after 175 seconds; own process group stopped. Do not retry automatically.')
text = log.read_text(errors='replace')
results = re.findall(r'Test Completed\. Result=\{(\w+)\} Name=\{([^}]+)\}', text)
print(json.dumps({'exit': code, 'tests': results, 'scope': 'state fixtures, not physical/manual flights'}, ensure_ascii=False))
assert code == 0 and len(results) == 2 and all(r[0] == 'Success' for r in results), log
assert not re.search(r'\b(?:Error|Fatal):|Handled ensure', text), log
assert text.count('LESSON_FIXTURE_COMPLETE') == 3

# A separate real Chaos run checks pre-impact sampling and thrust/contact agreement.
contact_log = evidence / 'contacts.log'
cmd = [engine, str(project), '-game', '-nullrhi', '-nosound', '-unattended',
       '-Stage03AutoExit', '-ExecCmds=DroneTrainer.CheckLessonContacts', f'-abslog={contact_log}']
with (evidence / 'contacts-process.log').open('w') as output:
    process = subprocess.Popen(cmd, stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        code = process.wait(timeout=175)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGTERM)
        process.wait(timeout=4)
        raise SystemExit('TIMEOUT after 175 seconds; own process group stopped. Do not retry automatically.')
text = contact_log.read_text(errors='replace')
import shutil
shutil.copyfile(project.parent / 'Saved/Stage03-contacts.csv', evidence / 'contacts.csv')
print('Chaos contacts exit', code)
assert code == 0 and 'LESSON_CONTACT_COMPLETE PASS' in text, contact_log
assert text.count('LESSON_CONTACT_CHECK') == 4 and 'FAIL' not in text, contact_log
assert not re.search(r'\b(?:Error|Fatal):|Handled ensure', text), contact_log
