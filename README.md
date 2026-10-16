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
| HD2 kernel from source | done |
| Pi 3B+ board target: boot shim, interrupts, timer, HDMI framebuffer | done, **runs on real hardware** |
| SD card: multi-block reads (~6 MB/s on a real Pi) | done, real hardware |
| Cortex-A53 fixes: write-back cacheable RAM, TLB maintenance after faults, no `swp`, HYP exit | done, real hardware |
| iOS 7.1.2 userland to SpringBoard / lock screen | done, **real hardware and QEMU** |
| USB mouse/tablet as touch (DWC2 host, hubs, split transactions, HID report descriptors) | done, real hardware and QEMU |
| Touches reach backboardd and SpringBoard | verified in QEMU |
| AppleKeyStore stand-in (no passcode) | done |
| iOS 7 lock handoff (`thread_switch` options 3-5; contended locks used to spin) | done |
| Render pacing: CPU rendering gets at most about half the CPU, so input and I/O keep up | done |
| Slide to unlock, home screen, page swipes | works, QEMU and real hardware |
| Apps: Settings opens and browses; right click = Home | works in QEMU (Settings also on the Pi) |
| Guarded fd syscalls (sqlite), general IOSurfaceCreate, launch watchdog off (`tools/rootfs-tweaks.py`) | done |
| ARM clock raised to the firmware maximum by the boot shim | done |
| More than one CPU core | not yet: the kernel port is uniprocessor |
| iOS 10.3.4 (32-bit, iPhone5,1) root: shared cache v2, zlib-compressed HFS files, iOS 10 system calls | done (QEMU) |
| iOS 10 libdispatch: kevent workqueue (event manager thread, `kevent_qos`, Mach port knotes), ulock | done (QEMU) |
| iOS 10 launchd: boot tasks, mounts (`tools/ios10-rootfs.py` fixes fstab), daemons via xpcproxy | in progress (QEMU) |

The kernel is built with `-O2` on the Pi (the HD2 kernel stays `-O0`).

## Ready-made card

`dist/rpiios-pi3b/` holds the current boot files (Raspberry Pi firmware,
`kernel7.img`) and `flash-sdcard.sh`, which writes a card (or an image) from
the HD2 release's `ios7.rar`. See its README.txt.

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

This fetches the pinned xnu-hd2 commit, applies `patches/xnu/`, builds the
toolchain into `out/toolchain` and the kernel into `out/build/`.
To build an SD card by hand instead, see `docs/SDCARD.txt`.

`tools/flash-sdcard.sh` is the end-user version: given prebuilt boot files in
`boot/` next to it, it writes a real card (`sudo tools/flash-sdcard.sh
/dev/sdX ios7.rar`) or a `.img`, extracting `rootfs.hfs` from the HD2
archive itself and reading everything back to verify.

Test in QEMU (Raspberry Pi 2 machine: same peripherals as the 3B+ in 32-bit
mode):

```sh
qemu-system-arm -M raspi2b -bios out/kernel7.img \
    -drive file=out/sdcard.img,if=sd,format=raw -serial stdio \
    -usb -device usb-mouse
```

Add `rpi3_uart=1` to the boot-args (`--cmdline` or `cmdline.txt`) for kernel
output on the serial port.

Driving it like a finger: start QEMU with `-device usb-tablet -qmp
unix:out/qmp.sock,server,nowait` from the repo root, then for example
`tools/qemu/qtouch.py out/qmp.sock "drag 60 720 440 720 8" "shot after"`
(`tap`, `drag`, `home` = right click, `shot`). `tools/dscsym.py` turns
userland PCs (printed by the kernel's boot watchdog) into shared-cache symbols.

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
