#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
if [[ $(uname -s) != Darwin ]]; then
    echo 'Shader precompilation requires macOS.' >&2; exit 1
fi
case ${BB_MAC_CPU:-rosetta} in
    rosetta)
        compiler=${BB_MAC_RUNTIME_DIR:-out/macos-runtime}/bb-macos-precompile-shaders
        export ROSETTA_ADVERTISE_AVX=1
        ;;
    bbcpu) compiler=${BB_MAC_NATIVE_RUNTIME_DIR:-out/macos-native-runtime}/bb-macos-precompile-shaders ;;
    *) echo 'BB_MAC_CPU must be rosetta or bbcpu.' >&2; exit 2 ;;
esac
if [[ ! -x $compiler ]]; then
    echo 'Build the selected runtime before precompiling shaders.' >&2; exit 2
fi
exec "$compiler" "$@"
