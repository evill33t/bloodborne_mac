#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run bounded CPU timing comparisons and a required negative control. No game launch."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def run(binary, elf, *, mutate=False):
    command = [str(binary), '--elf', str(elf)] if elf else [str(binary), '--self-test']
    if mutate:
        command.append('--mutate')
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=30,
                                env=dict(os.environ, ROSETTA_ADVERTISE_AVX='1'))
    except (OSError, subprocess.TimeoutExpired) as error:
        return dict(complete=False, error=str(error))
    try:
        evidence = json.loads(result.stdout)
    except json.JSONDecodeError:
        return dict(complete=False, exit_code=result.returncode, error=result.stderr.strip() or 'Missing verification JSON')
    evidence['exit_code'] = result.returncode
    evidence['complete'] = result.returncode in (0, 1) and bool(evidence.get('cases')) and evidence.get('step_checks', 0) > 0
    if result.stderr:
        evidence['diagnostics'] = result.stderr.strip()
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'out/macos-runtime/bb-macos-timing-tests')
    parser.add_argument('--elf', type=Path, help='Also compare the original 1.09 limiter from a read-only ELF')
    parser.add_argument('--report', type=Path, help='Write machine-readable evidence atomically')
    args = parser.parse_args()
    baseline = run(args.binary.resolve(), args.elf)
    negative = run(args.binary.resolve(), args.elf, mutate=True)
    passed = baseline.get('complete') and baseline.get('exit_code') == 0 and baseline.get('failed') == 0
    detected = (negative.get('complete') and negative.get('exit_code') == 1 and
                negative.get('failed', 0) == len(negative.get('cases', [])) and
                all(not case.get('passed') for case in negative.get('cases', [])))
    report = dict(schema=1, passed=bool(passed and detected), baseline=baseline, negative_control=negative,
                  scope='CPU fixtures only; no game startup, GPU, pacing measurements, or whole-game verification')
    text = json.dumps(report, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode='w', dir=args.report.parent, delete=False) as f:
            temporary = Path(f.name)
            f.write(text)
        temporary.replace(args.report)
    print(text, end='')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
