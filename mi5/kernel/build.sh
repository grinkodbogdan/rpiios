#!/bin/bash
# Build the Mi 5 kernel (sailfish's SWEET machine config) on Linux.
# usage: mi5/kernel/build.sh WORKDIR HACKIOS_TREE [make args...]
# The kernel is built for the macOS platform, as darwin-xnu-build builds it (SDKROOT=macosx): the
# compiler targets arm64-apple-macos13.5 (bin/xnu-clang), and the iOS 16.5 SDK overlay of HackiOS
# stands in for the SDK, with libfirehose_kernel rebuilt for that target (WORKDIR/sdk).
# Result: WORKDIR/xnu/BUILD/obj/DEVELOPMENT_ARM64_SWEET/kernel.development.sweet
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=$(cd "${1:?usage: build.sh WORKDIR HACKIOS_TREE}" && pwd); shift
H=$(cd "${1:?usage: build.sh WORKDIR HACKIOS_TREE}" && pwd); shift
source "$H/xnu-env.sh"
B=$W/bin
mkdir -p "$B"
cp "$HERE"/bin/sed "$HERE"/bin/xcrun "$B/"
sed -i "s|^exec .*hackios-xnu-kernel/xnu-deps/host/bin/xcrun|exec $XNUDEPS/host/bin/xcrun|" "$B/xcrun"
for n in xnu-clang xnu-clang++; do
	/usr/bin/sed -e "s|HB=\"\$(cd \"\$(dirname \"\$0\")\" \&\& pwd)\"|HB=$XNUDEPS/host/bin|" \
		-e 's|-target arm64-apple-ios${XNU_SDK_VERSION:-16.5}|-target arm64-apple-macos13.5|' "$XNUDEPS/host/bin/xnu-clang" > "$B/$n"
	chmod +x "$B/$n"
done
export PATH=$B:$PATH
SDK=$W/sdk/iPhoneOS16.5.xnu.sdk
if [ ! -f "$W/sdk/.firehose-macos" ]; then
	rm -rf "$W/sdk"; mkdir -p "$W/sdk"; cp -a "$XNU_SDK_PATH" "$SDK"
	# the kernel's headers first: the firehose library is compiled against them
	(cd "$W/xnu" && make SDKROOT=macosx PLATFORM=MacOSX ARCH_CONFIGS=ARM64 MACHINE_CONFIGS=SWEET KERNEL_CONFIGS=DEVELOPMENT \
		XNU_SDK_PATH="$SDK" HOST_CC="clang -include $XNUDEPS/hostinc/hostcompat.h -I$XNUDEPS/hostinc -Wno-unused-command-line-argument" \
		HOST_FLEX=flex HOST_BISON=bison HOST_GM4=m4 CC=xnu-clang CXX=xnu-clang++ -j"$(nproc)" exporthdrs > "$W/exporthdrs.log" 2>&1)
	hdrs=$W/xnu/BUILD/obj/EXPORT_HDRS
	mkdir -p "$W/firehose"
	(cd "$XNUDEPS" && xnu-clang -arch arm64 -mcpu=kryo -c -Os -mkernel -nostdinc -fno-builtin \
		-DKERNEL=1 -DKERNEL_PRIVATE=1 -DXNU_KERNEL_PRIVATE=0 -DPRIVATE=1 -D__DARWIN_64_BIT_INO_T=1 \
		-DDISPATCH_USE_DTRACE=0 -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
		-DOS_ATOMIC_CONFIG_STARVATION_FREE_ONLY=0 -DOS_FIREHOSE_SPI=1 -DARM64_BOARD_CONFIG_SWEET \
		-Ilibdispatch -Ilibdispatch/src -Ilibdispatch/src/firehose \
		-I$hdrs/osfmk -I$hdrs/bsd -I$hdrs/libkern -I$hdrs/pexpert -I$hdrs/iokit -I$hdrs/security \
		-I$W/xnu/libkern/firehose -I$hdrs/libkern/os -I$W/xnu/EXTERNAL_HEADERS -I$SDK/usr/local/include/kernel \
		-o "$W/firehose/firehose_buffer.o" libdispatch/src/firehose/firehose_buffer.c)
	mkdir -p "$SDK/usr/local/lib/kernel"
	rm -f "$SDK/usr/local/lib/kernel/libfirehose_kernel.a"
	libtool -static -o "$SDK/usr/local/lib/kernel/libfirehose_kernel.a" "$W/firehose/firehose_buffer.o"
	touch "$W/sdk/.firehose-macos"
fi
export XNU_SDK_PATH=$SDK
cd "$W/xnu"
exec make SDKROOT=macosx PLATFORM=MacOSX ARCH_CONFIGS=ARM64 MACHINE_CONFIGS=SWEET KERNEL_CONFIGS=${KC:-DEVELOPMENT} \
  ARCH_STRING_FOR_CURRENT_MACHINE_CONFIG=arm64 BUILD_WERROR=0 BUILD_LTO=0 DO_CTFMERGE=0 RC_DARWIN_KERNEL_VERSION=22.6.0 \
  HOST_CC="clang -include $XNUDEPS/hostinc/hostcompat.h -I$XNUDEPS/hostinc -Wno-unused-command-line-argument -Wno-error=implicit-function-declaration" \
  HOST_FLEX=flex HOST_BISON=bison HOST_GM4=m4 CC=xnu-clang CXX=xnu-clang++ LOGCOLORS=n -j"$(nproc)" -k "$@"
