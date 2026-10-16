#!/usr/bin/env python3
"""Prepare an iOS 10 (32-bit) root filesystem image for the Pi, in place.

Usage: ios10-rootfs.py ROOTFS.HFS

- /etc/fstab: iOS 10 mounts /private/var from a second partition
  (/dev/disk0s2). On the Pi everything lives in one HFS image, so that
  line is commented out and the root is mounted read-write (writes go
  to the kernel's in-memory copy-on-write overlay). Without this,
  launchd's mount boot task fails and launchd halts the system.

fstab is an HFS-compressed file (its zlib data lives in a
com.apple.decmpfs extended attribute), so the new text is recompressed
into the same space. Safe to run again.
"""
import mmap
import struct
import sys
import zlib

ORIGINAL = (b"/dev/disk0s1 / hfs ro 0 1\n"
            b"/dev/disk0s2 /private/var hfs rw,nosuid,nodev 0 2\n")
PATCHED = (b"/dev/disk0s1 / hfs rw 0 1\n"
           b"#dev/disk0s2 /private/var hfs rw,nosuid,nodev 0 2\n")
assert len(ORIGINAL) == len(PATCHED)


def find_fstab(m):
    """Offset of fstab's decmpfs header (type 3, zlib inline), and its contents."""
    pat = b'fpmc' + struct.pack('<IQ', 3, len(ORIGINAL))
    i = m.find(pat)
    while i != -1:
        body = m[i + 16:i + 16 + 256]
        try:
            text = zlib.decompressobj().decompress(body)
        except zlib.error:
            text = b''
        if text[:len(ORIGINAL)] in (ORIGINAL, PATCHED):
            # The attribute record's size field precedes the header.
            size = struct.unpack('>I', m[i - 4:i])[0]
            return i, size, text[:len(ORIGINAL)]
        i = m.find(pat, i + 1)
    sys.exit('ios10-rootfs: /etc/fstab not found; not the iOS 10.3.4 root image?')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1], 'r+b') as f:
        m = mmap.mmap(f.fileno(), 0)
        off, size, text = find_fstab(m)
        if text == PATCHED:
            print('ios10-rootfs: fstab already patched')
            return
        room = size - 16
        best = min((zlib.compress(PATCHED, level) for level in range(1, 10)), key=len)
        if len(best) > room:
            sys.exit('ios10-rootfs: patched fstab needs %d bytes, has %d' % (len(best), room))
        m[off + 16:off + 16 + room] = best + b'\0' * (room - len(best))
        m.flush()
        print('ios10-rootfs: fstab: root read-write, /private/var on the root volume')


if __name__ == '__main__':
    main()
