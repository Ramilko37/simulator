"""Run the development-only real-Chaos acceptance harness; no third-party packages.

python3 Unreal/DroneTrainer/Tests/run_stage02.py
python3 Unreal/DroneTrainer/Tests/run_stage02.py --render 1280 720
Engine may be overridden with --engine. Build current Source first, close the editor.
Fixed simulation cadence is intentional; this is not a renderer FPS benchmark.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--engine', default='/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor')
parser.add_argument('--render', nargs=2, type=int, metavar=('WIDTH', 'HEIGHT'))
args = parser.parse_args()
project = Path(__file__).resolve().parents[1]
saved = project/'Saved'
results = []
for fps in ([60] if args.render else [30, 60, 120]):
    suffix = f'{args.render[0]}x{args.render[1]}' if args.render else str(fps)
    log_path = saved/f'Stage02-{suffix}.log'
    result_path = saved/f'Stage02-{fps}-result.txt'
    result_path.unlink(missing_ok=True)
    command = [args.engine, str(project/'DroneTrainer.uproject'), '-game', '-nosound', '-unattended',
               '-Stage02AutoExit', f'-ExecCmds=DroneTrainer.CheckFlight {fps}', f'-abslog={log_path}']
    if args.render:
        command += ['-windowed', '-ForceRes', f'-ResX={args.render[0]}', f'-ResY={args.render[1]}', '-Stage02Screenshots']
    else:
        command += ['-nullrhi']
    with (saved/f'Stage02-{suffix}-process.log').open('w') as output:
        run = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=240)
    assert run.returncode == 0, (suffix, run.returncode)
    log = log_path.read_text(errors='replace')
    assert not re.search(r'\b(?:Error|Fatal):|Traceback \(most recent call last\)', log), log_path
    assert 'E2_FLIGHT_CHECK_COMPLETE' in log and 'E2_CHECK ' in log
    assert 'E2_CHECK' not in '\n'.join(line for line in log.splitlines() if 'FAIL' in line), log_path
    assert 'DroneTrainer: editor Python startup disabled in game mode' in log
    assert 'Game engine shut down' in log
    data = dict(line.split('=', 1) for line in result_path.read_text().splitlines())
    assert data['status'] == 'passed', data
    samples = list(csv.DictReader((saved/f'Stage02-{fps}-samples.csv').open()))
    assert len(samples) >= fps*(7 if args.render else 69)
    if not args.render: assert float(data['brake_time_s']) > 0
    if args.render:
        for view in ['fpv', 'chase']:
            assert (saved/f'Stage02-{suffix}-{view}.png').stat().st_size > 1000
    results.append(dict(run=suffix, **data, sample_count=len(samples)))
    print(json.dumps(results[-1]), flush=True)
(saved/f'Stage02-{"render-"+suffix if args.render else "physics"}-summary.json').write_text(json.dumps(results, indent=2))
