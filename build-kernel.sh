#!/usr/bin/env bash
# Fetch the HTC HD2 XNU tree, apply this repo's patches and build a kernel.
#
# Usage: ./build-kernel.sh [BOARD] [CONFIG]
#   BOARD   QSD8250_LEO (HD2, toolchain check) or RASPBERRYPI3 (default)
#   CONFIG  DEBUG (default) | DEVELOPMENT | RELEASE
# Output: out/build/<CONFIG>_ARM_<BOARD>/mach_kernel
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD="${1:-RASPBERRYPI3}"
CONFIG="${2:-DEBUG}"
OUT="$here/out"
TC="$OUT/toolchain"
XNU_REPO=https://github.com/Garysss123/xnu-hd2.git
XNU_COMMIT=8137aa25cd2188a270711e74b2a63346f381f67d
SRC="$OUT/xnu-hd2"

if [[ ! -d "$SRC/.git" ]]; then
    git clone -q "$XNU_REPO" "$SRC"
fi
# Re-apply the patch series whenever it changes (tracked by its hash).
stamp="$(cat "$here"/patches/xnu/*.patch | sha256sum | cut -c1-16)"
if [[ "$(cat "$SRC/.rpiios-patches" 2>/dev/null)" != "$stamp" ]]; then
    if [[ -n "$(git -C "$SRC" status --porcelain --untracked-files=no)" ]]; then
        echo "error: $SRC has uncommitted changes; commit them (and export them to" >&2
        echo "patches/xnu/) before the patch series is re-applied over them." >&2
        exit 1
    fi
    echo "==> Applying patches on top of $XNU_COMMIT"
    git -C "$SRC" checkout -q -f -B rpiios "$XNU_COMMIT"
    git -C "$SRC" -c user.name=rpiios -c user.email=rpiios@localhost \
        am -q --keep-cr --whitespace=nowarn "$here"/patches/xnu/*.patch
    echo "$stamp" > "$SRC/.rpiios-patches"
fi

XNU_SETUP="$SRC/xnu/SETUP" "$here/toolchain/build-toolchain.sh" "$TC"

export LD_LIBRARY_PATH="$TC/dispatch/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
echo "==> Building $CONFIG $BOARD"
make -C "$SRC/xnu" -j"${JOBS:-$(nproc)}" \
    TARGET_CONFIGS="$CONFIG ARM $BOARD" \
    XCRUN="$TC/bin/xcrun" HOST_SDKROOT="$TC/hostsdk" \
    OBJROOT="$OUT/build" SYMROOT="$OUT/sym" DSTROOT="$OUT/dst"

k="$OUT/build/${CONFIG}_ARM_${BOARD}/mach_kernel"
[[ -f "$k" ]] || { echo "error: $k was not produced" >&2; exit 1; }
echo "==> $k"
