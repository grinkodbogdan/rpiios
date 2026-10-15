#!/usr/bin/env bash
# Boot iPhone OS 2.x (iPod touch 2G) in the emulator.
#
# Controls (keyboard): H = Home, P = Power/Sleep, - / + = volume.
# Touch: a USB/DSI touchscreen or a mouse; left click = finger.
# Ctrl+Alt+F toggles fullscreen, Ctrl+Alt+G releases the mouse.
#
# Usage: run-ios.sh [--windowed] [extra qemu args...]
# Env:   BOOTROM, NOR, NAND to use your own self-generated images.
set -euo pipefail
source "$(dirname "$(readlink -f "$0")")/config.sh"

BOOTROM="${BOOTROM:-$FW_DIR/$BOOTROM_FILE}"
NOR="${NOR:-$FW_DIR/$NOR_FILE}"
NAND="${NAND:-$NAND_DIR}"

fullscreen=(-full-screen)
if [[ "${1:-}" == "--windowed" ]]; then
    fullscreen=()
    shift
fi

[[ -x "$QEMU_BIN" ]] || die "emulator not built; run scripts/build-emulator.sh first"
[[ -f "$BOOTROM" ]] || die "boot ROM not found at $BOOTROM; run scripts/fetch-firmware.sh"
[[ -f "$NOR" ]]     || die "NOR image not found at $NOR"
[[ -d "$NAND/cs0" ]] || die "NAND directory not found at $NAND"
mkdir -p "$RPIIOS_HOME/log" 2>/dev/null || true

# With no X/Wayland session (e.g. booted to the console), let SDL drive the
# display directly through KMS/DRM on the Pi's vc4 GPU.
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-kmsdrm}"
fi

# Notes on the flags:
#  - The machine maps its own fixed RAM regions (~130 MB, like the real
#    device), so the guest does not need the "-m 2G" from upstream's docs.
#    That matters on a 1 GB Pi.
#  - "-cpu max" is what upstream uses; the machine models an ARM1176.
#  - Serial console goes to a log for debugging the boot.
exec "$QEMU_BIN" \
    -M "iPod-Touch,bootrom=$BOOTROM,nor=$NOR,nand=$NAND" \
    -cpu max \
    -display sdl "${fullscreen[@]}" \
    -serial "file:${SERIAL_LOG:-$RPIIOS_HOME/log/serial.log}" \
    -monitor none \
    "$@"
