#!/usr/bin/env bash
#
# One-command build for RP2040 / RP2350 Doom.
#
#   ./build.sh                          Raspberry Pi Pico (RP2040) + Pimoroni Pico Display Pack 2.0
#   ./build.sh -b pico2                 Raspberry Pi Pico 2 (RP2350) + Display Pack 2.0
#   ./build.sh -d PIMORONI_DISPLAY_28   Display Pack 2.8 instead
#   ./build.sh -b pimoroni_pico_plus2_rp2350 -w doom.whd   16MB board, bundle a full-game WHD
#   ./build.sh -t doom_tiny             build just one target
#
# Uses PICO_SDK_PATH / PICO_EXTRAS_PATH if set (or sibling ../pico-sdk, ../pico-extras folders);
# otherwise the SDK and extras are downloaded automatically. arm-none-eabi-gcc must be on PATH
# (or set PICO_TOOLCHAIN_PATH). Output UF2s end up in build-<board>-<display>/src/.
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
board=pico
display=""
whd=""
target=""
jobs="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
extra=()

usage() { awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit "${1:-0}"; }

while getopts "b:d:w:t:j:D:h" opt; do
    case "$opt" in
        b) board="$OPTARG" ;;
        d) display="$OPTARG" ;;
        w) whd="$(cd "$(dirname "$OPTARG")" && pwd)/$(basename "$OPTARG")" ;;
        t) target="$OPTARG" ;;
        j) jobs="$OPTARG" ;;
        D) extra+=("-D$OPTARG") ;;
        h) usage 0 ;;
        *) usage 1 ;;
    esac
done

if [[ -z "$display" ]]; then
    display=PIMORONI_DISPLAY_2
fi

if ! command -v arm-none-eabi-gcc >/dev/null 2>&1 && [[ -z "${PICO_TOOLCHAIN_PATH:-}" ]]; then
    echo "error: arm-none-eabi-gcc not found. Install the Arm GNU toolchain (13.x recommended, see README)" >&2
    echo "       and put its bin/ directory on PATH, or set PICO_TOOLCHAIN_PATH." >&2
    exit 1
fi

sdk_args=()
if [[ -z "${PICO_SDK_PATH:-}" && -d "$here/../pico-sdk" ]]; then export PICO_SDK_PATH="$here/../pico-sdk"; fi
if [[ -z "${PICO_EXTRAS_PATH:-}" && -d "$here/../pico-extras" ]]; then export PICO_EXTRAS_PATH="$here/../pico-extras"; fi
if [[ -n "${PICO_SDK_PATH:-}" ]]; then
    sdk_args+=("-DPICO_SDK_PATH=$PICO_SDK_PATH")
else
    echo "PICO_SDK_PATH not set: the Pico SDK will be downloaded into $here/.deps"
    sdk_args+=("-DPICO_SDK_FETCH_FROM_GIT=ON" "-DPICO_SDK_FETCH_FROM_GIT_PATH=$here/.deps")
fi
if [[ -n "${PICO_EXTRAS_PATH:-}" ]]; then
    sdk_args+=("-DPICO_EXTRAS_PATH=$PICO_EXTRAS_PATH")
else
    echo "PICO_EXTRAS_PATH not set: pico-extras will be downloaded into $here/.deps"
    sdk_args+=("-DPICO_EXTRAS_FETCH_FROM_GIT=ON" "-DPICO_EXTRAS_FETCH_FROM_GIT_PATH=$here/.deps")
fi

# the USB keyboard targets use TinyUSB host support from the SDK
if [[ -n "${PICO_SDK_PATH:-}" && -d "$PICO_SDK_PATH/lib/tinyusb" && ! -e "$PICO_SDK_PATH/lib/tinyusb/src/tusb.h" ]]; then
    echo "note: $PICO_SDK_PATH/lib/tinyusb is empty; run 'git submodule update --init lib/tinyusb' there for the *_usb targets"
fi

generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)

build_dir="$here/build-$board-$(echo "$display" | tr 'A-Z' 'a-z')"
cmake -S "$here" -B "$build_dir" "${generator[@]}" \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DPICO_BOARD="$board" \
    -DDOOM_DISPLAY="$display" \
    ${whd:+-DDOOM_BUNDLE_WHD="$whd"} \
    "${sdk_args[@]}" "${extra[@]}"
cmake --build "$build_dir" -j "$jobs" ${target:+--target "$target"}

echo
echo "Built into $build_dir/src:"
ls -1 "$build_dir"/src/*.uf2 2>/dev/null | sed "s|$build_dir/src/|  |"
echo
echo "Drag a *_wad.uf2 onto the BOOTSEL drive to install the program and its WAD together."
