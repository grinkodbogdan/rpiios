#!/bin/bash
# The Mi 5's boot image, for `fastboot boot` (nothing is flashed to boot): sailfish's shim with
# mi5's changes (sailfish.patch: shim/mi5.c), and appended to it XNU (mi5/kernel), the Apple device
# tree (tools/mkadt.py --mi5, from the iPhone 8's own D20AP tree), the boot-args and the Adreno 530
# firmware; gzipped, the Mi 5's device trees after it (mi5/dtb.sh), in an Android v0 boot image.
#
# usage: mi5/mkboot.sh WORKDIR FWDIR [OUT.img]          (default WORKDIR/mi5-boot.img)
#   WORKDIR  mi5/kernel/setup.sh's and build.sh's (sailfish/, xnu/BUILD/obj/kernel.development.sweet)
#   FWDIR    the phone's own a530_pm4.fw, a530_pfp.fw, a530_zap.mdt, a530_zap.b02
#            (from LineageOS on the phone: adb pull /vendor/firmware/<name>; mi5/README.md)
#            and, for Wi-Fi, the QCA6174's otp30.bin, qwlan30.bin, bdwlan30.bin and any
#            bdwlan30.bNN (board variants): with them there, Wi-Fi is turned on (sailfishwlan=1)
# Needs: clang, ld.lld, llvm-objcopy/-nm/-readelf, gzip, cpp, dtc, lzfse, python3.
#   ARGS_EXTRA  more boot-args (sailfishssid=, sailfishpsk=, ...); MI5_ARGS replaces the defaults
#   NOGL=1      CPU drawing even when sweetgl.dylib is there
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=$(cd "${1:?usage: mkboot.sh WORKDIR FWDIR [OUT.img]}" && pwd)
F=$(cd "${2:?usage: mkboot.sh WORKDIR FWDIR [OUT.img]}" && pwd)
out=${3:-$W/mi5-boot.img}
SF=$W/sailfish
K=$W/xnu/BUILD/obj/kernel.development.sweet
IPSW=https://updates.cdn-apple.com/2025FallFCS/fullrestores/122-77002/0281BFC6-38ED-485A-A8BE-8871861174B1/iPhone_4.7_P3_16.7.16_20H392_Restore.ipsw

[[ -f $K ]] || { echo "mkboot.sh: no kernel at $K: run mi5/kernel/build.sh first" >&2; exit 1; }
for f in a530_pm4.fw a530_pfp.fw a530_zap.mdt a530_zap.b02; do
	[[ -s $F/$f ]] || { echo "mkboot.sh: $F/$f missing" >&2; exit 1; }
done

# sailfish with the Mi 5 shim (once)
if ! git -C "$SF" apply -R --check "$HERE/sailfish.patch" 2>/dev/null; then
	git -C "$SF" apply --whitespace=nowarn "$HERE/sailfish.patch"
fi
[[ -d $W/dtbs ]] || "$HERE/dtb.sh" "$W"

# The iPhone 8's device tree: MobileGestalt reads /product of it (an Apple file: kept in WORKDIR)
adt8=$SF/research/ipsw-8/d20ap.adt
if [[ ! -s $adt8 ]]; then
	mkdir -p "$(dirname "$adt8")"
	python3 -I "$HERE/tools/ipsw.py" "$IPSW" get Firmware/all_flash/DeviceTree.d20ap.im4p "$adt8.im4p"
	python3 -I "$SF/tools/adt.py" unwrap "$adt8.im4p" "$adt8"
fi
B=$W/mi5-build
mkdir -p "$B"
python3 -I "$SF/tools/mkadt.py" "$B/mi5.adt" --mi5

# The Adreno microcode without its 4-byte header (sailfish README step 3)
tail -c +5 "$F/a530_pm4.fw" > "$B/pm4.bin"
tail -c +5 "$F/a530_pfp.fw" > "$B/pfp.bin"

# sailfish's boot-args (README step 4) for the Mi 5: verbose boot (the log stays on screen until
# SpringBoard), no backlight driver (the Mi 5's is the WLED, which LK leaves on, not the panel's
# DCS brightness the Pixel's driver sends), no DDR clock request (the 820, not the 821), Wi-Fi
# only with its firmware in FWDIR, and the GPU for CoreAnimation (sweetgl.dylib injected into
# backboardd) only when mi5/sweetgl/build.sh has built it, which is also when mkroot.sh installs
# it: without it CoreAnimation draws on the CPU. sweetfb_noconsole=1: the kernel log stops being
# drawn at the first frame iOS shows (it still goes to the logs; panics still draw).
GL=
if [[ -s $W/sweetgl/sweetgl.dylib && -z ${NOGL:-} ]]; then
	GL=" sweetenv=backboardd:DYLD_INSERT_LIBRARIES=/usr/lib/sweetgl.dylib"
	echo "mkboot.sh: sweetgl.dylib built: GPU drawing on (the root must be made by mkroot.sh after it)"
elif [[ -n ${NOGL:-} ]]; then
	echo "mkboot.sh: NOGL set: CoreAnimation draws on the CPU"
else
	echo "mkboot.sh: no $W/sweetgl/sweetgl.dylib: CoreAnimation draws on the CPU"
fi
# Wi-Fi: the QCA6174 on PCIe RC0 is wired as on the Pixel (sailfish_wlan.c); the OTP program
# reads the board id and the kernel picks WLAN-BDF-<id>, else WLAN-BDF
WLAN=() wlan=0
if [[ -s $F/otp30.bin && -s $F/qwlan30.bin && -s $F/bdwlan30.bin ]]; then
	WLAN=(--blob WLAN-OTP="$F/otp30.bin" --blob WLAN-FW="$F/qwlan30.bin" --blob WLAN-BDF="$F/bdwlan30.bin")
	for b in "$F"/bdwlan30.b[0-9a-f][0-9a-f]; do
		[[ -s $b ]] && WLAN+=(--blob "WLAN-BDF-${b##*.b}=$b")
	done
	wlan=1
	echo "mkboot.sh: Wi-Fi firmware found: $(( ${#WLAN[@]} / 2 )) files"
else
	echo "mkboot.sh: no otp30.bin/qwlan30.bin/bdwlan30.bin in $F: Wi-Fi off"
fi
ARGS=${MI5_ARGS:-"debug=0xc serial=3 cpus=4 keepsyms=1 -v sweetusb=0 rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetsample=- sweetnull=backboardd cs_enforcement_disable=1 sweetprof=- sweetgpu=8 sweetgpuclk=600 sweetbl=0 sweetfb_noconsole=1 sailfishwlan=$wlan$GL"}
ARGS="$ARGS${ARGS_EXTRA:+ $ARGS_EXTRA}"
# the shim adds its sweetfb_* (about 110 bytes) to these; the kernel takes 608 at most
(( ${#ARGS} <= 480 )) || { echo "mkboot.sh: boot-args too long (${#ARGS} bytes, 480 at most)" >&2; exit 1; }
python3 -I "$SF/tools/mkpayload.py" "$K" "$B/mi5.adt" "$B/payload" --cmdline "$ARGS" \
	--blob GPU-PM4="$B/pm4.bin" --blob GPU-PFP="$B/pfp.bin" \
	--blob GPU-ZAP-MDT="$F/a530_zap.mdt" --blob GPU-ZAP-SEG="$F/a530_zap.b02" "${WLAN[@]}" > "$B/payload.log"
bash "$SF/shim/build-mi5.sh" "$W/dtbs" "$B/payload"
cp "$SF/shim/out/mi5-boot.img" "$out"
ls -l "$out"
echo "mkboot.sh: boot it with  fastboot boot $out"
