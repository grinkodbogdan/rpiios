#!/usr/bin/env bash
# Build the Raspberry Pi boot shim (shim.bin) with clang + ld.lld.
# Usage: shim/build.sh OUT_DIR
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:?usage: build.sh OUT_DIR}"
mkdir -p "$out"

# armv7ve: needs the virtualization extensions for msr elr_hyp / eret.
flags=(--target=armv7a-none-eabi -march=armv7ve -marm -mfloat-abi=soft
       -mno-unaligned-access -ffreestanding -fno-builtin -nostdlib
       -fno-pic -fno-stack-protector -O2 -Wall -Wextra -Werror)
clang "${flags[@]}" -c "$here/start.S" -o "$out/start.o"
clang "${flags[@]}" -c "$here/shim.c" -o "$out/shim.o"
ld.lld -T "$here/shim.ld" -o "$out/shim.elf" "$out/start.o" "$out/shim.o"
llvm-objcopy -O binary "$out/shim.elf" "$out/shim.bin"
echo "shim: $(stat -c %s "$out/shim.bin") bytes"
