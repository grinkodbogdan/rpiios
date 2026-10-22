#!/bin/bash
# The Mi A1's storage path under QEMU: iOS 16.5 (HackiOS) on a Cortex-A53 with a GICv2 and 4 GB of
# memory, as on the phone, its root not in memory but read from a disk through the Mi A1's eMMC
# driver (bsd/dev/redwood_tissot_emmc.c, emmc_root=userdata). QEMU has no eMMC model, so the disk is
# an SD card on QEMU's sdhci-pci (emmc_pci=1): the same SDHCI code, the SD card's way of being
# identified instead of the eMMC's.
#
# usage (in a HackiOS tree with hackios/hackios-xnu-kernel.diff applied and the kernel built):
#   mia1/qemu-emmc.sh DISK.IMG [seconds]
# DISK.IMG: a GPT disk of a power of two in size (QEMU's SD card wants that) with a partition
# named userdata holding userland/system.apfs, e.g.
#   truncate -s 4G disk.img
#   echo 'label: gpt
#   start=2048, size=5000000, name="userdata"' | sfdisk disk.img
#   dd if=userland/system.apfs of=disk.img bs=1M seek=1 conv=notrunc,sparse
# The kernel's own image (root.apfs, /dev/md1 for stage0) travels in the payload; a 16 KB stand-in
# for the resident image keeps it md1 (the loader makes the resident image md0).
DISK=$(readlink -f "${1:?usage: qemu-emmc.sh DISK.IMG [seconds]}")
SECS=${2:-600}
K=xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837
[ -f "$K" ] && [ -f userland/root.apfs ] || { echo "qemu-emmc.sh: run it in a HackiOS tree with the kernel and userland built"; exit 1; }
xnuloader/mkpayload.py -o qemu/payload.bin kernel=$K ramdisk=userland/root.apfs --rdres 4000 \
	--cmdline "emmc_root=userdata emmc_pci=1 loader_return_after=$SECS epoch=$(date +%s) virtio_input=1 $CMDLINE" >/dev/null || exit 1
QEMU_GIC=2 QEMU_CPU=cortex-a53 QEMU_MEM=${QEMU_MEM:-4G} \
QEMU_EFI=${QEMU_EFI:-/usr/share/AAVMF/AAVMF_CODE.fd} QEMU_VARS=${QEMU_VARS:-/usr/share/AAVMF/AAVMF_VARS.fd} \
exec qemu/run.sh $((SECS + 40)) \
	-device sdhci-pci -drive if=none,id=sd,file="$DISK",format=raw -device sd-card,drive=sd \
	-global virtio-mmio.force-legacy=false -device virtio-tablet-device -device virtio-keyboard-device \
	-device ramfb -monitor tcp:127.0.0.1:4444,server,nowait
