#!/usr/bin/env bash
# Build a Raspberry Pi SD card image: MBR + one FAT32 partition holding the
# Pi firmware, kernel7.img and IOS7/ROOTFS.HFS.
#
# Usage: tools/make-sdcard.sh --rootfs rootfs.hfs --kernel kernel7.img \
#            --out sdcard.img [--firmware DIR] [--size-mb 2048]
#
# --firmware: directory with bootcode.bin, start.elf and fixup.dat from
# github.com/raspberrypi/firmware (boot/). Needed on real hardware, not by
# QEMU (which loads kernel7.img itself). The size defaults to 2 GiB, a power
# of two as QEMU's SD card model requires.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rootfs= kernel= out= firmware= size_mb=2048
while [[ $# -gt 0 ]]; do
    case "$1" in
        --rootfs) rootfs="$2"; shift 2;;
        --kernel) kernel="$2"; shift 2;;
        --out) out="$2"; shift 2;;
        --firmware) firmware="$2"; shift 2;;
        --size-mb) size_mb="$2"; shift 2;;
        *) echo "unknown option $1" >&2; exit 2;;
    esac
done
[[ -f "$rootfs" && -f "$kernel" && -n "$out" ]] || { sed -n 2,12p "$0"; exit 2; }
command -v mkfs.vfat >/dev/null && command -v mcopy >/dev/null && command -v sfdisk >/dev/null ||
    { echo "need dosfstools, mtools and fdisk (sfdisk)" >&2; exit 1; }

start_sector=8192                                   # 4 MiB alignment
total_sectors=$(( size_mb * 2048 ))
part_sectors=$(( total_sectors - start_sector ))
rootfs_mb=$(( $(stat -c %s "$rootfs") / 1048576 + 64 ))
(( size_mb > rootfs_mb )) || { echo "--size-mb too small for the root image" >&2; exit 1; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
part="$tmp/part.img"

echo "==> Creating FAT32 partition ($(( part_sectors / 2048 )) MiB)"
truncate -s $(( part_sectors * 512 )) "$part"
mkfs.vfat -F 32 -n RPIIOS -h "$start_sector" "$part" >/dev/null

cat > "$tmp/config.txt" <<'EOF'
# rpiios: boot the iOS 7 XNU kernel in 32-bit mode.
arm_64bit=0
kernel=kernel7.img
# The shim talks to the firmware framebuffer itself; keep the GPU memory
# split modest so ARM keeps most of the 1 GiB.
gpu_mem=64
# Prefer HDMI even without a monitor detected at power-on.
hdmi_force_hotplug=1
disable_splash=1
# No Linux device tree: the firmware passes ATAGs (with cmdline.txt) instead.
device_tree=
EOF
# Extra boot-args for XNU go here (appended to the built-in ones).
printf '\n' > "$tmp/cmdline.txt"

m=(mcopy -i "$part" -o)
"${m[@]}" "$tmp/config.txt" ::config.txt
"${m[@]}" "$tmp/cmdline.txt" ::cmdline.txt
"${m[@]}" "$kernel" ::kernel7.img
if [[ -n "$firmware" ]]; then
    for f in bootcode.bin start.elf fixup.dat; do
        [[ -f "$firmware/$f" ]] || { echo "missing $firmware/$f" >&2; exit 1; }
        "${m[@]}" "$firmware/$f" "::$f"
    done
fi
mmd -i "$part" ::IOS7
echo "==> Copying root filesystem ($(( $(stat -c %s "$rootfs") / 1048576 )) MiB)"
"${m[@]}" "$rootfs" ::IOS7/ROOTFS.HFS

echo "==> Writing disk image $out"
rm -f "$out"
truncate -s $(( total_sectors * 512 )) "$out"
printf 'label: dos\n%d,%d,c\n' "$start_sector" "$part_sectors" | sfdisk -q "$out"
dd if="$part" of="$out" bs=1M seek=$(( start_sector / 2048 )) conv=notrunc,sparse status=none
echo "==> Done: $out"
mdir -i "$out@@$(( start_sector * 512 ))" ::
