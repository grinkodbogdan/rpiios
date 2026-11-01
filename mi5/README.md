# iOS 16.7 on the Xiaomi Mi 5 (gemini)

The Mi 5 is the Pixel 1's SoC (MSM8996) on another board, so this is
[ios-sailfish](https://github.com/joranv1/ios-sailfish) (joranv1's iOS 16.7 port to the Pixel 1)
moved to the Mi 5. Everything here builds on **Linux or WSL2 on Windows: no Mac is needed**.

Nothing is flashed to `boot`, `recovery` or any firmware partition: the phone runs this through
`fastboot boot`, once per boot. Only **`userdata` is overwritten** (the iOS root lives there), so
everything on the phone is lost. Back it up first. To go back to Android, format data from
recovery (or `fastboot erase userdata`, then boot Android).

## What is different from the Pixel

| | Pixel 1 (sailfish) | Mi 5 (this) |
|---|---|---|
| Bootloader | Google's ABL | Qualcomm LK: Image.gz with DTBs appended, kernel at 0x80080000 |
| Display | VIG0, command-mode panel | the shim reads **whatever LK left running** off the MDP: pipe, buffer, stride, depth, command or video mode (`shim/mi5.c`) and passes it as `sweetfb_*` boot-args |
| Backlight | DCS brightness | the WLED, left on by LK (`sweetbl=0`) |
| Memory | 4 GB | upper bank size taken from LK's `/memory` (3 GB and 4 GB models) |
| Touch | Synaptics | **both Mi 5 controllers, found at boot**: Synaptics RMI4 at I²C 0x20, else Atmel mXT641T at 0x4a (BLSP2 QUP6, reset GPIO 89, IRQ 125, AVDD GPIO 73, L6 1.8 V) |
| Keys | power, volume | power, volume up (PM8994 GPIO 2), volume down (RESIN), **home** (TLMM 34) |
| RAM console | Pixel's ramoops | 0x92920000 (the Mi 5's ramoops console zone) |

Storage (UFS), GPU (Adreno 530) and the rest are the same SoC blocks and use sailfish's drivers
unchanged.

## What you need

- A Mi 5 with an unlocked bootloader. The 3 GB / 32 GB and 4 GB models both work.
- A PC with Linux, or Windows 10/11 with WSL2 (Ubuntu 24.04). About **50 GB free** inside WSL's
  own disk (`~`), **not** under `/mnt/c`: the files' owners must be kept.
- The phone's own Adreno firmware: `a530_pm4.fw`, `a530_pfp.fw`, `a530_zap.mdt`, `a530_zap.b02`.
  With LineageOS on the phone: `adb pull /vendor/firmware/a530_pm4.fw` (and the same for the
  others; on some builds they are in `/system/vendor/firmware` or `/firmware/image`). They are
  Qualcomm's and Xiaomi's files and are not in this repository.
- For Wi-Fi, also the phone's QCA6174 files: `otp30.bin`, `qwlan30.bin`, `bdwlan30.bin` and any
  `bdwlan30.b??` next to them. They are not in LineageOS's vendor tree but on the phone's
  firmware partitions; find them with a root shell:
  `adb shell su -c "find / -name 'qwlan30*' -o -name 'otp30*' -o -name 'bdwlan30*' 2>/dev/null"`.
  Put them in the same folder as the Adreno files. Without them, everything else still works,
  only without Wi-Fi.
- [Theos](https://theos.dev/docs/installation-linux) with its Linux toolchain, and the iOS 16.5
  SDK from [theos/sdks](https://github.com/theos/sdks) in `~/theos/sdks`. HackiOS's toolchain
  builds on top of it.

Packages (Ubuntu/WSL):

```sh
sudo apt install git curl build-essential clang lld llvm flex bison m4 perl python3 autoconf \
    cmake rsync 7zip lzfse device-tree-compiler hfsprogs libguestfs-tools linux-image-generic
```

`7zip` must be 7-Zip **23 or newer** (Ubuntu 24.04's `7zip` package; the old `p7zip-full` cannot
read APFS). On WSL, `libguestfs-tools` is what writes the HFS+ volume, because WSL's kernel has
no hfsplus. It runs a small VM and needs `linux-image-generic` for that VM's kernel. On native
Linux with hfsplus, the volume is mounted directly instead.

## Build

```sh
git clone https://github.com/grinkodbogdan/rpiios && cd rpiios
git clone https://github.com/oosexgang/hackios-xnu-kernel ~/hackios
(cd ~/hackios && xnu-deps/setup.sh --stage1)        # Darwin toolchain: cctools, ld64, mig, SDK overlay
python3 -m venv ~/hackios/xnu-deps/build/mesa/venv && ~/hackios/xnu-deps/build/mesa/venv/bin/pip install meson mako

mi5/kernel/setup.sh ~/mi5 ~/hackios                 # XNU 8796.141.3 + sailfish + Linux + Mi 5 patches
mi5/kernel/build.sh ~/mi5 ~/hackios                 # -> ~/mi5/xnu/BUILD/obj/kernel.development.sweet
mi5/sweetgl/build.sh ~/mi5 ~/hackios                # -> ~/mi5/sweetgl/sweetgl.dylib (GPU drawing)
mi5/mkboot.sh ~/mi5 ~/mi5-firmware                  # -> ~/mi5/mi5-boot.img
sudo mi5/mkroot.sh ~/mi5 ~/hackios ~/mi5/ios-root.img   # -> the iOS root (downloads 7.4 GB from Apple)
```

Keep that order: `sweetgl/build.sh` before the other two. `mkboot.sh` turns GPU drawing on when
`sweetgl.dylib` exists, and `mkroot.sh` installs it into the root. A boot image with GPU drawing
on a root without the library leaves the screen on the boot log. `NOGL=1 mi5/mkboot.sh ...`
makes a CPU-drawing image, which works on any root.

`mkroot.sh` takes 30 to 90 minutes. It fetches the iPhone 8 iOS 16.7.16 (20H392) images straight
from Apple's CDN, extracts them with 7-Zip and HackiOS's LZBITMAP decoder, restores owners, modes
and links from 7-Zip's listing (`tools/fixmeta.py`), adds what sailfish adds, and writes the HFS+
volume.

## Flash and boot

From Windows, run Google's platform-tools `fastboot.exe` on the files in WSL
(`\\wsl$\Ubuntu\root\mi5\...`); WSL itself does not see USB without usbipd. Hold volume down +
power for fastboot mode, then:

```sh
fastboot flash userdata ios-root.img     # 16 GB, sent in sparse chunks: takes a while
fastboot boot mi5-boot.img
```

What you should see:

1. The shim's screen for a moment: `SHIM xiaomi mi 5`, the display pipe it found
   (`VIG0`, `RGB0` or `DMA0`), command or video mode, the RAM size.
2. The XNU verbose log (`-v`). It stays on screen until iOS shows its first frame
   (`sweetfb_noconsole=1`), and after that it only goes to the logs.
3. The first boot takes several minutes longer: iOS builds its caches and runs its data
   migration. Then the lock screen. The home key unlocks, as on an iPhone 8.

The kernel log goes to the RAM console (it survives a warm reset) and to a 4 MiB ring at the end
of `userdata`. To read it, boot TWRP for gemini with `fastboot boot twrp.img`, then run
sailfish's `tools/sweet-log-pull.sh OUT.txt`. When reporting a problem, send that file and a
photo of the screen.

## Status

First run on a Mi 5 (3 GB model, Synaptics touch), 1 November 2026:
- LK boots the image. The shim finds the display, and XNU's verbose log shows on the panel.
- UFS works: `userdata` is found on LUN 0.
- Touch: Synaptics RMI4 (F12, 1079x1919, interrupt on GPIO 125), with coordinates matching the
  panel.
- Keys: power, volume up and volume down.
- GPU: SMMU, microcode and the RBCCU/SP power-up all succeed. Charger and fuel gauge work.
- The root did not mount because the phone's `userdata` still held Android's data (no
  `ios-root.img` flashed yet). The kernel now says so in plain words (`SWEET UFS: *** ...`).

Not yet seen on the phone: everything from the root mount on.

Done:
- kernel, shim, ADT, payload and boot image build on Linux from these scripts;
- `sweetgl.dylib` builds on Linux: Mesa 26.1.0 with sailfish's patch, linked with ld64 against
  Theos's iOS 16.5 SDK (sailfish uses Xcode's 17.5 SDK, targeting iOS 16.0 too), signed ad hoc
  with ldid;
- the shim's FDT memory-bank parser was tested on the host against the Mi 5's DTBs;
- the root builder's extract, metadata and HFS+ steps were tested end to end on the App cryptex
  (owners, modes and 194 hard links kept; `fsck.hfsplus` clean).

The first boot on the phone is what will show whether LK's display state, the touch rails and
the keys match the Mi 5 device trees they were taken from.

Not there yet:
- **Wi-Fi** needs the phone's own firmware files (see *What you need*). With them, `mkboot.sh`
  adds them and turns Wi-Fi on (`sailfishwlan=1`). The QCA6174 is the same chip on the same
  PCIe port as the Pixel's, and the kernel picks the board file from the board ID that the chip
  reports. Join a network from Settings, or add `ARGS_EXTRA="sailfishssid=NAME sailfishpsk=PASS"`.
- No modem, camera, audio, sensors or fingerprint, the same as on the Pixel.

## Files

| | |
|---|---|
| `kernel/setup.sh`, `kernel/build.sh` | the XNU tree (Apple's xnu-8796.141.3 + sailfish's patches) and its Linux build |
| `kernel/linux.patch` | what building that tree with stock clang and HackiOS's toolchain takes |
| `kernel/mi5.patch` | the Mi 5 board: framebuffer from boot-args (any pipe, video-mode panels), Atmel mXT touch next to Synaptics, the four keys, the RAM console address |
| `sailfish.patch` | sailfish's shim and ADT generator for the Mi 5 (`shim/mi5.c`, `shim/build-mi5.sh`, `mkadt.py --mi5`) |
| `dtb.sh` | the Mi 5's device trees from LineageOS's kernel, for LK |
| `mkboot.sh` | `mi5-boot.img`: shim + kernel + ADT + boot-args + GPU (and Wi-Fi) firmware |
| `sweetgl/build.sh`, `sweetgl/mesa-darwin.patch` | sailfish's `sweetgl.dylib` (Mesa freedreno for backboardd) built on Linux |
| `mkroot.sh`, `tools/fixmeta.py`, `tools/ipsw.py` | the iOS root, on Linux or WSL |

ios-sailfish has no license file, so none of its files are copied here: they are fetched at
commit `6f27e41f` and changed by `sailfish.patch`. Apple's images are fetched from Apple's own
CDN by the scripts, and the vendor firmware comes from the phone.
