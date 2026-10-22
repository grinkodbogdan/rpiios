# HackiOS on Cortex-A53 (toward the Xiaomi Mi A1)

Work on top of OOSEX GANG's HackiOS (iOS 16.5 on Apple's open-source XNU):
[hackios-xnu-kernel](https://github.com/oosexgang/hackios-xnu-kernel) and
[hackios-xnu-userland](https://github.com/oosexgang/hackios-xnu-userland).
No Apple files here.

## Result

iOS 16.5 reaches the home screen in QEMU `virt` with `-cpu cortex-a53` (ARMv8.0, the
Snapdragon 625's core in the Mi A1) and `gic-version=2` (the Snapdragon 625's interrupt
controller), with the kernel built from source here and the loader changes below. iOS 16.5's
user space ran there with no illegal-instruction faults.

## Files

- `hackios-xnu-kernel.diff`: changes to their kernel repo (`git apply` in a fresh checkout):
  - `xnuloader/loader.c`:
    - on a core without PAN (ARMv8.0: Cortex-A53) the loader patches the kernel's PAN
      instructions out of `__TEXT_EXEC` in memory before starting it (writes -> NOP, reads ->
      "PAN set"); one kernel then runs on both kinds of core;
    - a core without the GICv3 system-register interface gets a GICv2 described in the
      device tree (`arm,gic-v2`; second range = the CPU interface, `gic_cpu` per platform),
      and the loader's own context save/restore skips the GICv3 registers there (and PAN/UAO
      on cores without them);
    - the ramfb completion check read a non-volatile copy of the DMA control word, so the
      loader kept the firmware's 800x600 mode while QEMU showed its new 1125x2436 buffer
      (black screen with Ubuntu's AAVMF firmware);
  - `xnu/files/osfmk/arm64/gic_v3_irq.c`: GICv2 (the Mi A1's Snapdragon 625 has one): banked
    distributor for SGIs/PPIs, memory-mapped CPU interface, `GICD_ITARGETSR` routing,
    `GICD_SGIR` signals; chosen at run time from `ID_AA64PFR0_EL1.GIC`;
  - `xnu/files/bsd/dev/redwood_tissot_touch.c`: the Mi A1's touch screen, written the way
    their POCO drivers are (no frameworks, steps said in the log, a polling thread):
    FocalTech FT5x06 at 0x38 on I2C driven by hand on GPIO10/11, reset GPIO64, interrupt
    line GPIO65 looked at every 8 ms (held low while touched: register 0xA4 = 0); reports
    1080x1920 scaled to 0..65535 through `redwood_touch_post`. Its supply (PM8953 LDO10)
    belongs to the RPM, so it is only read and reported. `tissot_touch_steps=N`,
    `tissot_touch_flip=1/2/3`.
  - `xnu/files/bsd/dev/redwood_tissot_buttons.c`: power and volume down from the PM8953
    power-on block (`RT_STS` bits 0 and 1) read through PMIC arbiter **version 2**
    (channel table at core+0x800, observer channels 0x8000 apart), read only as theirs;
    volume up from TLMM GPIO85 (active low, pulled up). Power is the side button.
    `tissot_buttons_steps=N`. Also exports `redwood_tissot_spmi_read()`.
  - Both are off unless boot-arg `tissot=1` (or `tissot_touch=1` / `tissot_buttons=1`);
    their `touchscreen=1`/`buttons=1` must not be given on the Mi A1 (other SoC's addresses).
  - `qemu/run.sh`: `QEMU_CPU` and `QEMU_GIC` select the CPU model and GIC version.
- `a53-kernel.py`: the same PAN patch applied to a kernel file (for their prebuilt kernel with
  an unmodified loader). Its LSE atomics are only in the ath11k Wi-Fi driver.
- `hackios-xnu-userland.diff`: `userland/build.sh` builds on hosts without `timedatectl`
  and with a prebuilt set that lacks the `drawable_test` program.

## Building the kernel here

Their `xnu/README.md` steps work on Ubuntu with: theos in `~/theos` with the Linux iOS toolchain
(`L1ghtmann/llvm-project` release `iOSToolchain-x86_64.tar.xz` in `toolchain/`) and
`iPhoneOS16.5.sdk` from `theos/sdks` in `sdks/`; and GNU awk as `awk` (with mawk XNU's
`makesyscalls.sh` writes a broken `init_sysent.c`).

## Reproduce

```sh
git clone https://github.com/oosexgang/hackios-xnu-kernel hackios && cd hackios
common/setup.sh && git apply ../hackios-xnu-kernel.diff
GIT_DIR=.userland.git GIT_WORK_TREE=. git apply ../hackios-xnu-userland.diff
ios165/fetch.sh && SKIP=kernel common/build.sh qemu-virt
K=xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837
mkdir -p $K && ../a53-kernel.py prebuilt/mach.development.bcm2837 $K/mach.development.bcm2837
QEMU_GIC=2 QEMU_CPU=cortex-a53 QEMU_EFI=/usr/share/AAVMF/AAVMF_CODE.fd QEMU_VARS=/usr/share/AAVMF/AAVMF_VARS.fd \
  QEMU_SMP=4 CMDLINE=cpus=4 qemu/run-ios.sh 1500
```
