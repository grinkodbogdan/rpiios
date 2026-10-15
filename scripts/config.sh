# Shared settings for all rpiios scripts. Sourced, not executed.
# Override any of these by exporting them before running a script.

# Upstream emulator: devos50/qemu-ios, iPod touch 2G (n72ap) branch.
# Pinned to the commit we tested so a later upstream push can't break the build.
QEMU_IOS_REPO="${QEMU_IOS_REPO:-https://github.com/devos50/qemu-ios.git}"
QEMU_IOS_BRANCH="${QEMU_IOS_BRANCH:-ipod_touch_2g}"
QEMU_IOS_COMMIT="${QEMU_IOS_COMMIT:-328815631bd1f5ed388c2dc3393ab030fa8725ac}"

# Where everything lives on the Pi.
RPIIOS_HOME="${RPIIOS_HOME:-/opt/rpiios}"
QEMU_SRC="$RPIIOS_HOME/qemu-ios"
QEMU_BIN="$QEMU_SRC/build/qemu-system-arm"
FW_DIR="$RPIIOS_HOME/firmware"

# Firmware published by the emulator's author alongside the n72ap_v1 release.
FW_RELEASE_URL="${FW_RELEASE_URL:-https://github.com/devos50/qemu-ios/releases/download/n72ap_v1}"
BOOTROM_FILE="bootrom_240_4"
NOR_FILE="nor_n72ap.bin"
NAND_ZIP="nand_n72ap.zip"
BOOTROM_SHA256="55f4d8ea2791ba51dd89934168f38f0fb21ce8762ff614c1e742407c0d3ca054"
NOR_SHA256="c2284d53fd45ef430415921cf7017aef03e2352dcd2490cd52799204c252868f"
NAND_ZIP_SHA256="2dd19b16f8a90e0a333f7ea28311ab4cd64b4f2a122f451ed69ef04e8d441412"

# The emulator only reads the NAND pages (writes stay in guest RAM), so every
# boot starts from the same factory state.
NAND_DIR="$FW_DIR/nand"

die() { echo "error: $*" >&2; exit 1; }
log() { echo "==> $*"; }
