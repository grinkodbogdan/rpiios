#!/usr/bin/env python3
"""Grow an HFS+/HFSX volume image in place (no journal, unmounted).

Usage: hfs-grow.py IMAGE [--blocks N]

iOS root images are nearly full (the iOS 10.3.4 one has 25 MB free); on a
phone /private/var is a separate, large partition. On the Pi everything
lives in one image, so give it room. The default size keeps the image
under FAT32's 4 GiB file limit.

Only the cheap case is handled: the allocation file must already have
bits for the new block count (unused bits are zero, i.e. free, as
TN1150 requires), so nothing has to move. The old alternate volume
header block is freed, the new last block is marked used, and the
alternate header is written at the new end.
"""
import argparse
import os
import struct
import sys

DEFAULT_BLOCKS = 1040000           # 4096-byte blocks: 4.26 GB < 4 GiB-1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('image')
    ap.add_argument('--blocks', type=int, default=DEFAULT_BLOCKS)
    a = ap.parse_args()

    with open(a.image, 'r+b') as f:
        f.seek(1024)
        vh = bytearray(f.read(512))
        if vh[:2] not in (b'H+', b'HX'):
            sys.exit('hfs-grow: not an HFS+/HFSX volume')
        attrs, = struct.unpack_from('>I', vh, 4)
        if attrs & 0x2000:
            sys.exit('hfs-grow: volume is journaled; clear the journal first')
        bs, total, free = struct.unpack_from('>III', vh, 0x28)
        n = a.blocks
        if n <= total:
            print('hfs-grow: already %d blocks' % total)
            return
        if n * bs >= 1 << 32:
            sys.exit('hfs-grow: %d blocks would pass FAT32\'s 4 GiB limit' % n)
        alloc_size, = struct.unpack_from('>Q', vh, 0x70)
        if alloc_size * 8 < n:
            sys.exit('hfs-grow: allocation file only covers %d blocks' % (alloc_size * 8))
        extents = [struct.unpack_from('>II', vh, 0x70 + 16 + 8 * i) for i in range(8)]

        def bitmap_byte(block):
            """File offset of the allocation-file byte holding this block's bit."""
            off = block // 8
            for start, count in extents:
                if off < count * bs:
                    return start * bs + off
                off -= count * bs
            sys.exit('hfs-grow: bitmap extent missing')

        def set_bit(block, used):
            pos = bitmap_byte(block)
            f.seek(pos)
            b = f.read(1)[0]
            mask = 0x80 >> (block % 8)
            b = (b | mask) if used else (b & ~mask)
            f.seek(pos)
            f.write(bytes([b]))

        # new blocks must be marked free already
        for blk in range((total + 7) // 8 * 8, n, 4096):
            f.seek(bitmap_byte(blk))
            if any(f.read(min(512, (n - blk + 7) // 8))):
                sys.exit('hfs-grow: bitmap past the volume end is not clear')

        f.truncate(n * bs)
        set_bit(total - 1, False)      # old alternate header block
        set_bit(n - 1, True)           # new alternate header block
        struct.pack_into('>II', vh, 0x2c, n, free + (n - total))
        f.seek(1024)
        f.write(vh)
        f.seek(n * bs - 1024)
        f.write(vh)
    print('hfs-grow: %d -> %d blocks (%.0f MB free)' % (total, n, (free + n - total) * bs / 1e6))


if __name__ == '__main__':
    main()
