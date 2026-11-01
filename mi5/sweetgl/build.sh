#!/bin/bash
# sailfish's sweetgl.dylib (Mesa freedreno + GLES + its EAGL frontend, loaded into backboardd so
# that CoreAnimation draws on the Adreno 530), built on Linux: sailfish's tools/sweetgl/build.sh
# and mesa-setup.sh with HackiOS's cross clang + ld64 and Theos's iOS 16.5 SDK in place of Xcode.
#
# usage: mi5/sweetgl/build.sh WORKDIR HACKIOS_TREE     -> WORKDIR/sweetgl/sweetgl.dylib
#   WORKDIR       mi5/kernel/setup.sh's (its sailfish/ is used)
#   HACKIOS_TREE  with xnu-deps/setup.sh run (xnu-clang, ld64) and tools/mk-mesa.sh's venv
#                 (meson, Mako) or meson >= 1.4 and Mako on the PATH
# With it there, mkroot.sh installs it as /usr/lib/sweetgl.dylib and mkboot.sh turns it on.
#
# Mesa 26.1.0: the release whose sources sailfish's mesa.patch was made against (its blob ids),
# plus mesa-darwin.patch (26.1's freedreno reads the RAM size with Linux's sysinfo()).
set -euo pipefail
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=$(cd "${1:?usage: build.sh WORKDIR HACKIOS_TREE}" && pwd)
H=$(cd "${2:?usage: build.sh WORKDIR HACKIOS_TREE}" && pwd)
G=$W/sailfish/tools/sweetgl
VERSION=26.1.0
SHA256=a5095e6dc2986c78f0cef4c5555dc803e93b6bfe5670e991f9e8bd49395bae19
source "$H/xnu-env.sh"
SDK=$THEOS/sdks/iPhoneOS16.5.sdk
LDID=$THEOS/toolchain/linux/iphone/bin/ldid
[[ -x $H/xnu-deps/host/bin/xnu-clang ]] || { echo "build.sh: run $H/xnu-deps/setup.sh first" >&2; exit 1; }
[[ -x $LDID ]] || LDID=$(command -v ldid) || { echo "build.sh: no ldid (Theos's toolchain has one)" >&2; exit 1; }
[[ -d $H/xnu-deps/build/mesa/venv/bin ]] && export PATH=$H/xnu-deps/build/mesa/venv/bin:$PATH
command -v meson >/dev/null || { echo "build.sh: meson >= 1.4 and Mako are needed (pip install meson mako)" >&2; exit 1; }

S=$W/sweetgl
M=$S/mesa-$VERSION
B=$S/mesa-ios
O=$S/out
mkdir -p "$S" "$O"
if [[ ! -d $M ]]; then
	[[ -f $S/mesa-$VERSION.tar.xz ]] || curl -fL -o "$S/mesa-$VERSION.tar.xz" "https://archive.mesa3d.org/mesa-$VERSION.tar.xz"
	echo "$SHA256  $S/mesa-$VERSION.tar.xz" | sha256sum -c --quiet
	tar -xJf "$S/mesa-$VERSION.tar.xz" -C "$S"
	patch -d "$M" -p1 -s < "$G/mesa.patch"
	patch -d "$M" -p1 -s < "$HERE/mesa-darwin.patch"
fi

CCW=$H/xnu-deps/host/bin/xnu-clang
cat > "$S/cross.ini" <<EOF
[binaries]
c = ['$CCW', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=16.0']
cpp = ['$CCW++', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=16.0', '-fuse-cxa-atexit']
objc = ['$CCW', '-arch', 'arm64', '-isysroot', '$SDK', '-miphoneos-version-min=16.0']
ar = 'llvm-ar'
ranlib = 'llvm-ranlib'
strip = 'llvm-strip'
pkg-config = 'false'

[built-in options]
c_args = ['-I$G/include']
cpp_args = ['-I$G/include']

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'

[properties]
needs_exe_wrapper = true
EOF
# sailfish's mesa-setup.sh options
[[ -f $B/build.ninja ]] || meson setup "$B" "$M" --cross-file "$S/cross.ini" --wrap-mode=nofallback \
	--buildtype=release -Db_ndebug=true -Ddefault_library=static \
	-Dplatforms= -Degl=disabled -Dglx=disabled -Dgbm=disabled \
	-Dgallium-drivers=freedreno -Dvulkan-drivers= -Dfreedreno-kmds=msm \
	-Dllvm=disabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
	-Dzstd=disabled -Dzlib=disabled -Dexpat=disabled -Dxmlconfig=disabled \
	-Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled \
	-Dbuild-tests=false -Dshader-cache=disabled -Dperfetto=false \
	-Dgallium-va=disabled -Dgallium-rusticl=false -Dtools= \
	-Dhtml-docs=disabled -Ddisplay-info=disabled -Dteflon=false > "$S/meson-setup.log"

# sailfish's tools/sweetgl/build.sh from here on: the same libraries, sources and link
LIBS=$(sed -n '/^LIBS="/,/"$/p' "$G/build.sh" | tr -d '"' | sed 's/^LIBS=//')
(cd "$B" && ninja $LIBS > "$S/ninja.log") || { tail -30 "$S/ninja.log" >&2; exit 1; }
llvm-nm -g "$B/src/mesa/glapi/glapi/libglapi_bridge.a" > "$O/bridge.nm"
CC="$CCW -arch arm64 -isysroot $SDK -miphoneos-version-min=16.0"
INC="-I$G/include -I$M/include -I$M/src -I$M/src/mesa -I$M/src/mapi -I$M/src/gallium/include
	-I$M/src/gallium/auxiliary -I$M/src/gallium/drivers -I$M/src/gallium/winsys
	-I$B/src -I$B/src/mesa -I$B/src/mapi"
python3 -I -c '
import json, shlex, sys
cc = json.load(open(sys.argv[1]))
e = [x for x in cc if x["file"].endswith("state_tracker/st_manager.c")][0]
print("\n".join(shlex.quote(a) for a in shlex.split(e["command"]) if a.startswith("-D")))
' "$B/compile_commands.json" > "$O/mesa-defines.rsp"
python3 -I "$G/gen-dispatch.py" "$G/ios-dispatch.txt" "$M/include" "$O/bridge.nm" "$O/dispatch.c"
for f in sweetgl sweet_drm eagl layerdump "$O/dispatch"; do
	case $f in /*) src=$f.c ;; *) src=$G/$f.c ;; esac
	$CC -O2 -Wall -fvisibility=hidden @"$O/mesa-defines.rsp" $INC -I"$G" -c "$src" -o "$O/$(basename "$f").o"
done
printf '_sweetgl_init\n_sweetgl_create\n_sweetgl_make_current\n_sweetgl_destroy\n_sweetgl_selftest\n' > "$O/exports.txt"
"$CCW++" -arch arm64 -isysroot "$SDK" -miphoneos-version-min=16.0 -fuse-cxa-atexit -dynamiclib -dead_strip \
	-install_name /usr/lib/sweetgl.dylib -exported_symbols_list "$O/exports.txt" \
	-o "$O/sweetgl.dylib" "$O/sweetgl.o" "$O/sweet_drm.o" "$O/eagl.o" "$O/layerdump.o" "$O/dispatch.o" \
	$(for l in $LIBS; do echo "$B/$l"; done) \
	-framework IOKit -framework CoreFoundation -framework IOSurface -framework OpenGLES -framework CoreGraphics \
	-lobjc -lc++ -lz 2> >(grep -v '^ld: warning' >&2)
"$LDID" -S "$O/sweetgl.dylib"	# ad hoc, as sailfish's `codesign -f -s -`
n=$(llvm-nm -gU "$O/sweetgl.dylib" | grep -c ' _sweetgl_')
[[ $n == 5 ]] || { echo "build.sh: sweetgl.dylib exports $n of the 5 sweetgl_* functions" >&2; exit 1; }
SIZE=$(stat -c %s "$O/sweetgl.dylib")
(( SIZE <= 67108864 )) || { echo "build.sh: sweetgl.dylib is $SIZE bytes, over 64 MiB" >&2; exit 1; }
# zero-padded to 64 MiB as sailfish does: the file keeps one size and can be rewritten in place
cp "$O/sweetgl.dylib" "$S/sweetgl.dylib"
truncate -s 67108864 "$S/sweetgl.dylib"
echo "build.sh: $S/sweetgl.dylib ($SIZE bytes, padded to 64 MiB)"
