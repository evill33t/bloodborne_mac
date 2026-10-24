#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
if [[ $(uname -s) != Darwin ]]; then
    echo 'This launcher requires macOS.' >&2; exit 1
fi
game=${BB_SOURCE_GAME:-${BB_GAME_DIR:-${1:-}}}
if [[ -z $game || ! -f $game/eboot.bin ]]; then
    echo 'Usage: BB_GAME_DIR=/path/to/CUSA00900 bash run.sh' >&2; exit 2
fi
case ${BB_MAC_CPU:-rosetta} in
    rosetta) runtime=${BB_MAC_RUNTIME_DIR:-out/macos-runtime}/bb-macos-game; builder=packaging/macos/build-runtime.sh ;;
    bbcpu) runtime=${BB_MAC_NATIVE_RUNTIME_DIR:-out/macos-native-runtime}/bb-macos-game; builder=packaging/macos/build-native-runtime.sh ;;
    *) echo 'BB_MAC_CPU must be rosetta or bbcpu.' >&2; exit 2 ;;
esac
if [[ ! -x $runtime ]]; then
    bash "$builder"
fi
python=${PYTHON:-python3}
game=$("$python" -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve(strict=True))' "$game")
# A source dump and its add-ons commonly use GAMES/<title> and DLC/<title>.
# Resolve from the SFO title ID before creating a mod view (whose name differs).
# An explicitly empty BB_DLC_DIR still allows a deliberate base-game-only run.
if [[ -z ${BB_DLC_DIR+x} ]]; then
    dlc=$("$python" - "$game" <<'PY'
import pathlib,sys
sys.path.insert(0, 'scripts')
from prepare import sfo
game = pathlib.Path(sys.argv[1])
title = sfo((game / 'sce_sys/param.sfo').read_bytes())['TITLE_ID']
if game.parent.name == 'GAMES':
    candidate = game.parent.parent / 'DLC' / title
    if candidate.is_dir():
        print(candidate.resolve())
PY
)
    if [[ -n $dlc ]]; then export BB_DLC_DIR="$dlc"; fi
fi
if [[ -n ${BB_DLC_DIR:-} ]]; then
    dlc=$("$python" -c 'import pathlib,sys; p=pathlib.Path(sys.argv[1]).resolve(strict=True); assert p.is_dir(), "DLC path must be a directory"; print(p)' "$BB_DLC_DIR")
    export BB_DLC_DIR="$dlc"
    echo "macOS: DLC directory: $BB_DLC_DIR"
fi
data=${BB_DATA_DIR:-out/macos-data}
prepared=$data/prepared
export BB_GAME_DIR="$game" BB_DATA_DIR="$data"
export BB_SOURCE_GAME="$game"
export BB_MODS_DIR="${BB_MODS_DIR:-$data/mods}"
if [[ -n ${BB_MAC_MOD_VIEW:-} ]]; then
    "$python" - "$BB_MAC_MOD_VIEW" "$data/mod-views" <<'PY'
import pathlib,shutil,sys
old,root=map(pathlib.Path,sys.argv[1:])
if old.parent.resolve()==root.resolve() and old.name.startswith('mod-game-') and old.is_dir() and not old.is_symlink():
    shutil.rmtree(old)
PY
fi
game=$("$python" scripts/mods.py "$game" --out "$data/mod-views" --mods-dir "$BB_MODS_DIR" \
    --config "${BB_MODS_CONFIG:-$data/mods.json}" --enabled "${BB_MODS_ENABLED:-1}")
export BB_GAME_DIR="$game"
if [[ $game != "$BB_SOURCE_GAME" ]]; then export BB_MAC_MOD_VIEW="$game"; else unset BB_MAC_MOD_VIEW; fi
mod_game=${BB_MAC_MOD_VIEW:-}
cleanup_mod_view() {
    if [[ -n $mod_game ]]; then
        "$python" - "$mod_game" "$data/mod-views" <<'PY'
import pathlib,shutil,sys
view,root=map(pathlib.Path,sys.argv[1:])
if view.parent.resolve()==root.resolve() and view.name.startswith('mod-game-') and view.is_dir() and not view.is_symlink():
    shutil.rmtree(view)
PY
    fi
}
trap cleanup_mod_view EXIT
export BB_MAC_SETTINGS="${BB_MAC_SETTINGS:-$data/macos-settings.ini}"
export BB_MAC_BASE_USER_DIR="${BB_MAC_BASE_USER_DIR:-${BB_USER_DIR:-$data/user}}"
export BB_USER_DIR=$("$python" scripts/macos_nightwatch.py --settings "$BB_MAC_SETTINGS" \
    --user "$BB_MAC_BASE_USER_DIR" --cpu "${BB_MAC_CPU:-rosetta}")
"$python" scripts/macos_prepare.py "$game" --out "$prepared" \
    --graphics-profile "${BB_MAC_GRAPHICS_PROFILE:-original}" --settings "$BB_MAC_SETTINGS"
# Recalculate automatic sizing after a resolution restart, while preserving an
# explicit BB_DMEM_MB override. The previous automatic value is inherited by exec.
if [[ -z ${BB_DMEM_MB:-} || ${BB_MAC_AUTO_DMEM_MB:-} == "$BB_DMEM_MB" ]]; then
    export BB_DMEM_MB=$("$python" -c 'import json,sys; print(json.load(open(sys.argv[1]))["direct_memory_mb"])' "$prepared/macos-runtime-options.json")
    export BB_MAC_AUTO_DMEM_MB="$BB_DMEM_MB"
fi
# Command-scoped Rosetta feature advertisement; no persistent OS settings.
if [[ ${BB_MAC_CPU:-rosetta} == rosetta ]]; then export ROSETTA_ADVERTISE_AVX=1; fi
export BB_MAC_TIMESTEP=1
export BB_MAC_EBOOT_SHA256=$("$python" -c 'import json,sys; print(json.load(open(sys.argv[1]))["elf_sha256"])' "$prepared/macos-runtime-options.json")
export BB_SOUND_HACK=${BB_SOUND_HACK:-0}
"$runtime" "$prepared/boot-linked.bin" --patches "$prepared/patches.bin" \
    --content-profile "$prepared/content.bin" --app0 "$game" --user "${BB_USER_DIR:-$data/user}" \
    --timeout "${BB_TIMEOUT:-0}"
