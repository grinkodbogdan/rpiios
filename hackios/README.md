# HackiOS on Cortex-A53 (toward the Xiaomi Mi A1)

Work on top of OOSEX GANG's HackiOS (iOS 16.5 on Apple's open-source XNU):
[hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel) and
[hackios-xnu-userland](https://github.com/oosexgang/hackios-xnu-userland).
No Apple files here.

## Result

iOS 16.5 reaches the home screen in QEMU `virt` with `-cpu cortex-a53` (ARMv8.0, the
Snapdragon 625's core in the Mi A1), using HackiOS's prebuilt kernel with PAN patched out.
iOS 16.5's user space ran there with no illegal-instruction faults.

## Files

- `a53-kernel.py`: patches HackiOS's prebuilt kernel for ARMv8.0 (PAN writes -> NOP, PAN
  reads -> "set"). Its LSE atomics are only in the ath11k Wi-Fi driver.
- `hackios-xnu-kernel.diff`: local changes to their kernel repo:
  - `xnuloader/loader.c`: the ramfb completion check read a non-volatile copy of the DMA
    control word, so the loader kept the firmware's 800x600 mode while QEMU showed its new
    1125x2436 buffer (black screen with Ubuntu's AAVMF firmware);
  - `qemu/run.sh`: `QEMU_CPU` selects the CPU model (default cortex-a76).
- `hackios-xnu-userland.diff`: `userland/build.sh` builds on hosts without `timedatectl`
  and with a prebuilt set that lacks the `drawable_test` program.

## Reproduce

```sh
git clone https://github.com/oosexgang/hackios-xnu-kernel hackios && cd hackios
common/setup.sh && git apply ../hackios-xnu-kernel.diff
GIT_DIR=.userland.git GIT_WORK_TREE=. git apply ../hackios-xnu-userland.diff
ios165/fetch.sh && SKIP=kernel common/build.sh qemu-virt
K=xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837
mkdir -p $K && ../a53-kernel.py prebuilt/mach.development.bcm2837 $K/mach.development.bcm2837
QEMU_CPU=cortex-a53 QEMU_EFI=/usr/share/AAVMF/AAVMF_CODE.fd QEMU_VARS=/usr/share/AAVMF/AAVMF_VARS.fd \
  QEMU_SMP=4 CMDLINE=cpus=4 qemu/run-ios.sh 1500
```
