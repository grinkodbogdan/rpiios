#!/usr/bin/env bash
# Make a bootable rpiios SD card for the Raspberry Pi 3 Model B+.
#
#   sudo ./flash-sdcard.sh /dev/sdX  ios7.rar      write a real card
#   ./flash-sdcard.sh rpiios.img     ios7.rar      make a card image instead
#                                                  (flash it with Raspberry Pi
#                                                  Imager, Etcher or dd)
#
# The second argument is the HD2 release archive (ios7.rar) or the rootfs.hfs
# already extracted from it. The card is repartitioned (MBR, one FAT32
# partition) and EVERYTHING ON IT IS ERASED. The boot files are taken from the
# boot/ folder next to this script (override with BOOT_DIR=...). Image size
# defaults to 2048 MiB (override with IMAGE_MB=...).
#
# Needs: sfdisk (fdisk/util-linux), mkfs.vfat (dosfstools), mtools, and for
# .rar input one of 7z (p7zip-full / 7zip), unar or unrar.
# Debian/Ubuntu: sudo apt install fdisk dosfstools mtools unar
set -euo pipefail

die() { echo "error: $*" >&2; exit 1; }
usage() { sed -n 2,17p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
# Size in sectors of the first partition, as written to the MBR.
partition_sectors() { sfdisk -d "$1" | sed -n 's/.*start= *[0-9]*, *size= *\([0-9]*\).*/\1/p' | head -1; }

[[ $# -eq 2 ]] || usage
target="$1" source="$2"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
boot="${BOOT_DIR:-$here/boot}"
image_mb="${IMAGE_MB:-2048}"
start_sector=8192                       # 4 MiB, erase-block aligned
boot_files=(bootcode.bin start.elf fixup.dat kernel7.img config.txt cmdline.txt)

for f in "${boot_files[@]}"; do
    [[ -f "$boot/$f" ]] || die "missing $boot/$f (keep flash-sdcard.sh next to the boot/ folder)"
done
[[ -f "$source" ]] || die "no such file: $source"
for t in sfdisk mkfs.vfat mcopy mmd mdir; do
    command -v "$t" >/dev/null || die "$t not found; on Debian/Ubuntu: sudo apt install fdisk dosfstools mtools unar"
done

# --- Root filesystem: use it directly or pull it out of the archive. -------
work="$(mktemp -d "${TMPDIR:-/tmp}/rpiios.XXXXXX")"
trap 'rm -rf "$work"' EXIT
case "$(head -c 4 "$source" | od -An -tx1 | tr -d ' ')" in
    52617221)                                           # "Rar!"
        echo "==> Extracting rootfs.hfs from $(basename "$source") (about 900 MiB)"
        avail_kb=$(df -Pk "$work" | awk 'NR==2 {print $4}')
        (( avail_kb > 1000000 )) || die "need about 1 GiB free in ${TMPDIR:-/tmp} (set TMPDIR to somewhere with space)"
        # Some 7z builds (Debian's p7zip) lack the RAR codec, so try each
        # extractor that is installed until one produces the file.
        rootfs="$work/rootfs.hfs" tried=""
        for x in 7zz 7z unar unrar; do
            command -v "$x" >/dev/null || continue
            tried+=" $x"
            rm -f "$rootfs"
            case "$x" in
                7z|7zz) "$x" e -y -bd -o"$work" "$source" ios7/rootfs.hfs >/dev/null 2>&1 ;;
                unar)   unar -q -f -D -o "$work" "$source" ios7/rootfs.hfs >/dev/null 2>&1 ;;
                unrar)  unrar e -y -inul "$source" ios7/rootfs.hfs "$work/" >/dev/null 2>&1 ;;
            esac || continue
            # Some extractors keep the ios7/ folder; take the file from wherever it landed.
            found="$(find "$work" -type f -name rootfs.hfs | head -1)"
            [[ -n "$found" && "$found" != "$rootfs" ]] && mv "$found" "$rootfs"
            [[ -s "$rootfs" ]] && break
        done
        [[ -s "$rootfs" ]] || {
            [[ -n "$tried" ]] || die "need 7zip, unar or unrar to open the .rar (sudo apt install unar)"
            die "could not extract ios7/rootfs.hfs with:$tried (sudo apt install unar)"
        }
        ;;
    *) rootfs="$source";;
esac
rootfs_bytes=$(stat -Lc %s "$rootfs")
# An HFS+ volume carries "H+" (or "HX") 1024 bytes in.
sig="$(dd if="$rootfs" bs=1 skip=1024 count=2 status=none)"
[[ "$sig" == "H+" || "$sig" == "HX" ]] || die "$rootfs is not an HFS+ image (expected the rootfs.hfs from ios7.rar)"
(( rootfs_bytes < 4294967296 )) || die "rootfs.hfs is 4 GiB or larger and cannot go on FAT32"
need_mb=$(( start_sector / 2048 + rootfs_bytes / 1048576 + 64 ))

# --- Target: a card (block device) or an image file. ----------------------
if [[ -b "$target" ]]; then
    [[ $EUID -eq 0 ]] || die "writing to $target needs root: sudo $0 $*"
    dev="$(readlink -f "$target")"
    case "$(lsblk -dno TYPE "$dev")" in disk|loop) ;; *) false;; esac ||
        die "$dev is a partition; give the whole card (e.g. /dev/sdb or /dev/mmcblk0, not /dev/sdb1)"
    # Never touch the disk the running system lives on.
    for m in / /boot /boot/efi /usr /home; do
        src="$(findmnt -no SOURCE "$m" 2>/dev/null || true)"
        [[ -n "$src" && -b "$src" ]] || continue
        parent="$(lsblk -no PKNAME "$src" 2>/dev/null | head -1)"
        [[ "/dev/$parent" != "$dev" && "$src" != "$dev" ]] ||
            die "$dev holds this computer's $m; refusing"
    done
    size_bytes=$(lsblk -bdno SIZE "$dev")
    (( size_bytes / 1048576 >= need_mb )) || die "$dev is too small (need at least $need_mb MiB)"
    echo
    lsblk -o NAME,SIZE,TYPE,TRAN,RM,MODEL,MOUNTPOINTS "$dev" 2>/dev/null || lsblk "$dev"
    echo
    echo "EVERYTHING on $dev ($(( size_bytes / 1000000000 )) GB) will be erased."
    read -r -p "Type yes to continue: " answer
    [[ "$answer" == yes ]] || die "cancelled"

    echo "==> Unmounting $dev"
    for p in $(lsblk -lnpo NAME "$dev"); do
        for mp in $(findmnt -rno TARGET -S "$p" 2>/dev/null); do umount "$mp" || die "cannot unmount $mp"; done
    done
    echo "==> Partitioning"
    wipefs -aq "$dev"
    printf 'label: dos\n%d,,c,*\n' "$start_sector" | sfdisk -q --wipe always "$dev"
    command -v udevadm >/dev/null && udevadm settle || true
    partprobe "$dev" 2>/dev/null || blockdev --rereadpt "$dev" 2>/dev/null || true
    command -v udevadm >/dev/null && udevadm settle || true
    part=""
    for _ in $(seq 20); do
        part="$(lsblk -lnpo NAME,TYPE "$dev" | awk '$2=="part" {print $1; exit}')"
        [[ -n "$part" && -b "$part" ]] && break
        part=""
        sleep 0.5
    done
    if [[ -n "$part" ]]; then
        # A desktop may automount the fresh partition; keep it unmounted.
        for mp in $(findmnt -rno TARGET -S "$part" 2>/dev/null); do umount "$mp" || true; done
        echo "==> Formatting $part as FAT32"
        mkfs.vfat -F 32 -n RPIIOS -h "$start_sector" "$part" >/dev/null
        fat="$part"
    else
        # No partition node (no udev, some card readers): write the same
        # filesystem at the partition's offset on the whole device.
        part_sectors=$(partition_sectors "$dev")
        echo "==> Formatting the partition as FAT32 (through $dev)"
        mkfs.vfat -F 32 -n RPIIOS -h "$start_sector" --offset "$start_sector" "$dev" $(( part_sectors / 2 )) >/dev/null
        fat="$dev@@$(( start_sector * 512 ))"
    fi
else
    [[ "$target" == *.img ]] || die "$target is not a block device; to make an image file, name it something.img"
    (( image_mb >= need_mb )) || die "IMAGE_MB=$image_mb is too small (need at least $need_mb)"
    echo "==> Creating $target ($image_mb MiB)"
    rm -f "$target"
    truncate -s $(( image_mb * 1048576 )) "$target"
    printf 'label: dos\n%d,,c,*\n' "$start_sector" | sfdisk -q "$target"
    part_sectors=$(partition_sectors "$target")
    mkfs.vfat -F 32 -n RPIIOS -h "$start_sector" --offset "$start_sector" "$target" $(( part_sectors / 2 )) >/dev/null
    fat="$target@@$(( start_sector * 512 ))"
fi

# --- Files. ---------------------------------------------------------------
export MTOOLS_SKIP_CHECK=1
# Run an mtools command; show its stderr only if it fails (on block devices
# it warns about disk geometry on every call).
mt() { "$@" 2>"$work/mt.err" || { cat "$work/mt.err" >&2; return 1; }; }
echo "==> Copying boot files"
for f in "${boot_files[@]}" LICENCE.broadcom; do
    [[ -f "$boot/$f" ]] && mt mcopy -o -i "$fat" "$boot/$f" "::$f"
done
mt mmd -i "$fat" ::IOS7
echo "==> Copying IOS7/ROOTFS.HFS ($(( rootfs_bytes / 1048576 )) MiB, this takes a few minutes)"
mt mcopy -o -i "$fat" "$rootfs" ::IOS7/ROOTFS.HFS
sync

echo "==> Verifying"
# The kernel's FAT reader insists the boot sector's hidden-sector count
# equals the partition start (iokit .../fat_locator.c).
raw="${dev:-$target}"
hidden=$(dd if="$raw" bs=512 skip="$start_sector" count=1 status=none | od -An -tu4 -j28 -N4 | tr -d ' ')
[[ "$hidden" == "$start_sector" ]] || die "FAT32 hidden-sector field is $hidden, expected $start_sector (please report this)"
if [[ -b "$target" ]]; then blockdev --flushbufs "$dev" "$part" 2>/dev/null || true; fi
for f in "${boot_files[@]}"; do
    mt mcopy -n -i "$fat" "::$f" - | cmp -s - "$boot/$f" || die "$f did not read back correctly; the card may be faulty"
done
mt mcopy -n -i "$fat" ::IOS7/ROOTFS.HFS - | cmp -s - "$rootfs" ||
    die "IOS7/ROOTFS.HFS did not read back correctly; the card may be faulty"
mt mdir -i "$fat" -/ :: | grep -vE '^$|Volume|Directory for' || true

echo
if [[ -b "$target" ]]; then
    echo "Done. Remove the card, put it in the Pi 3B+, connect HDMI and a USB mouse, power on."
else
    echo "Done: $target. Flash it to a card (2 GB or larger) with Raspberry Pi Imager"
    echo "(\"Use custom\"), balenaEtcher, or: sudo dd if=$target of=/dev/sdX bs=4M conv=fsync"
fi
