#!/usr/bin/env bash
# Download the boot ROM, NOR and NAND images the emulator needs.
#
# These files contain Apple's copyrighted code. They are NOT in this repo.
# This script fetches them from the emulator author's own release page. Only
# use them if you own an iPod touch 2G and the use is lawful where you live.
# Alternatively, build them yourself from your own device and IPSW with
# https://github.com/devos50/qemu-ios-generate-nor and
# https://github.com/devos50/qemu-ios-generate-nand, then pass the paths to
# run-ios.sh via BOOTROM / NOR / NAND.
set -euo pipefail
source "$(dirname "$(readlink -f "$0")")/config.sh"

if [[ "${1:-}" != "--i-own-an-ipod-touch-2g" ]]; then
    cat >&2 <<EOF
The firmware is Apple's copyrighted code. Re-run with
    $0 --i-own-an-ipod-touch-2g
to confirm you own the device it comes from, or generate the images yourself
(see the comments at the top of this script).
EOF
    exit 2
fi

mkdir -p "$FW_DIR"
cd "$FW_DIR"

fetch() {  # name sha256
    local name=$1 sum=$2
    if [[ -f "$name" ]] && echo "$sum  $name" | sha256sum -c --quiet - 2>/dev/null; then
        log "$name already present"
        return
    fi
    log "Downloading $name"
    curl -fL --retry 4 -o "$name.part" "$FW_RELEASE_URL/$name"
    echo "$sum  $name.part" | sha256sum -c --quiet - || die "$name: checksum mismatch"
    mv "$name.part" "$name"
}

fetch "$BOOTROM_FILE" "$BOOTROM_SHA256"
fetch "$NOR_FILE" "$NOR_SHA256"
if [[ ! -d "$NAND_DIR/cs0" ]]; then
    fetch "$NAND_ZIP" "$NAND_ZIP_SHA256"
    log "Unpacking NAND (about 1 GB)"
    rm -rf nand.tmp && mkdir nand.tmp
    unzip -q "$NAND_ZIP" -d nand.tmp
    rm -rf "$NAND_DIR"
    mv nand.tmp/nand "$NAND_DIR"
    rmdir nand.tmp
    rm -f "$NAND_ZIP"   # free ~250 MB on the SD card; the checksum above is enough to re-fetch
fi
log "Firmware ready in $FW_DIR"
