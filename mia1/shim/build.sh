#!/bin/bash
# Build the Mi A1 boot images for the stock bootloader (nothing is written to the phone by
# `fastboot boot`; a reboot brings back whatever was there):
#   OUT/mia1-ios.img        the shim, HackiOS's loader and kernel: iOS from the userdata partition
#   OUT/mia1-ios-4cpu.img   the same on all four cores of the first cluster
# Usage: build.sh OUT_DIR HACKIOS_TREE
# HACKIOS_TREE: a HackiOS checkout with hackios/hackios-xnu-kernel.diff applied, the kernel,
# xnuloader and userland/root.apfs built. The iOS system (userland/system.apfs) goes to the phone
# separately: fastboot flash userdata system.apfs.
set -euo pipefail
# SHIM_CFLAGS=-DSHIM_UART=0x09000000 SHIM_CMDLINE="...": a build for QEMU (mia1/qemu-shim.sh)
SHIM_CFLAGS=${SHIM_CFLAGS:-}
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$(mkdir -p "${1:?usage: build.sh OUT_DIR HACKIOS_TREE}" && cd "$1" && pwd)"
hack="$(cd "${2:?usage: build.sh OUT_DIR HACKIOS_TREE}" && pwd)"
kernel=$hack/xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837
for f in "$kernel" "$hack/xnuloader/loader.efi" "$hack/userland/root.apfs"; do
	[ -f "$f" ] || { echo "build.sh: $f is missing" >&2; exit 1; }
done

flags=(--target=aarch64-none-elf -mgeneral-regs-only -ffreestanding -fno-builtin -nostdlib
       -fpie -fno-stack-protector -mstrict-align -O2 -Wall -Wextra -Werror -Wno-format -I"$hack/xnuloader" $SHIM_CFLAGS)
objs=()
for src in start.S main.c console.c efi.c fdt.c mmu.c libc.c; do
	clang "${flags[@]}" -c "$here/$src" -o "$out/${src%.*}.o"
	objs+=("$out/${src%.*}.o")
done
ld.lld -T "$here/shim.ld" --pie -z norelro --no-dynamic-linker -o "$out/shim.elf" "${objs[@]}"
# only relocations the entry code applies itself
if llvm-readelf -r "$out/shim.elf" | grep "R_AARCH64" | grep -vq "R_AARCH64_RELATIVE"; then
	echo "shim: relocations other than R_AARCH64_RELATIVE" >&2; exit 1
fi
llvm-objcopy -O binary "$out/shim.elf" "$out/shim.bin"
dtc -q -I dts -O dtb -o "$out/tissot.dtb" "$here/tissot.dts"
gzip -9 -n -c "$out/shim.bin" > "$out/shim.bin.gz"
cat "$out/shim.bin.gz" "$out/tissot.dtb" > "$out/kernel-dtb"

image() {   # NAME CMDLINE
	python3 "$hack/xnuloader/mkpayload.py" -o "$out/payload-$1.bin" kernel="$kernel" ramdisk="$hack/userland/root.apfs" \
		--rdres 4000 --cmdline "$2 epoch=$(date +%s)" >/dev/null
	python3 "$here/mkcontainer.py" "$out/ramdisk-$1" loader="$hack/xnuloader/loader.efi" payload="$out/payload-$1.bin"
	python3 "$hack/prebuilt/uefi/mkbootimg.py" --header_version 0 --kernel "$out/kernel-dtb" \
		--ramdisk "$out/ramdisk-$1" --base 0x80000000 --pagesize 2048 --cmdline "rpiios" -o "$out/$1.img"
	echo "shim: $out/$1.img ($(stat -c %s "$out/$1.img") bytes): $2"
}
# tissot=1: the Mi A1's drivers (eMMC, touch, buttons); emmc_root: the iOS volume on userdata;
# -v: the kernel's text on the screen, the only log there is
if [ -n "${WDT_TESTS:-}" ]; then
	# the watchdog tests: count 40 s, then (modes 8 and 4) boot as mia1-ios.img does
	for mode in 0 8 4; do
		SHIM_CFLAGS="-DWDT_MODE=$mode -DWDT_WATCH_S=40" WDT_TESTS= "$0" "$out/wdt$mode" "$hack" >/dev/null
		cp "$out/wdt$mode/mia1-ios.img" "$out/mia1-wdt$mode.img"
		echo "shim: $out/mia1-wdt$mode.img"
	done
	exit 0
fi
if [ -n "${SHIM_CMDLINE:-}" ]; then
	image mia1-test "$SHIM_CMDLINE"
	exit 0
fi
image mia1-ios "-v tissot=1 emmc_root=userdata"
image mia1-ios-4cpu "-v tissot=1 emmc_root=userdata cpus=4"
