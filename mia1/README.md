# HackiOS (iOS 16.5) on the Xiaomi Mi A1

**It boots to the home screen** (2026-10-22, `first-home-screen.jpg`): one core at first, now all eight, the iOS volume
read from the eMMC's userdata partition, the display as LK left it switched to 32 bits a pixel.

The Mi A1 (tissot, Snapdragon 625 / MSM8953, 4 GB) boots HackiOS's XNU and iOS 16.5 userland
from its stock bootloader, with `fastboot boot`: nothing of the phone's firmware is replaced.

```
aboot (LK) --fastboot boot--> shim (mia1/shim) --its own UEFI--> HackiOS loader --> XNU
                                                                  iOS root: eMMC userdata
```

- `shim/`: started by aboot as an arm64 "Linux kernel". Turns the MMU on, switches the display
  (the splash memory LK leaves scanned out, 1080x1920) from 24 to 32 bits a pixel, prints on it,
  builds a memory map from the RAM banks aboot reports, and runs HackiOS's `loader.efi` under a
  minimal UEFI of its own (`efi.c`). Its ramdisk is a MIA1BOOT container: the loader and its
  payload (kernel, `root.apfs`, boot-args).
- The fast image: HackiOS's framebuffer tells iOS it has an iPhone X screen (1125 x 2436) and
  scales every frame onto the panel with the processor. `fbscale=2` makes iOS draw the same
  screen at two pixels a point (750 x 1624, 2.25 times fewer); the shim (bootargs `fbw=750
  fbh=1624`) hands the loader a framebuffer of exactly that size and has the MDP's VIG0 pipe, the
  one with a scaler, stretch it to the panel. The processor draws less and scales nothing.
  (`qemu-750x1624.png`: the same chain under QEMU, `mia1/qemu-shim.sh` with `SHIM_ARGS`.)
- `emmc/emmc_core.h`: the eMMC (SDHCI) code; the kernel's `redwood_tissot_emmc.c` carries a copy.
- The kernel side is in `../hackios/hackios-xnu-kernel.diff` (eMMC, touch, buttons, GICv2, the
  `tissot` platform of the loader, and `redwood_tissot_rpm.c`: requests to the RPM over SMD, the
  way the Pixel 1 port ios-sailfish asks its RPM for supplies; the touch screen asks it for LDO10
  (2.85 V) and LDO5/LDO6 (1.8 V) first. `tissot_rpm=0` skips that).

## Running it

1. The iOS system, once, on the userdata partition (this erases userdata):

   ```
   fastboot flash userdata system.apfs
   ```

   `system.apfs` is HackiOS's `userland/system.apfs` (2.4 GB), built from Apple's own iOS 16.5
   files: `ios165/fetch.sh` downloads them from Apple, `common/build.sh qemu-virt` builds it
   (see `../hackios/README.md`, "Reproduce"). It is not in this repository.

2. Every boot:

   ```
   fastboot boot mia1-ios-fast.img     # eight cores; iOS draws at 750x1624, the display scales it
   fastboot boot mia1-ios.img          # all eight cores (both clusters, MPIDR 0x0-0x3, 0x100-0x103)
   fastboot boot mia1-ios-1cpu.img     # one core
   fastboot boot mia1-ios-4cpu.img     # the first cluster's four
   fastboot boot mia1-ios-gpu.img      # one core; the Adreno 506 powered up and its registers read
   ```

   Every image measures the processor's clock at boot (`tissot-gpu: this processor ... MHz`);
   the GPU image adds its power-up stages (`redwood_tissot_gpu.c`, after ios-sailfish's first
   GPU step). Both show in yellow on the next boot.

   (`prebuilt/`; `shim/build.sh OUT HACKIOS_TREE` builds them.) The screen shows the shim's and
   the loader's text, then the kernel's (`-v`). If it stops, it stays on the screen: a
   photograph of it is the log. Hold power for 10 s to restart.

`prebuilt/mia1-hello.img` is the first test: colour bands, nothing else.

## Testing without the phone

- `qemu-emmc.sh DISK.IMG`: the kernel's eMMC driver and the root on it (an SD card on QEMU's
  sdhci-pci); boots to the home screen on a Cortex-A53, GICv2, 4 GB (`emmc-root-qemu.png`).
- `qemu-shim.sh HACKIOS_TREE DISK.IMG`: the whole chain -- the shim started as aboot starts it,
  its UEFI, the loader, the kernel, the root from the card -- to SpringBoard; text on the serial
  line (QEMU has no MDP).
