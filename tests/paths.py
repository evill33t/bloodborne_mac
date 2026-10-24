"""Repository paths for the Python tests (run: python3 -m unittest discover -s tests)."""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUNTIME_DIR = ROOT / Path(os.environ.get('BB_MAC_RUNTIME_DIR', 'out/macos-runtime'))
if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))
