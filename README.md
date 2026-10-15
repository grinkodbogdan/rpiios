# rpiios — native iOS 7 on Raspberry Pi 3B+

Goal: boot the iOS 7.1.2 userland from the October 2026 HTC HD2 port
(j0sh1x / [Garysss123/xnu-hd2](https://github.com/Garysss123/xnu-hd2)) natively
on a Raspberry Pi 3 Model B+, with HDMI output and a USB mouse as the
touchscreen. Not emulation: Apple's XNU kernel runs directly on the Pi's
Cortex-A53 cores in 32-bit mode.

## How the HD2 port works (and why the Pi can reuse it)

- The HD2 boots a custom XNU (Darwin 13, iOS 7's kernel) built from the
  darwin-on-arm tree, with HD2 drivers for interrupts/timer, display, SD card
  and touch.
- iOS 7.1.2 (11D257, iPhone 4) userland lives in `IOS7/ROOTFS.HFS`, an HFS+
  image *file* on the SD card's FAT partition. Nothing in it is HD2-specific.
- There is no GPU driver: the kernel provides CPU-backed IOSurface and
  framebuffer services and iOS renders in software.

So the Pi port replaces only the board layer (boot shim, interrupt controller,
timer, display, SD, input) and reuses the root filesystem unchanged.

## Status

| Step | State |
|---|---|
| Darwin ARM toolchain on Linux (ld64, cctools, MIG) | done |
| Build the HD2 kernel from source; hand-written assembly matches the published HD2 kernel instruction for instruction | done |
| Raspberry Pi 3B+ board target (boot shim, IRQ, timer, framebuffer) | done, tested in QEMU (raspi2b) |
| SD card driver: root filesystem mounts, `launchd` starts | done, tested in QEMU |
| USB mouse → touch | not started (no input yet) |
| SD card image builder | done |

Nothing here has run on real Pi hardware yet.

## Building the kernel

On Ubuntu 24.04 (x86_64), as root for the package install step:

```sh
./build-kernel.sh QSD8250_LEO DEBUG   # HD2 kernel, toolchain check
./build-kernel.sh RASPBERRYPI3 DEBUG  # Pi kernel
shim/build.sh out/shim
tools/pack-kernel.py --shim out/shim/shim.bin \
    --kernel out/build/DEBUG_ARM_RASPBERRYPI3/mach_kernel --out out/kernel7.img
tools/make-sdcard.sh --rootfs rootfs.hfs --kernel out/kernel7.img \
    --out out/sdcard.img [--firmware path/to/raspberrypi-firmware/boot]
```

Test in QEMU (Raspberry Pi 2 machine: same peripherals as the 3B+ in 32-bit
mode):

```sh
qemu-system-arm -M raspi2b -bios out/kernel7.img \
    -drive file=out/sdcard.img,if=sd,format=raw -serial stdio
```

Add `rpi3_uart=1` to the boot-args (`--cmdline` or `cmdline.txt`) for kernel
output on the serial port.

This fetches the pinned xnu-hd2 commit, applies `patches/xnu/`, builds the
toolchain into `out/toolchain` and the kernel into `out/build/`.

## What is not in this repo

Apple's iOS root filesystem is not redistributed here. You need the
`rootfs.hfs` from the HD2 release package.

## Layout

- `toolchain/` builds cctools-port, libdispatch and MIG, and writes compiler
  wrappers and an `xcrun` stand-in for xnu's makefiles.
- `patches/xnu/` holds changes on top of xnu-hd2. The first patch makes the
  published tree build with a modern Linux toolchain. Its commit message lists
  every fix, including stand-ins for a few HD2-only files that were missing
  from the public tree.
- `tools/ual-convert.py` respells pre-UAL ARM assembly for modern LLVM.
