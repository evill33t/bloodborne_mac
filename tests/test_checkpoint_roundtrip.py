#!/usr/bin/env python3
"""Run a provider's save and repeated restores in independent processes."""
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="bb-checkpoint-roundtrip-") as directory:
        checkpoint = str(Path(directory) / "fixture.bbstate")
        for operation in ("--save", "--restore", "--restore"):
            result = subprocess.run([executable, operation, checkpoint], timeout=10)
            if result.returncode == 77:
                return 77
            result.check_returncode()
    return 0


if __name__ == "__main__":
    sys.exit(main())
