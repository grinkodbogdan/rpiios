#!/bin/bash
# Build the Mi A1 shim as an Android boot image for the stock bootloader:
#   fastboot boot OUT/mia1-hello.img
# (nothing is written to the phone; a reboot brings back whatever was there).
# Usage: build.sh OUT_DIR [MKBOOTIMG]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:?usage: build.sh OUT_DIR [MKBOOTIMG]}"
mkbootimg="${2:-$here/../../../oosexgang/hackios-xnu-kernel/prebuilt/uefi/mkbootimg.py}"
mkdir -p "$out"
flags=(--target=aarch64-none-elf -mgeneral-regs-only -ffreestanding -fno-builtin -nostdlib
       -fpie -fno-stack-protector -O2 -Wall -Wextra -Werror)
clang "${flags[@]}" -c "$here/start.S" -o "$out/start.o"
clang "${flags[@]}" -c "$here/main.c" -o "$out/main.o"
ld.lld -T "$here/shim.ld" --pie -z norelro -o "$out/shim.elf" "$out/start.o" "$out/main.o"
# position independent: nothing for a loader to relocate
if llvm-readelf -r "$out/shim.elf" | grep -q "R_AARCH64"; then
	echo "shim: has relocations" >&2; exit 1
fi
llvm-objcopy -O binary "$out/shim.elf" "$out/shim.bin"
dtc -I dts -O dtb -o "$out/tissot.dtb" "$here/tissot.dts"
# Image.gz-dtb, like the phone's own kernels: aboot gunzips the image and finds the device tree
# right after the compressed stream
gzip -9 -n -c "$out/shim.bin" > "$out/shim.bin.gz"
cat "$out/shim.bin.gz" "$out/tissot.dtb" > "$out/kernel-dtb"
python3 "$mkbootimg" --header_version 0 --kernel "$out/kernel-dtb" --base 0x80000000 \
	--pagesize 2048 --cmdline "rpiios" -o "$out/mia1-hello.img"
echo "shim: $(stat -c %s "$out/shim.bin") bytes, image $out/mia1-hello.img"
