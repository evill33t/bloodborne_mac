#!/usr/bin/env python3
"""Run the synthetic guest's save and restore in different fresh processes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    env = dict(os.environ, ROSETTA_ADVERTISE_AVX="1")
    with tempfile.TemporaryDirectory(prefix="bb-checkpoint-process-") as directory:
        checkpoint = Path(directory) / "fixture.bbstate"
        for save, restore in (("--proxy-save", "--proxy-restore"),
                              ("--before-save", "--before-restore"),
                              ("--save", "--restore")):
            for operation in (save, restore, restore):
                subprocess.run([executable, operation, str(checkpoint)], check=True,
                               env=env, timeout=10)
        original = checkpoint.read_bytes()
        for damaged in (b"", original[:-1], original[:200] + bytes([original[200] ^ 1]) + original[201:]):
            checkpoint.write_bytes(damaged)
            result = subprocess.run([executable, "--restore", str(checkpoint)],
                                    env=env, timeout=10, capture_output=True)
            assert result.returncode == 1, result
        print("Fresh-process CPU checkpoint passed at direct, before-import and after-import boundaries, including repeat restores and invalid-file rejection.")


if __name__ == "__main__":
    main()
