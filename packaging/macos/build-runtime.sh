#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
case ${1:-} in
    ''|--test) ;;
    *) echo 'Usage: bash packaging/macos/build-runtime.sh [--test]' >&2; exit 2 ;;
esac
build_dir=${BB_MAC_RUNTIME_DIR:-out/macos-runtime}
cmake -S packaging/macos/runtime -B "$build_dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$build_dir" --parallel "${BB_BUILD_JOBS:-$(sysctl -n hw.logicalcpu)}"
if [[ ${1:-} == --test ]]; then
    ROSETTA_ADVERTISE_AVX=1 ctest --test-dir "$build_dir" --output-on-failure
fi
echo "Built experimental x86-64 game runtime with direct Metal: $build_dir/bb-macos-game"
