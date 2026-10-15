#!/usr/bin/env bash
# Build everything needed to compile the 32-bit ARM XNU kernel on Linux:
#   - cctools-port (Apple ld64, as, nm, otool, ...) targeting arm-apple-darwin11
#   - libdispatch (cctools-port needs it on Linux)
#   - migcom from bootstrap_cmds-86 (iOS 7 era), with a 32-bit Mach type shim
#   - compiler wrappers and an xcrun stand-in for xnu's makedefs
#   - setsegname / kextsymboltool host tools
#
# Usage: toolchain/build-toolchain.sh [OUT_DIR]     (default: ./out/toolchain)
# Tested on Ubuntu 24.04 (x86_64) with clang 18.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$(realpath -m "${1:-$here/../out/toolchain}")"
SRC="$OUT/src"
PREFIX="$OUT/prefix"
WRAP="$OUT/bin"
JOBS="${JOBS:-$(nproc)}"

CCTOOLS_REPO=https://github.com/tpoechtrager/cctools-port.git
CCTOOLS_COMMIT=904de2a71d4da6a9b30d2efaf912a10ddc7d9ddb
DISPATCH_REPO=https://github.com/swiftlang/swift-corelibs-libdispatch.git
DISPATCH_COMMIT=ce1b4d3540427a8c8a17a3b5eb7c5749665d384e
BOOTSTRAP_REPO=https://github.com/apple-oss-distributions/bootstrap_cmds.git
BOOTSTRAP_TAG=bootstrap_cmds-86

log() { echo "==> $*"; }

fetch() {  # repo dir ref
    if [[ ! -d "$SRC/$2/.git" ]]; then
        git clone -q "$1" "$SRC/$2"
    fi
    git -C "$SRC/$2" fetch -q --depth 1 origin "$3" 2>/dev/null || git -C "$SRC/$2" fetch -q origin
    git -C "$SRC/$2" checkout -q --detach "$3" 2>/dev/null || git -C "$SRC/$2" checkout -q --detach FETCH_HEAD
}

mkdir -p "$SRC" "$PREFIX" "$WRAP"

if command -v apt-get >/dev/null && [[ $EUID -eq 0 ]]; then
    log "Installing host packages"
    apt-get install -y --no-install-recommends clang lld llvm cmake ninja-build \
        bison flex unifdef tcsh uuid-dev libssl-dev libxml2-dev \
        libblocksruntime-dev perl python3 >/dev/null
fi

# --- libdispatch -----------------------------------------------------------
if [[ ! -f "$OUT/dispatch/lib/libdispatch.so" ]]; then
    log "Building libdispatch"
    fetch "$DISPATCH_REPO" libdispatch "$DISPATCH_COMMIT"
    cmake -S "$SRC/libdispatch" -B "$SRC/libdispatch-build" -G Ninja \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_INSTALL_PREFIX="$OUT/dispatch" -DBUILD_TESTING=OFF >/dev/null
    ninja -C "$SRC/libdispatch-build" install >/dev/null
fi
export LD_LIBRARY_PATH="$OUT/dispatch/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# --- cctools-port / ld64 ---------------------------------------------------
if [[ ! -x "$PREFIX/bin/arm-apple-darwin11-ld" ]]; then
    log "Building cctools-port (ld64, as, nm, otool)"
    fetch "$CCTOOLS_REPO" cctools-port "$CCTOOLS_COMMIT"
    (cd "$SRC/cctools-port/cctools" &&
        ./configure --prefix="$PREFIX" --target=arm-apple-darwin11 \
            --with-libdispatch="$OUT/dispatch" CC=clang CXX=clang++ >/dev/null &&
        make -j"$JOBS" >/dev/null && make install >/dev/null)
fi
CCTOOLS="$SRC/cctools-port/cctools"

# --- MIG --------------------------------------------------------------------
if [[ ! -x "$PREFIX/libexec/migcom" ]]; then
    log "Building migcom ($BOOTSTRAP_TAG)"
    rm -rf "$SRC/bootstrap_cmds"
    git clone -q --depth 1 -b "$BOOTSTRAP_TAG" "$BOOTSTRAP_REPO" "$SRC/bootstrap_cmds"
    m="$SRC/migbuild"; rm -rf "$m"; cp -r "$SRC/bootstrap_cmds/migcom.tproj" "$m"
    (cd "$m" &&
        bison -d -y -o parser.c parser.y 2>/dev/null && cp parser.h y.tab.h &&
        flex -o lexxer.c lexxer.l &&
        # handler.c is dead code: Apple's own project does not build it.
        clang -w -D_GNU_SOURCE -DMIG_VERSION='"bootstrap_cmds-86 (rpiios Linux shim)"' \
            -include sys/types.h -I. -I"$here/migshim" -o migcom \
            $(ls *.c | grep -v '^handler\.c$'))
    mkdir -p "$PREFIX/libexec"
    install -m 0755 "$m/migcom" "$PREFIX/libexec/migcom"
    # Apple's mig driver script, pointed at clang instead of xcrun/arch.
    sed -e '1s|.*|#!/bin/bash|' \
        -e 's#^arch=`/usr/bin/arch`#arch=armv7#' \
        -e 's#MIGCC=$(realpath "${scriptRoot}/cc")#MIGCC=clang#' \
        -e 's#"$C" -E -arch ${arch}#"$C" -E -target ${arch}-apple-ios6.0 -x c#' \
        -e 's#/usr/bin/mktemp#mktemp#' \
        "$m/mig.sh" > "$PREFIX/bin/mig"
    chmod 0755 "$PREFIX/bin/mig"
fi

# --- Compiler wrappers -------------------------------------------------------
log "Writing compiler wrappers and xcrun"
mkdir -p "$OUT/asbin"
ln -sf "$PREFIX/bin/arm-apple-darwin11-as" "$OUT/asbin/as"
ln -sf "$PREFIX/bin/arm-apple-darwin11-ld" "$OUT/asbin/ld"

# Diagnostics that were warnings for the 2013 compiler and are errors today.
LEGACY="-Wno-error=implicit-function-declaration -Wno-error=implicit-int \
-Wno-error=int-conversion -Wno-error=incompatible-function-pointer-types \
-Wno-error=incompatible-pointer-types -Wno-error=return-type"

for cc in clang clang++; do
    cat > "$WRAP/armv7-$cc" <<EOF
#!/bin/bash
# Cross $cc for the 32-bit ARM Darwin kernel: Mach-O output, Apple ld64 links.
# - Hand-written .s files use pre-UAL Apple syntax: route them through the
#   cctools assembler driver (-no-integrated-as).
# - -disable-atexit-based-global-dtor-lowering keeps static destructors in
#   __mod_term_func, as Apple's compiler did; the kernel has no __cxa_atexit.
extra=()
for a in "\$@"; do case "\$a" in *.s) extra=(-no-integrated-as);; esac; done
exec $cc -target armv7-apple-ios6.0 -B$OUT/asbin -fuse-ld=$PREFIX/bin/arm-apple-darwin11-ld \\
    $LEGACY -Wno-everything -mllvm -disable-atexit-based-global-dtor-lowering \\
    "\$@" -Wno-error -Wno-error=cast-align "\${extra[@]}"
EOF
done
cat > "$WRAP/host-clang" <<EOF
#!/bin/bash
# Host compiler for xnu's K&R-era build helpers.
exec clang $LEGACY -Wno-everything -D_GNU_SOURCE "\$@"
EOF

mkdir -p "$OUT/hostsdk" "$OUT/sdk"
cat > "$WRAP/xcrun" <<EOF
#!/bin/bash
# Linux stand-in for Apple's xcrun, as used by xnu's makedefs on Linux hosts.
export LD_LIBRARY_PATH=$OUT/dispatch/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}
host=0; find=""; args=()
while [ \$# -gt 0 ]; do
  case "\$1" in
    -sdk) [ "\$2" = "$OUT/hostsdk" ] && host=1; shift 2;;
    -find) find="\$2"; shift 2;;
    --show-sdk-path) echo "$OUT/sdk"; exit 0;;
    -verbose|-log) shift;;
    *) args+=("\$1"); shift;;
  esac
done
if [ -n "\$find" ]; then
  case "\$find" in
    clang|cc) [ \$host = 1 ] && echo $WRAP/host-clang || echo $WRAP/armv7-clang;;
    clang++) [ \$host = 1 ] && echo /usr/bin/clang++ || echo $WRAP/armv7-clang++;;
    mig) echo $PREFIX/bin/mig;;
    unifdef|flex|bison) command -v "\$find";;
    ld|as|strip|lipo|libtool|nm|nmedit|otool|ranlib|ar|ctf_insert) echo $PREFIX/bin/arm-apple-darwin11-\$find;;
    *) [ -x "$WRAP/\$find" ] && echo "$WRAP/\$find" || { echo "xcrun: cannot find \$find" >&2; exit 1; };;
  esac
  exit 0
fi
exec "\${args[@]}"
EOF
chmod 0755 "$WRAP"/*

# --- Mach-O host tools from the xnu tree ------------------------------------
XNU_SETUP="${XNU_SETUP:-}"
if [[ -n "$XNU_SETUP" ]]; then
    log "Building setsegname and kextsymboltool"
    flags=(-DHAVE_BCMP -DHAVE_BZERO -DHAVE_BCOPY -DHAVE_INDEX -DHAVE_RINDEX
           -I"$CCTOOLS/include" -I"$CCTOOLS/include/foreign")
    "$WRAP/host-clang" "${flags[@]}" -o "$WRAP/setsegname" "$XNU_SETUP/setsegname/setsegname.c"
    "$WRAP/host-clang" "${flags[@]}" -o "$WRAP/kextsymboltool" \
        "$XNU_SETUP/kextsymboltool/kextsymboltool.c" "$here/machstub.c" \
        "$CCTOOLS/libstuff/.libs/libstuff.a" "$CCTOOLS/libmacho/.libs/libmacho.a" \
        -luuid -lstdc++ -L"$OUT/dispatch/lib" -ldispatch -lBlocksRuntime \
        -Wl,-rpath,"$OUT/dispatch/lib"
fi

log "Toolchain ready in $OUT"
