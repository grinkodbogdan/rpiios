#!/usr/bin/env python3
"""Record iOS 10's first-boot state on a card image, in place.

Usage: ios10-prefs.py CARD.IMG

- lockdownd's data_ark: the device is activated. Activation needs Apple's
  servers and a baseband ticket; without it SpringBoard holds the
  activation flow instead of the lock screen.
- backboardd: BKNoWatchdogs. backboardd kills a system app that takes
  longer than about 20 s to check in, far shorter than this port takes.

Both files exist on a booted card; hfs_put rewrites them within the
space they already own.
"""
import os
import plistlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hfs_put  # noqa: E402

PREFS = [
    ('/private/var/root/Library/Lockdown/data_ark.plist',
     {'-ActivationState': 'Activated', '-BrickState': False}),
    ('/private/var/mobile/Library/Preferences/com.apple.backboardd.plist',
     {'BKNoWatchdogs': True}),
]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    image = sys.argv[1]
    for path, values in PREFS:
        data = hfs_put.read_file(image, path)
        prefs = plistlib.loads(data) if data else {}
        prefs.update(values)
        hfs_put.write_file(image, path, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY))
        print('ios10-prefs: %s: %s' % (path, ', '.join(values)))


if __name__ == '__main__':
    main()
