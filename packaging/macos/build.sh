#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
if [[ $(uname -s) != Darwin ]]; then
    echo 'The direct Metal targets require macOS and Xcode command-line tools.' >&2
    exit 1
fi
for tool in cmake xcrun; do
    command -v "$tool" >/dev/null || { echo "Missing $tool (install CMake and Xcode command-line tools)." >&2; exit 1; }
done
case ${1:-} in
    ''|--test) ;;
    *) echo 'Usage: bash packaging/macos/build.sh [--test]' >&2; exit 2 ;;
esac
build_dir=${BB_MAC_BUILD_DIR:-out/macos}
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_OSX_ARCHITECTURES="${BB_MAC_ARCH:-arm64}" -DBUILD_TESTING=ON
cmake --build "$build_dir" --parallel "${BB_BUILD_JOBS:-$(sysctl -n hw.logicalcpu)}"
if [[ ${1:-} == --test ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
    echo 'A skipped metal-gpu test means GPU execution was not verified in this environment.'
fi
echo "Built native Metal development targets in $build_dir/gpu/metal."
case ${BB_MAC_CPU:-rosetta} in
    rosetta) bash packaging/macos/build-runtime.sh "$@" ;;
    bbcpu) bash packaging/macos/build-native-runtime.sh "$@" ;;
    *) echo 'BB_MAC_CPU must be rosetta or bbcpu.' >&2; exit 2 ;;
esac
