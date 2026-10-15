rpiios: iOS 7.1.2 natively on a Raspberry Pi 3 Model B+
========================================================

EXPERIMENTAL. Boots on a real Pi 3B+ to the lock screen; slide to unlock,
the home screen and Settings work. Expect rough edges - see "If it does not
boot" below.

You need
- Raspberry Pi 3 Model B+, power supply, HDMI screen, USB mouse
- microSD card, 2 GB or larger (EVERYTHING ON IT WILL BE ERASED)
- the HD2 iOS 7 package, ios7.rar
- a Linux PC (any distro; a live USB stick works) with an SD card reader

Making the SD card (Linux)
1. Install the tools (Debian/Ubuntu/Mint/Raspberry Pi OS):
     sudo apt install fdisk dosfstools mtools unar python3
   Fedora: sudo dnf install util-linux dosfstools mtools unar python3
   Arch:   sudo pacman -S util-linux dosfstools mtools unarchiver python
2. Insert the card and find its name:
     lsblk -o NAME,SIZE,TRAN,MODEL
   It is the disk with your card's size, e.g. sdb (USB reader) or mmcblk0
   (built-in reader). Use the whole disk, not a partition like sdb1.
3. In this folder, run (with your card's name and the path to ios7.rar):
     sudo bash flash-sdcard.sh /dev/sdX ~/Downloads/ios7.rar
   It shows the card and asks you to type yes, then partitions and formats
   it, extracts rootfs.hfs from the archive, copies everything and reads it
   all back to check. It takes a few minutes. It refuses to touch the disk
   your PC runs from.
4. When it says Done, remove the card, put it in the Pi, connect HDMI and the
   mouse, then power on.

No card reader on the Linux PC? Make an image file instead (no sudo needed):
     bash flash-sdcard.sh rpiios.img ~/Downloads/ios7.rar
and write rpiios.img to the card from any computer with Raspberry Pi Imager
("Choose OS" > "Use custom") or balenaEtcher.

The script also accepts rootfs.hfs instead of ios7.rar if you already
extracted it.

Updating a card you already made (no need to rewrite it):
1. Put the card in the PC; its partition opens (or mount it), e.g. at
   /media/$USER/RPIIOS.
2. Copy boot/kernel7.img over kernel7.img on the card.
3. Apply the iOS settings to the root filesystem on the card:
     python3 rootfs-tweaks.py /media/$USER/RPIIOS/IOS7/ROOTFS.HFS
   (It is safe to run again. It turns off backboardd's launch watchdog,
   which kills apps that take more than ~20 s to open, and fixes a crash
   when Settings opens. Use the new kernel7.img with it.)
4. Eject the card properly before removing it.

What to expect
- The screen goes black; within a few seconds small numbers appear in the top
  left (boot stages), then white kernel text, then the iOS lock screen.
  On a wide monitor the picture is a tall strip in the middle.
- Move the mouse to move the pointer. Hold the left button to touch the screen
  at the pointer, release to lift the finger. Slide to unlock = hold the button
  near "slide to unlock" and drag to the right.
- Right mouse button = Home button (leave an app, go back to the home screen).
- The boot shim asks the firmware for the full CPU clock (1.4 GHz on a 3B+;
  the kernel prints "rpi3: ARM clock ... MHz"). iOS uses one of the 4 cores.
- iOS drawing is done by the CPU (no GPU driver), so it is slow, as on the HD2.
- Nothing you do is saved: like the HD2 build, iOS runs on a RAM copy of the
  root filesystem.

If it does not boot
- A full red screen with "XNU FATAL" and numbers: take a photo of it.
- Stuck on white text: take a photo of the last lines.
- Black screen and nothing ever appears: the firmware did not start the
  kernel, or HDMI is not detected. Try another HDMI input or cable, and run
  the script again in case the card was written badly.
Send the photo back, it shows exactly where it stopped.

Contents
  flash-sdcard.sh   makes the card
  rootfs-tweaks.py  iOS settings for the Pi (the flash script runs it)
  hfs_put.py        helper for rootfs-tweaks.py (edits a file in the image)
  boot/             the files the script puts on the card: Raspberry Pi
                    firmware (bootcode.bin, start.elf, fixup.dat,
                    LICENCE.broadcom), kernel7.img (boot shim + iOS 7 XNU
                    kernel for the Pi), config.txt, cmdline.txt
  Source: github.com/grinkodbogdan/rpiios
