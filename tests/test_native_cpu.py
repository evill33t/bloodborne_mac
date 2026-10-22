#!/usr/bin/env python3
"""Compare synthetic x86 reference results with native arm64 interpreter and JIT."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    reference, runner = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
    env = dict(os.environ, ROSETTA_ADVERTISE_AVX='1')
    with tempfile.TemporaryDirectory(prefix='bb-native-cpu-') as directory:
        expected = Path(directory) / 'expected.txt'
        with expected.open('wb') as output:
            subprocess.run([reference], env=env, stdout=output, check=True)
        # The reference alone uses Rosetta. Both runners below are arm64.
        for jit in ('0', '1'):
            print(f'Native arm64 BB_JIT={jit}', flush=True)
            subprocess.run([runner, reference, str(expected)],
                           env=dict(env, BB_JIT=jit), check=True)


if __name__ == '__main__':
    main()
