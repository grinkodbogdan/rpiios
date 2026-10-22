#!/usr/bin/env python3
"""The Mi A1 boot shim's ramdisk: a MIA1BOOT container of named files (main.c reads it).

usage: mkcontainer.py OUT NAME=FILE...

  char magic[8] "MIA1BOOT"; u32 version 1; u32 count;
  4 entries of { char name[16]; u64 offset; u64 size; }   (offsets from the container's start)
  then the files, each at a 4096-byte boundary.
"""
import struct
import sys

if len(sys.argv) < 3:
    sys.exit(__doc__)
files = []
for arg in sys.argv[2:]:
    name, path = arg.split('=', 1)
    assert len(name) < 16
    files.append((name, open(path, 'rb').read()))
assert len(files) <= 4
header_size = 16 + 4 * 32
offset = 4096
entries = b''
body = b''
for name, data in files:
    entries += struct.pack('<16sQQ', name.encode(), offset, len(data))
    pad = (-len(data)) % 4096
    body += data + b'\0' * pad
    offset += len(data) + pad
entries += b'\0' * (4 * 32 - len(entries))
out = b'MIA1BOOT' + struct.pack('<II', 1, len(files)) + entries
out += b'\0' * (4096 - len(out)) + body
open(sys.argv[1], 'wb').write(out)
