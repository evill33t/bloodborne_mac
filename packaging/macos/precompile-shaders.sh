#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
if [[ $(uname -s) != Darwin ]]; then
    echo 'Shader precompilation requires macOS.' >&2; exit 1
fi
compiler=${BB_MAC_RUNTIME_DIR:-out/macos-runtime}/bb-macos-precompile-shaders
if [[ ! -x $compiler ]]; then
    echo 'Build the runtime first: bash packaging/macos/build-runtime.sh' >&2; exit 2
fi
export ROSETTA_ADVERTISE_AVX=1
exec "$compiler" "$@"
