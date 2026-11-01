#!/bin/bash
# The Mi 5's iOS root (iPhone 8 / iOS 16.7.16, 20H392), built on Linux or WSL2: no Mac, no
# apfs-fuse, no hfsplus kernel module needed. What ios-sailfish's tools/mkiosroot.sh does, from
# the same three images, by another road:
#
#   1. tools/ipsw.py fetches the OS, SystemOS and AppOS images out of the IPSW on Apple's CDN
#      (7.4 GB of the 7.6 GB archive)
#   2. 7-Zip (23 or newer) extracts them; HackiOS's unzbm expands the LZBITMAP-compressed files
#      7-Zip cannot; tools/fixmeta.py puts back owners, modes, symbolic and hard links from
#      7-Zip's own listing of each image
#   3. sailfish's additions on that tree: the shared cache and the cryptex files linked where
#      dyld looks at ignition level 1, logd's stores, the plain system keybag, the stock apps
#      moved into /Applications, sweetgl.dylib if built, then its tools/sweet-root-tweaks.py
#   4. a case-sensitive journaled HFS+ volume (mkfs.hfsplus), filled through the kernel's
#      hfsplus where there is one, else through libguestfs (WSL2's kernel has no hfsplus), then
#      fsck.hfsplus
#
# usage: sudo mi5/mkroot.sh WORKDIR HACKIOS_TREE OUT.img [SIZE]       (SIZE default 16G)
#   WORKDIR       mi5/kernel/setup.sh's (its sailfish/ is used; fetched here if missing). Needs
#                 about 40 GB free on a Linux filesystem (in WSL: under ~, never /mnt/c)
#   HACKIOS_TREE  a HackiOS checkout (for unzbm's source: xnu-deps/build/lzbitmap)
#   OUT.img       the raw volume: `fastboot flash userdata OUT.img` (fastboot sends it sparse)
# Needs: python3, 7z, cc, rsync, hfsprogs (mkfs.hfsplus, fsck.hfsplus), and without kernel
# hfsplus: libguestfs-tools. Packages: mi5/README.md.
#   KEEP=1  keep the downloaded images (WORKDIR/ipsw/) for another run
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
[[ $# -ge 3 ]] || { sed -n '2,25p' "$0"; exit 2; }
[[ $(id -u) == 0 ]] || { echo "mkroot.sh: run as root (sudo): the owners of the files must be kept" >&2; exit 1; }
W=$(mkdir -p "$1" && cd "$1" && pwd)
H=$(cd "$2" && pwd)
out=$3 size=${4:-16G}
[[ ! -e $out ]] || { echo "mkroot.sh: refusing to overwrite $out" >&2; exit 1; }

IPSW=https://updates.cdn-apple.com/2025FallFCS/fullrestores/122-77002/0281BFC6-38ED-485A-A8BE-8871861174B1/iPhone_4.7_P3_16.7.16_20H392_Restore.ipsw
OS_DMG=087-86684-044.dmg        # BuildManifest: OS
SYSOS_DMG=087-86580-047.dmg     # Cryptex1,SystemOS (the dyld shared cache)
APPOS_DMG=087-86715-047.dmg     # Cryptex1,AppOS
SAILFISH_SHA=6f27e41ff249f2680540ebe8ee5ab982c5e641c9

for t in python3 7z cc rsync mkfs.hfsplus fsck.hfsplus; do
	command -v $t >/dev/null || { echo "mkroot.sh: $t is not installed (see mi5/README.md)" >&2; exit 1; }
done
7z i | grep -Eq '7-Zip (2[3-9]|[3-9][0-9])\.' || { echo "mkroot.sh: 7-Zip 23 or newer is needed (it reads APFS); p7zip 16 does not" >&2; exit 1; }

if [[ ! -d $W/sailfish/.git ]]; then
	git init -q "$W/sailfish"
	git -C "$W/sailfish" fetch -q --depth 1 https://github.com/joranv1/ios-sailfish "$SAILFISH_SHA"
	git -C "$W/sailfish" checkout -q FETCH_HEAD
fi
UNZBM=$W/bin/unzbm
if [[ ! -x $UNZBM ]]; then
	mkdir -p "$W/bin"
	cc -O2 -w -o "$UNZBM" "$H/xnu-deps/build/lzbitmap/unzbm.c" "$H/xnu-deps/build/lzbitmap/libzbitmap.c"
fi

D=$W/ipsw
mkdir -p "$D"
fetch() {	# MEMBER NAME
	[[ -s $D/$2.dmg ]] && return 0
	python3 -I "$HERE/tools/ipsw.py" "$IPSW" get "$1" "$D/$2.dmg.part"
	mv "$D/$2.dmg.part" "$D/$2.dmg"
}

S=$W/stage
if [[ -e $S ]]; then
	echo "mkroot.sh: $S exists (an earlier run): remove it to start again" >&2
	exit 1
fi

# extract IMAGE-NAME DEST [7z switches]: files, LZBITMAP expanded, metadata from the listing
extract() {
	local name=$1 dest=$2
	shift 2
	echo "mkroot.sh: extracting $name -> /${dest#"$S"}"
	7z l -slt "$D/$name.dmg" > "$D/$name.slt"
	mkdir -p "$dest"
	# 7z reports every LZBITMAP file as an error (Unsupported Method): expected, checked below
	7z x -y -snld -o"$dest" "$D/$name.dmg" "$@" > "$D/$name.log" 2>&1 || true
	find "$dest" -type f -name '*:com.apple.decmpfs' | sed 's/:com\.apple\.decmpfs$//' | tr '\n' '\0' |
		xargs -0 -r -n 200 "$UNZBM"
	local left
	left=$(find "$dest" -name '*:com.apple.*' | wc -l)
	[[ $left == 0 ]] || { echo "mkroot.sh: $left files of $name are still compressed (see $D/$name.log)" >&2; exit 1; }
	python3 -I "$HERE/tools/fixmeta.py" "${FIXMETA_SKIP[@]}" "$D/$name.slt" "$dest"
	[[ -n ${KEEP:-} ]] || rm -f "$D/$name.dmg"
}

fetch "$OS_DMG" os
FIXMETA_SKIP=()
extract os "$S"
fetch "$SYSOS_DMG" sysos
# the shared cache's .symbols files (about 0.6 GB) are never mapped
FIXMETA_SKIP=(--exclude '*.symbols')
extract sysos "$S/private/preboot/Cryptexes/OS" '-xr!*.symbols'
fetch "$APPOS_DMG" appos
FIXMETA_SKIP=()
extract appos "$S/private/preboot/Cryptexes/App"
rm -rf "$S"/private/preboot/Cryptexes/*/.fseventsd "$S/.fseventsd"

# --- sailfish's additions (tools/mkiosroot.sh, README step 2) ------------------------------------
# the shared cache where dyld looks at ignition level 1: hard links
dsc=$S/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld
mkdir -p "$S/System/Library/Caches/com.apple.dyld"
for f in "$dsc"/*; do ln -f "$f" "$S/System/Library/Caches/com.apple.dyld/${f##*/}"; done
# cryptex files without a counterpart in the OS root, linked to their root path
for c in OS App; do
	(cd "$S/private/preboot/Cryptexes/$c" && find . ! -type d ! -path './System/Library/Caches/com.apple.dyld/*') |
	while IFS= read -r f; do
		f=${f#./}
		if [[ ! -e $S/$f && ! -L $S/$f ]]; then
			mkdir -p "$S/$(dirname "$f")"
			src=$S/private/preboot/Cryptexes/$c/$f
			if [[ -L $src ]]; then cp -P "$src" "$S/$f"; else ln "$src" "$S/$f"; fi
		fi
	done
done
# logd's stores (owner _logd 272, .migrated markers)
db=$S/private/var/db
install -d -o 272 -g 272 -m 0751 "$db/diagnostics"
install -d -o 272 -g 272 -m 0755 "$db/uuidtext" "$db/diagnostics/timesync"
for d in diagnostics uuidtext diagnostics/timesync; do
	install -o 272 -g 272 -m 0644 /dev/null "$db/$d/.migrated"
done
# the system keybag for the built-in AppleKeyStore (no passcode)
install -d -m 0700 -o 0 -g 0 "$S/private/var/keybags"
printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' \
	'<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	'<plist version="1.0"><dict><key>KeyBagKeys</key><data>U1dFRVQ=</data></dict></plist>' |
	install -m 0600 -o 0 -g 0 /dev/stdin "$S/private/var/keybags/systembag.kb"
# the stock apps: iOS's own first-boot install of them fails on code signing here
if [[ -d $S/private/var/staged_system_apps ]]; then
	for a in "$S"/private/var/staged_system_apps/*.app; do
		[[ -e $a ]] || continue
		[[ -e $S/Applications/${a##*/} ]] || mv "$a" "$S/Applications/"
	done
fi
# the GL library for backboardd (mi5/sweetgl/build.sh; mkboot.sh injects it when it is built)
if [[ -s $W/sweetgl/sweetgl.dylib ]]; then
	install -m 0755 -o 0 -g 0 "$W/sweetgl/sweetgl.dylib" "$S/usr/lib/sweetgl.dylib"
	echo "mkroot.sh: /usr/lib/sweetgl.dylib installed"
else
	echo "mkroot.sh: no $W/sweetgl/sweetgl.dylib: the root has no GPU drawing (mi5/sweetgl/build.sh)"
fi
python3 -I "$W/sailfish/tools/sweet-root-tweaks.py" "$S"
du -sh "$S"

# --- the HFS+ volume ----------------------------------------------------------------------------
truncate -s "$size" "$out"
mkfs.hfsplus -s -J -v iOSRoot "$out"
if modprobe hfsplus 2>/dev/null || grep -qw hfsplus /proc/filesystems; then
	m=$(mktemp -d)
	mount -t hfsplus -o loop,force "$out" "$m"
	rsync -aH --numeric-ids "$S/" "$m/"
	df -h "$m"
	umount "$m"
	rmdir "$m"
else
	command -v guestfish >/dev/null || { echo "mkroot.sh: no hfsplus in this kernel: install libguestfs-tools" >&2; exit 1; }
	echo "mkroot.sh: no hfsplus in this kernel: writing the volume through libguestfs"
	# the volume is journaled: Linux writes it only with force (and leaves the journal empty)
	tar --numeric-owner --format=gnu -C "$S" -cf - . |
		LIBGUESTFS_BACKEND=direct guestfish --rw -a "$out" run : mount-options force /dev/sda / : \
			tar-in - / : df-h : umount-all
fi
# Linux hfsplus decomposes some non-ASCII names differently from Apple's tables: fsck rewrites them
fsck.hfsplus -f -y "$out" | grep -v 'Illegal name' || true
fsck.hfsplus -f -n "$out"
[[ $(od -An -tx1 -j1024 -N2 "$out" | tr -d ' ') == 4858 ]] || { echo "mkroot.sh: no HFS+ (HX) signature in $out" >&2; exit 1; }
chown "${SUDO_UID:-0}:${SUDO_GID:-0}" "$out"
[[ -n ${KEEP:-} ]] || rm -rf "$S"
ls -l "$out"
echo "mkroot.sh: done: flash it with  fastboot flash userdata $out"
