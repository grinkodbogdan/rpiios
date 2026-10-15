#!/usr/bin/env python3
"""Apply rpiios's settings to the iOS 7 root filesystem image, in place.

Usage: rootfs-tweaks.py ROOTFS.HFS

Works on the rootfs.hfs from ios7.rar or on IOS7/ROOTFS.HFS on an
already-written card (mount its FAT partition first). Safe to run again.

- backboardd: BKNoWatchdogs. backboardd kills an app that takes longer than
  about 20 s to launch ("failed to launch in time"); without a GPU, and
  while the system is busy, apps can take longer than that on the Pi.
"""
import os
import plistlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hfs_put  # noqa: E402

TWEAKS = [
    ('/private/var/mobile/Library/Preferences/com.apple.backboardd.plist', {'BKNoWatchdogs': True}),
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


if __name__ == '__main__':
    main()
