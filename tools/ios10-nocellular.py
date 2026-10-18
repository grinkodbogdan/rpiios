#!/usr/bin/env python3
"""Make iOS 10 behave as a device without cellular, in place.

Usage: ios10-nocellular.py CARD.IMG [--undo]

The iPhone 5 identity makes iOS expect a modem. The Pi has none and
CommCenter crashes (a null object it creates only for a live baseband)
before it answers anyone, but launchd keeps its Mach services registered,
so CoreTelephony's requests queue for an answer that never comes and
SpringBoard waits forever (_CTServerConnectionCopyRegistrationInfo).

This renames CommCenter's Mach services in launchd's job cache
(/System/Library/Caches/com.apple.xpcd/xpcd_cache.dylib, binary plists:
one byte changed per name, lengths kept). Lookups then fail at once, as on
an iPod, and CoreTelephony reports no service instead of waiting.

Programs that see an iPhone still call CoreTelephony and do not all cope
with that failure (locationd crashes, and SpringBoard waits on it through
CoreMotion). A device without cellular never calls: MobileGestalt says so.
MobileGestalt's answer cache (CacheExtra, keyed by the obfuscated key
names) takes precedence over what it would work out, so this also records
the answers a Wi-Fi-only device gives.
"""
import base64
import hashlib
import os
import plistlib
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hfs_put  # noqa: E402

CACHE = '/System/Library/Caches/com.apple.xpcd/xpcd_cache.dylib'
# The services CoreTelephony and its clients look up in CommCenter.
SERVICES = [b'com.apple.commcenter', b'com.apple.commcenter.xpc',
            b'com.apple.CommCenter.CallsProvider.xpc',
            b'com.apple.commcenter.cupolicy.xpc', b'com.apple.commcenter.atcs.xpc']


MG_CACHE = ('/private/var/containers/Shared/SystemGroup/'
            'systemgroup.com.apple.mobilegestaltcache/Library/Caches/'
            'com.apple.MobileGestalt.plist')
NO_CELLULAR = ['telephony', 'cellular-data', 'HasBaseband', 'CellularTelephonyCapability',
               'sms', 'data-plan', 'CarrierInstallCapability']


def mg_key(name):
    return base64.b64encode(hashlib.md5(b'MGCopyAnswer' + name.encode()).digest()).decode()[:22]


def set_mg_answers(image, undo):
    data = hfs_put.read_file(image, MG_CACHE)
    cache = plistlib.loads(data)
    extra = cache.setdefault('CacheExtra', {})
    for name in NO_CELLULAR:
        if undo:
            extra.pop(mg_key(name), None)
        else:
            extra[mg_key(name)] = False
    new = plistlib.dumps(cache, fmt=plistlib.FMT_BINARY)
    hfs_put.write_file(image, MG_CACHE, new)
    print('ios10-nocellular: MobileGestalt %s: %s'
          % ('answers removed' if undo else 'answers no', ', '.join(NO_CELLULAR)))


def renamed(name):
    return name[:-1] + b'_'


def bplist_string(name):
    # ASCII string object as a binary plist stores it (lengths 15..255)
    assert 15 <= len(name) < 256
    return b'\x5f\x10' + bytes([len(name)]) + name


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        sys.exit(__doc__)
    undo = '--undo' in sys.argv
    data = hfs_put.read_file(args[0], CACHE)
    new = data
    for name in SERVICES:
        old, rep = bplist_string(name), bplist_string(renamed(name))
        if undo:
            old, rep = rep, old
        n = new.count(old)
        if n == 0 and new.count(rep) == 0:
            sys.exit('ios10-nocellular: %s not in the job cache' % name.decode())
        new = new.replace(old, rep)
        print('ios10-nocellular: %s %s (%d)' % (name.decode(), 'restored' if undo else 'hidden', n))
    if new != data:
        hfs_put.write_file(args[0], CACHE, new)
    set_mg_answers(args[0], undo)


if __name__ == '__main__':
    main()
