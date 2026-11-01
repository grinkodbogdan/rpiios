#!/bin/bash
# The Mi 5 kernel tree: Apple's xnu-8796.141.3 (macOS 13.5, the iOS 16.6/16.7 generation), with
# ios-sailfish's patches (the Pixel 1 / MSM8996 port), darwin-xnu-build's, linux.patch (what it
# takes to build that tree on Linux with stock clang and HackiOS's Darwin toolchain) and mi5.patch
# (the Mi 5 board).
#
# usage: mi5/kernel/setup.sh WORKDIR HACKIOS_TREE
#   WORKDIR       made here: sailfish/, dxb/ and xnu/ (the tree build.sh builds)
#   HACKIOS_TREE  a HackiOS checkout with xnu-deps/setup.sh run (its cctools, mig, SDK overlay)
# Then: mi5/kernel/build.sh WORKDIR HACKIOS_TREE
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=$(mkdir -p "${1:?usage: setup.sh WORKDIR HACKIOS_TREE}" && cd "$1" && pwd)
H=$(cd "${2:?usage: setup.sh WORKDIR HACKIOS_TREE}" && pwd)

SAILFISH_SHA=6f27e41ff249f2680540ebe8ee5ab982c5e641c9   # joranv1/ios-sailfish
DXB_SHA=663906528d1cd5ea8fde7de6f16de73db9511549        # blacktop/darwin-xnu-build, sailfish's pin
TAG=xnu-8796.141.3

fetch() {   # DIR URL REF
	[ -d "$1/.git" ] && return 0
	git init -q "$1"
	git -C "$1" fetch -q --depth 1 "$2" "$3"
	git -C "$1" checkout -q FETCH_HEAD
}
fetch "$W/sailfish" https://github.com/joranv1/ios-sailfish "$SAILFISH_SHA"
fetch "$W/dxb" https://github.com/blacktop/darwin-xnu-build "$DXB_SHA"
[ -e "$W/xnu" ] && { echo "setup.sh: $W/xnu exists; remove it to start again" >&2; exit 1; }
git init -q "$W/xnu"
git -C "$W/xnu" fetch -q --depth 1 https://github.com/apple-oss-distributions/xnu.git "refs/tags/$TAG:refs/tags/$TAG"
git -C "$W/xnu" checkout -q -b mi5 "$TAG"
echo 'BUILD/' >> "$W/xnu/.git/info/exclude"

# sailfish's staging scripts (they fetch libpthread, BearSSL, HFS and IONetworkingFamily at pinned
# commits) want macOS's xcrun and sed: bin/ has stand-ins
source "$H/xnu-env.sh"
export PATH=$HERE/bin:$PATH MACHINE_CONFIG=SWEET MACOS_VERSION=13.5
cd "$W/xnu"
for s in "$W"/sailfish/xnu/patches/*.sh; do bash "$s"; done
# the patches in the order darwin-xnu-build applies them (one directory, glob order), skipping those
# that do not apply to this release as it does
for p in "$W"/sailfish/xnu/patches/*.patch "$W"/dxb/patches/*.patch; do
	if git apply --check "$p" 2>/dev/null; then git apply --whitespace=nowarn "$p"; else echo "setup.sh: skipped $(basename "$p")"; fi
done
git apply --whitespace=nowarn "$HERE/linux.patch"
# the Mi 5 board: its framebuffer as LK leaves it, its keys, both its touch controllers
git apply --whitespace=nowarn "$HERE/mi5.patch"
echo "setup.sh: $W/xnu ready"
