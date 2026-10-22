#!/bin/bash
# The Mi A1 boot chain under QEMU: the shim (built with a PL011 for its text) started the way
# aboot starts it -- as an arm64 Linux kernel, its MIA1BOOT container as the initrd -- runs
# HackiOS's loader under its own UEFI, which starts the kernel; the iOS root from an SD card
# through the eMMC driver, as in qemu-emmc.sh. QEMU has no MDP, so the display is not switched
# and the loader runs without one: everything is on the serial line.
# usage: mia1/qemu-shim.sh HACKIOS_TREE DISK.IMG [seconds]
# KERNEL_ARGS: more of the kernel's arguments. SHIM_ARGS="fbw=750 fbh=1624": the boot image's
# command line (the scaled mode's picture is then in memory at 0x90101000, 4 bytes a pixel:
# QEMU's monitor on 127.0.0.1:4445 can save it with pmemsave)
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
hack=$(readlink -f "$1"); disk=$(readlink -f "$2"); secs=${3:-600}
out=$here/../out/mia1/qemu
SHIM_CFLAGS=-DSHIM_UART=0x09000000 SHIM_CMDLINE="emmc_pci=1 emmc_root=userdata loader_return_after=$secs ${KERNEL_ARGS:-}" \
	"$here/shim/build.sh" "$out" "$hack" || exit 1
gunzip -c "$out/shim.bin.gz" > "$out/Image"
exec timeout $((secs + 60)) qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 -m 4G -smp 1 \
	-kernel "$out/Image" -initrd "$out/ramdisk-mia1-test" \
	-device sdhci-pci -drive if=none,id=sd,file="$disk",format=raw -device sd-card,drive=sd \
	-append "rpiios ${SHIM_ARGS:-}" -display none -serial stdio -monitor tcp:127.0.0.1:4445,server,nowait -no-reboot
