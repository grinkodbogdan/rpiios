"""Symbolize runtime addresses in the iOS 7 armv7 dyld shared cache.

Usage: dscsym.py CACHE RUNTIME_MAPPING0_VA ADDR [ADDR...]
The runtime VA of mapping 0 comes from the kernel's "IOS7LAB cache mapping 0
va=..." line; the slide is that minus the cache's own mapping 0 address.
"""
import struct, sys, bisect

path, va0 = sys.argv[1], int(sys.argv[2], 16)
f = open(path, 'rb')
hdr = f.read(0x60)
mapping_off, mapping_count, images_off, images_count = struct.unpack_from('<IIII', hdr, 16)
f.seek(mapping_off)
mappings = [struct.unpack('<QQQII', f.read(32)) for _ in range(mapping_count)]
slide = va0 - mappings[0][0]

def file_off(addr):
    for a, size, off, _, _ in mappings:
        if a <= addr < a + size:
            return off + (addr - a)
    return None

f.seek(images_off)
images = []
for _ in range(images_count):
    addr, mtime, inode, path_off, pad = struct.unpack('<QQQII', f.read(32))
    images.append((addr, path_off))
images.sort()

def cstr(off):
    f.seek(off)
    b = f.read(256)
    return b.split(b'\0')[0].decode(errors='replace')

cache = {}
def symbols(img_addr):
    if img_addr in cache:
        return cache[img_addr]
    off = file_off(img_addr)
    f.seek(off)
    magic, cpu, sub, ftype, ncmds, sizecmds, flags = struct.unpack('<7I', f.read(28))
    pos = off + 28
    syms = []
    text = (img_addr, img_addr)
    for _ in range(ncmds):
        f.seek(pos)
        cmd, size = struct.unpack('<II', f.read(8))
        if cmd == 1:  # LC_SEGMENT
            f.seek(pos + 8)
            name = f.read(16).split(b'\0')[0]
            vmaddr, vmsize = struct.unpack('<II', f.read(8))
            if name == b'__TEXT':
                text = (vmaddr, vmaddr + vmsize)
        if cmd == 2:  # LC_SYMTAB
            f.seek(pos + 8)
            symoff, nsyms, stroff, strsize = struct.unpack('<IIII', f.read(16))
            f.seek(symoff)
            raw = f.read(nsyms * 12)
            for k in range(nsyms):
                strx, ntype, nsect, ndesc, value = struct.unpack_from('<IBBhI', raw, k * 12)
                if ntype & 0xe0 or not (ntype & 0x0e) == 0x0e or not value:
                    continue
                syms.append((value & ~1, strx))
            syms.sort()
            syms = [(v, stroff + s) for v, s in syms]
        pos += size
    cache[img_addr] = (syms, text)
    return cache[img_addr]

for a in sys.argv[3:]:
    rt = int(a, 16)
    addr = (rt & ~1) - slide
    k = bisect.bisect_right([i[0] for i in images], addr) - 1
    if k < 0:
        print(a, 'not in cache'); continue
    img_addr, path_off = images[k]
    syms, text = symbols(img_addr)
    name = cstr(path_off).split('/')[-1]
    if not (text[0] <= addr < text[1]):
        print(a, 'outside __TEXT of', name); continue
    j = bisect.bisect_right([s[0] for s in syms], addr) - 1
    if j < 0:
        print(a, '=', name, '+', hex(addr - img_addr)); continue
    print(a, '=', name, cstr(syms[j][1]), '+', hex(addr - syms[j][0]))
