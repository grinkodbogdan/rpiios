#!/usr/bin/env python3
"""Replace the contents of one existing file inside an HFS+/HFSX image, in place.

Usage: hfs_put.py IMAGE HFS_PATH SOURCE_FILE
       hfs_put.py IMAGE HFS_PATH --cat          (print the current contents)

The file must already exist and the new contents must fit in the blocks it
already owns (a small preferences plist always does: it owns at least one
4 KiB block). The data is written into those blocks and the file's logical
size is updated in its catalog record; nothing else on the volume changes.
IMAGE may be a bare volume (rootfs.hfs) or anything that holds exactly one
volume header at a 512-byte boundary (a disk image).
"""
import struct
import sys


def fail(msg):
    raise SystemExit('hfs_put: ' + msg)


class Volume:
    def __init__(self, f):
        self.f = f
        self.base = self.find_header()
        f.seek(self.base + 1024)
        vh = f.read(512)
        self.block = struct.unpack_from('>I', vh, 40)[0]
        # Catalog file fork data at offset 0x110 of the volume header.
        self.cat_extents = self.fork_extents(vh, 0x110)
        if struct.unpack_from('>I', vh, 0x110 + 12)[0] != sum(c for _, c in self.cat_extents):
            fail('the catalog file has overflow extents; not supported')
        self.cat = b''.join(self.read_blocks(s, c) for s, c in self.cat_extents)
        hdr = self.cat[14:14 + 106]
        (self.depth, self.root, self.leaf_records, self.first_leaf, self.last_leaf,
         self.node_size) = struct.unpack_from('>HIIIIH', hdr, 0)

    def find_header(self):
        f = self.f
        f.seek(1024)
        if f.read(2) in (b'H+', b'HX'):
            return 0
        # Search a disk image for the volume header (sector aligned).
        f.seek(0)
        pos, found, chunk = 0, [], 1 << 24
        while True:
            data = f.read(chunk + 4)
            if not data:
                break
            for sig in (b'H+\x00\x04', b'HX\x00\x05'):
                i = data.find(sig)
                while i != -1:
                    off = pos + i
                    if (off - 1024) % 512 == 0 and off >= 1024:
                        f2 = f.tell()
                        f.seek(off + 40)
                        bs = struct.unpack('>I', f.read(4))[0]
                        f.seek(f2)
                        if bs in (512, 1024, 2048, 4096, 8192, 16384, 32768, 65536):
                            found.append(off - 1024)
                    i = data.find(sig, i + 1)
            pos += chunk
            f.seek(pos)
        found = sorted(set(found))
        # The alternate header (2 sectors from the end) repeats the first one.
        if not found:
            fail('no HFS+ volume header found')
        return found[0]

    @staticmethod
    def fork_extents(buf, off):
        ext = []
        for k in range(8):
            s, c = struct.unpack_from('>II', buf, off + 16 + 8 * k)
            if c:
                ext.append((s, c))
        return ext

    def read_blocks(self, start, count):
        self.f.seek(self.base + start * self.block)
        return self.f.read(count * self.block)

    def records(self):
        """Every catalog leaf record: (parent_id, name, record_bytes, catalog_offset)."""
        node = self.first_leaf
        seen = 0
        while node:
            base = node * self.node_size
            nd = self.cat[base:base + self.node_size]
            flink, _, kind, _, nrec = struct.unpack_from('>IIbBH', nd, 0)
            if kind != -1:
                fail('catalog leaf chain is broken')
            offs = [struct.unpack_from('>H', nd, self.node_size - 2 * (i + 1))[0] for i in range(nrec + 1)]
            for i in range(nrec):
                o = offs[i]
                klen = struct.unpack_from('>H', nd, o)[0]
                parent = struct.unpack_from('>I', nd, o + 2)[0]
                nlen = struct.unpack_from('>H', nd, o + 6)[0]
                name = nd[o + 8:o + 8 + 2 * nlen].decode('utf-16-be')
                rec = o + 2 + klen
                rec += rec & 1
                yield parent, name, nd[rec:offs[i + 1]], base + rec
            seen += 1
            if seen > len(self.cat) // self.node_size:
                fail('catalog leaf chain loops')
            node = flink

    def lookup(self, path):
        index = {}
        for parent, name, rec, off in self.records():
            rtype = struct.unpack_from('>h', rec, 0)[0]
            if rtype in (1, 2):  # folder, file
                index[(parent, name)] = (rtype, rec, off)
        cnid = 2  # root folder
        parts = [p for p in path.split('/') if p]
        entry = None
        for k, part in enumerate(parts):
            entry = index.get((cnid, part))
            if not entry:
                fail('%s: no such file or folder in the image' % '/'.join([''] + parts[:k + 1]))
            rtype, rec, off = entry
            cnid = struct.unpack_from('>I', rec, 8)[0]
            if k < len(parts) - 1 and rtype != 1:
                fail('%s is not a folder' % part)
        if not entry or entry[0] != 2:
            fail('%s is not a file' % path)
        return entry

    def write_catalog(self, cat_off, data):
        """Write bytes at a catalog-file offset back to the image."""
        node = 0
        for s, c in self.cat_extents:
            span = c * self.block
            if cat_off < node + span:
                self.f.seek(self.base + s * self.block + (cat_off - node))
                self.f.write(data)
                return
            node += span
        fail('catalog offset outside the catalog')


def _open_file(f, hfs_path):
    v = Volume(f)
    _, rec, off = v.lookup(hfs_path)
    fork = 88  # data fork inside an HFSPlusCatalogFile record
    size = struct.unpack_from('>Q', rec, fork)[0]
    total = struct.unpack_from('>I', rec, fork + 12)[0]
    extents = Volume.fork_extents(rec, fork)
    if sum(c for _, c in extents) != total:
        fail('%s has overflow extents; not supported' % hfs_path)
    return v, off + fork, size, total, extents


def read_file(image, hfs_path):
    with open(image, 'rb') as f:
        v, _, size, _, extents = _open_file(f, hfs_path)
        return b''.join(v.read_blocks(s, c) for s, c in extents)[:size]


def write_file(image, hfs_path, new):
    """Replace the file's contents; returns the old size."""
    with open(image, 'r+b') as f:
        v, size_off, size, total, extents = _open_file(f, hfs_path)
        room = total * v.block
        if len(new) > room:
            fail('%s owns %d bytes; the new contents need %d' % (hfs_path, room, len(new)))
        padded = new + b'\0' * (room - len(new))
        pos = 0
        for s, c in extents:
            f.seek(v.base + s * v.block)
            f.write(padded[pos:pos + c * v.block])
            pos += c * v.block
        v.write_catalog(size_off, struct.pack('>Q', len(new)))
        f.flush()
        return size


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    image, hfs_path, src = sys.argv[1:]
    if src == '--cat':
        sys.stdout.buffer.write(read_file(image, hfs_path))
        return
    new = open(src, 'rb').read()
    old = write_file(image, hfs_path, new)
    print('hfs_put: %s: %d -> %d bytes' % (hfs_path, old, len(new)))


if __name__ == '__main__':
    main()
