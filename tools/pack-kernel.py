#!/usr/bin/env python3
"""Build kernel7.img: the Raspberry Pi boot shim followed by the XNU payload.

Layout:  [shim.bin padded to 64 KiB][payload header][Mach-O kernel][device tree]

The device tree is Apple's flattened format (what XNU's DTInit reads), with
the same nodes the HD2 package uses: /chosen carries the root-matching rule
for IOS7/ROOTFS.HFS and a 128-byte handoff the shim fills in at boot.

Usage: pack-kernel.py --shim shim.bin --kernel mach_kernel --out kernel7.img
                      [--cmdline "..."] [--describe]
"""
import argparse
import struct
import sys

PAYLOAD_OFFSET = 0x10000
MAGIC0, MAGIC1, VERSION = 0x33495052, 0x31554E58, 1
MAX_SEGMENTS = 16
HEADER_FMT = "<2I9I"           # magic[2], version..segment_count
SEGMENT_FMT = "<4I"
HEADER_BYTES = struct.calcsize(HEADER_FMT) + MAX_SEGMENTS * struct.calcsize(SEGMENT_FMT) + 256

DEFAULT_CMDLINE = "ios7lab_leo_first_entry=1 ios7lab_guard_open_error=1 cs_enforcement_disable=1"
ROOT_MATCHING = ("<dict><key>IOProviderClass</key><string>IOMedia</string>"
                 "<key>IOPropertyMatch</key><dict><key>Content</key><string>Apple_HFS</string>"
                 "<key>IOS7LeoRootFile</key><string>IOS7/ROOTFS.HFS</string></dict></dict>")


def parse_macho(data):
    """Return (entry_va, segments) for a 32-bit little-endian ARM Mach-O."""
    magic, cputype, _sub, filetype, ncmds, _size, _flags = struct.unpack_from("<7I", data, 0)
    if magic != 0xFEEDFACE or cputype != 12 or filetype != 2:
        sys.exit("not a 32-bit ARM MH_EXECUTE Mach-O")
    off, entry, segments = 28, None, []
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<2I", data, off)
        if cmd == 0x1:  # LC_SEGMENT
            name = data[off + 8:off + 24].split(b"\0")[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<4I", data, off + 24)
            segments.append((name, vmaddr, vmsize, fileoff, filesize))
        elif cmd == 0x5:  # LC_UNIXTHREAD
            flavor, count = struct.unpack_from("<2I", data, off + 8)
            regs = struct.unpack_from("<%dI" % count, data, off + 16)
            if flavor != 1 or count < 16:
                sys.exit("unexpected thread state flavor/count")
            entry = regs[15]  # pc
        off += cmdsize
    if entry is None:
        sys.exit("no LC_UNIXTHREAD entry point")
    for name, vmaddr, vmsize, fileoff, filesize in segments:
        if filesize and fileoff + filesize > len(data):
            sys.exit("segment %s extends past the file" % name)
    return entry, segments


def dt_prop(name, value):
    if isinstance(value, str):
        value = value.encode() + b"\0"
    raw = name.encode()
    assert len(raw) < 32
    return raw.ljust(32, b"\0") + struct.pack("<I", len(value)) + value + b"\0" * (-len(value) % 4)


def dt_node(props, children=()):
    out = struct.pack("<2I", len(props), len(children))
    for name, value in props:
        out += dt_prop(name, value)
    for child in children:
        out += child
    return out


def build_device_tree():
    marker = b"RPI3HANDOFF-SLOT" * 8      # 128 bytes, located after building
    cpu = dt_node([("name", "cpu"), ("device_type", "cpu"),
                   ("compatible", "arm,cortex-a53"), ("reg", struct.pack("<I", 0))])
    cpus = dt_node([("name", "cpus"), ("#address-cells", struct.pack("<I", 1)),
                    ("#size-cells", struct.pack("<I", 0))], [cpu])
    chosen = dt_node([("name", "chosen"), ("firmware-version", "rpiios-shim"),
                      ("rpi3-handoff", marker), ("root-matching", ROOT_MATCHING),
                      ("ios7leo-fast-console", struct.pack("<I", 0))])
    root = dt_node([("name", "device-tree"), ("model", "Raspberry Pi 3 Model B+"),
                    ("compatible", b"brcm,bcm2837\0raspberrypi,3-model-b-plus\0")],
                   [chosen, cpus])
    at = root.find(marker)
    assert at > 0 and root.count(marker) == 1
    return root.replace(marker, b"\0" * 128), at


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shim", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--out")
    ap.add_argument("--cmdline", default=DEFAULT_CMDLINE)
    ap.add_argument("--describe", action="store_true", help="print the Mach-O layout and exit")
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    entry, segments = parse_macho(kernel)
    if a.describe:
        print("entry 0x%08x" % entry)
        for s in segments:
            print("  %-16s vm 0x%08x+0x%07x file 0x%07x+0x%07x" % s)
        return
    segments = [s for s in segments if s[2]]
    if len(segments) > MAX_SEGMENTS:
        sys.exit("too many segments")

    shim = open(a.shim, "rb").read()
    if len(shim) > PAYLOAD_OFFSET:
        sys.exit("shim is %d bytes; payload offset is %d" % (len(shim), PAYLOAD_OFFSET))
    cmdline = a.cmdline.encode()
    if len(cmdline) > 255:
        sys.exit("cmdline too long")

    dt, handoff_at = build_device_tree()
    kernel_offset = (HEADER_BYTES + 0xFFF) & ~0xFFF
    dt_offset = (kernel_offset + len(kernel) + 0xF) & ~0xF
    total = dt_offset + len(dt)

    header = struct.pack(HEADER_FMT, MAGIC0, MAGIC1, VERSION, total, entry,
                         kernel_offset, len(kernel), dt_offset, len(dt), handoff_at,
                         len(segments))
    for _name, vmaddr, vmsize, fileoff, filesize in segments:
        header += struct.pack(SEGMENT_FMT, vmaddr, vmsize, fileoff, filesize)
    header += struct.pack(SEGMENT_FMT, 0, 0, 0, 0) * (MAX_SEGMENTS - len(segments))
    header += cmdline.ljust(256, b"\0")
    assert len(header) == HEADER_BYTES

    payload = bytearray(total)
    payload[0:len(header)] = header
    payload[kernel_offset:kernel_offset + len(kernel)] = kernel
    payload[dt_offset:dt_offset + len(dt)] = dt
    image = shim.ljust(PAYLOAD_OFFSET, b"\0") + bytes(payload)
    open(a.out, "wb").write(image)
    print("%s: %d bytes, entry 0x%08x, %d segments, device tree %d bytes" %
          (a.out, len(image), entry, len(segments), len(dt)))


if __name__ == "__main__":
    main()
