#!/usr/bin/env python3
"""Apply rpiios's settings to the iOS 7 root filesystem image, in place.

Usage: rootfs-tweaks.py ROOTFS.HFS

Works on the rootfs.hfs from ios7.rar or on IOS7/ROOTFS.HFS on an
already-written card (mount its FAT partition first). Safe to run again.

- backboardd: BKNoWatchdogs. backboardd kills an app that takes longer than
  about 20 s to launch ("failed to launch in time"); without a GPU, and
  while the system is busy, apps can take longer than that on the Pi.
- Settings (Preferences.app): always load iconCache@2x.artwork, the only
  icon cache it ships. At this port's screen scale (1.5) it asks for
  iconCache.artwork, which does not exist, and then CFReleases an output
  the failed call never set: Settings crashes or not depending on stack
  garbage. One instruction (a bpl) becomes a nop. Patched code needs the
  kernel's cs_enforcement_disable=1, which rpiios's kernel7.img passes.
"""
import os
import plistlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hfs_put  # noqa: E402

TWEAKS = [
    ('/private/var/mobile/Library/Preferences/com.apple.backboardd.plist', {'BKNoWatchdogs': True}),
]

# (path, file offset, original bytes, new bytes, what)
CODE_PATCHES = [
    ('/Applications/Preferences.app/Preferences', 0x171f8, b'\x05\xd5', b'\x00\xbf',
     'Settings: always use iconCache@2x.artwork'),
]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    image = sys.argv[1]
    for path, values in TWEAKS:
        prefs = plistlib.loads(hfs_put.read_file(image, path))
        if all(prefs.get(k) == v for k, v in values.items()):
            print('rootfs-tweaks: %s already set' % os.path.basename(path))
            continue
        prefs.update(values)
        hfs_put.write_file(image, path, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY))
        print('rootfs-tweaks: %s: %s' % (os.path.basename(path),
                                         ', '.join('%s=%s' % kv for kv in values.items())))
    for path, offset, old, new, what in CODE_PATCHES:
        data = bytearray(hfs_put.read_file(image, path))
        here = bytes(data[offset:offset + len(old)])
        if here == new:
            print('rootfs-tweaks: %s already applied' % what)
            continue
        if here != old:
            sys.exit('rootfs-tweaks: %s: unexpected bytes %s at %#x; not the iOS 7.1.2 file?'
                     % (path, here.hex(), offset))
        data[offset:offset + len(old)] = new
        hfs_put.write_file(image, path, bytes(data))
        print('rootfs-tweaks: %s' % what)


if __name__ == '__main__':
    main()
