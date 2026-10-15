#!/usr/bin/env bash
# Build the iPod touch 2G emulator (devos50/qemu-ios) on Raspberry Pi OS.
# Run with sudo. Takes roughly 1-2 hours on a Pi 3B+.
set -euo pipefail
source "$(dirname "$(readlink -f "$0")")/config.sh"

[[ $EUID -eq 0 ]] || die "run as root (sudo $0)"

log "Installing build dependencies"
apt-get update
apt-get install -y --no-install-recommends \
    git build-essential ninja-build pkg-config python3 python3-venv \
    libssl-dev libsdl2-dev libpixman-1-dev libglib2.0-dev \
    flex bison unzip curl ca-certificates

# A Pi 3B+ has 1 GB of RAM; compiling QEMU runs out of memory without more
# swap. Add a temporary swap file for the build (works whether the OS uses
# dphys-swapfile, zram, or nothing) and remove it afterwards.
mem_kb=$(awk '/MemTotal/ {print $2}' /proc/meminfo)
swap_kb=$(awk '/SwapTotal/ {print $2}' /proc/meminfo)
build_swap=/var/swap.rpiios-build
cleanup() {
    if [[ -f $build_swap ]]; then
        swapoff "$build_swap" 2>/dev/null || true
        rm -f "$build_swap"
    fi
}
trap cleanup EXIT
if (( mem_kb + swap_kb < 3000000 )); then
    log "Adding a temporary 2 GB swap file for the build (low-RAM board)"
    fallocate -l 2G "$build_swap" 2>/dev/null || dd if=/dev/zero of="$build_swap" bs=1M count=2048
    chmod 600 "$build_swap"
    mkswap "$build_swap" >/dev/null
    swapon "$build_swap"
fi

mkdir -p "$RPIIOS_HOME"
if [[ ! -d "$QEMU_SRC/.git" ]]; then
    log "Cloning $QEMU_IOS_REPO ($QEMU_IOS_BRANCH)"
    git clone --branch "$QEMU_IOS_BRANCH" "$QEMU_IOS_REPO" "$QEMU_SRC"
fi
git -C "$QEMU_SRC" fetch origin "$QEMU_IOS_BRANCH"
git -C "$QEMU_SRC" checkout --detach "$QEMU_IOS_COMMIT"

log "Configuring (arm-softmmu only)"
mkdir -p "$QEMU_SRC/build"
cd "$QEMU_SRC/build"
# Same flags upstream's RUNNING.md gives for Linux.
../configure \
    --enable-sdl --disable-cocoa \
    --target-list=arm-softmmu \
    --disable-capstone --disable-slirp \
    --extra-cflags=-I/usr/include/openssl --extra-ldflags='-lcrypto' \
    --disable-werror --enable-pie

# Build only the emulator binary, not the full QEMU tool set: much faster.
jobs=2
(( mem_kb >= 2000000 )) && jobs=$(nproc)
log "Compiling qemu-system-arm with $jobs jobs (this is the slow part)"
ninja -j"$jobs" qemu-system-arm

[[ -x "$QEMU_BIN" ]] || die "build finished but $QEMU_BIN is missing"
log "Built $QEMU_BIN"
"$QEMU_BIN" -machine help | grep -q 'iPod-Touch' || die "binary lacks the iPod-Touch machine"
log "OK: iPod-Touch machine available"
