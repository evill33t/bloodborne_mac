#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
exec bash packaging/macos/build.sh "$@"
