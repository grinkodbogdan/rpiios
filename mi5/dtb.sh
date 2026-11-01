#!/bin/bash
# The Mi 5's device trees (gemini: Xiaomi's board files a1-*), compiled from LineageOS's kernel, for
# appending to the shim's Image: LK, the Mi 5's bootloader, picks the one for the board (msm-id,
# board-id) and fills in /memory and /chosen. Only the shim reads them (the memory banks); XNU gets
# its own Apple device tree (sailfish's tools/mkadt.py --mi5).
# usage: mi5/dtb.sh WORKDIR     -> WORKDIR/dtbs/*.dtb   (needs git, cpp, dtc)
set -euo pipefail
W=$(mkdir -p "${1:?usage: dtb.sh WORKDIR}" && cd "$1" && pwd)
K=$W/lineage-kernel
SHA=535079d2fa367e4a4ea2111e5903c62dced89cd2   # LineageOS/android_kernel_xiaomi_msm8996, lineage-22.2
if [[ ! -d $K/.git ]]; then
	git init -q "$K"
	git -C "$K" sparse-checkout set --no-cone /arch/arm64/boot/dts/ /arch/arm/boot/dts/qcom/ /include/dt-bindings/ /scripts/dtc/include-prefixes/
	git -C "$K" fetch -q --depth 1 --filter=blob:none https://github.com/LineageOS/android_kernel_xiaomi_msm8996 "$SHA"
	git -C "$K" checkout -q FETCH_HEAD
fi
mkdir -p "$W/dtbs"
cd "$K"
for d in arch/arm64/boot/dts/qcom/a1-*.dts; do
	n=$(basename "$d" .dts)
	cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp -I arch/arm64/boot/dts/qcom -I arch/arm64/boot/dts \
		-I include "$d" | dtc -q -I dts -O dtb -o "$W/dtbs/$n.dtb" -
	echo "dtb.sh: $W/dtbs/$n.dtb"
done
