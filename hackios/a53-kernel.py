#!/usr/bin/env python3
"""Make the HackiOS kernel (oosexgang/hackios-xnu-kernel) run on ARMv8.0 cores (Cortex-A53).

Usage: a53-kernel.py IN.mach OUT.mach

The prebuilt kernel uses PAN (Privileged Access Never, ARMv8.1): writes
around user copies and context switches, and reads that check it is set.
A Cortex-A53 has no PAN and takes an undefined-instruction exception on the
first one (arm_init). Without PAN there is nothing to switch, so writes
become NOPs and reads report PAN set (bit 22), which is what the kernel
expects between copies. Only __TEXT_EXEC is touched.

Its other ARMv8.1 instructions (LSE atomics) are all in the ath11k Wi-Fi
driver and its Linux layer, which an A53 device without a WCN6750 never runs.
"""
import struct
import sys

NOP = 0xd503201f
PATCHES = {
    0xd500409f: NOP,         # msr PAN, #0
    0xd500419f: NOP,         # msr PAN, #1
    0xd5384268: 0xd2a00808,  # mrs x8, PAN -> mov x8, #0x400000
}


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = bytearray(open(sys.argv[1], 'rb').read())
    counts = dict.fromkeys(PATCHES, 0)
    ncmds = struct.unpack_from('<I', d, 16)[0]
    off = 32
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', d, off)
        if cmd == 0x19 and d[off + 8:off + 24].split(b'\0')[0] == b'__TEXT_EXEC':
            fileoff, filesize = struct.unpack_from('<QQ', d, off + 40)
            for i in range(fileoff, fileoff + filesize, 4):
                w = struct.unpack_from('<I', d, i)[0]
                if w in PATCHES:
                    struct.pack_into('<I', d, i, PATCHES[w])
                    counts[w] += 1
        off += size
    open(sys.argv[2], 'wb').write(d)
    print('a53-kernel: ' + ', '.join('%08x x%d' % kv for kv in counts.items()))


if __name__ == '__main__':
    main()
