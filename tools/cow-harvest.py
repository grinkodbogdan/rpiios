#!/usr/bin/env python3
"""Save what a running iOS wrote to its root image, so later boots keep it.

Usage: cow-harvest.py OVERLAY_ADDR TARGET [--gdb HOST:PORT] [--stop-at KADDR]
                        [--monitor HMP_SOCKET] [--mark-clean] [--dry-run] [--resume]

The kernel mounts IOS7/ROOTFS.HFS read-only with a copy-on-write overlay in
RAM, so every boot is a first boot: lsd rebuilds its app database, installd
re-enumerates every app, containermanagerd recreates containers. Under QEMU
that is most of the boot time.

Boot once with the kernel's -gdb stub enabled (run10.sh passes
-gdb tcp::1234), wait until the system is idle (SpringBoard up, a minute
for the buffer cache to sync), then run this with the address the kernel
logged as "rpi3: cow overlay 0x...". --monitor (QEMU's -monitor unix socket)
makes it read the overlay in one memsave instead of GDB packets, which
is much faster. It stops the guest, reads every overlay
page through the GDB stub and writes it into TARGET:

  - an HFS image (rootfs.hfs): pages go at their file offsets;
  - an SD card image: IOS7/ROOTFS.HFS is found in its FAT32 partition and
    the pages go through the file's cluster chain.

--mark-clean sets the volume's cleanly-unmounted bit, without which the
kernel refuses the image as root (only do this after the guest has been
idle long enough for its 30-second sync to have flushed metadata).

Copy TARGET first if you want to keep the pristine image. The guest is left
stopped (--resume lets it run on); kill QEMU afterwards (the harvested state is what a clean shutdown
would have left, with HFS's journal replayed at the next mount).
"""
import socket
import struct
import sys

PAGE = 4096


class Gdb:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port))
        self.buf = b''
        self.s.sendall(b'\x03')  # stop the guest
        self.cmd('?')

    def _read_packet(self):
        while True:
            i = self.buf.find(b'$')
            j = self.buf.find(b'#', i + 1) if i >= 0 else -1
            if i >= 0 and j >= 0 and len(self.buf) >= j + 3:
                body = self.buf[i + 1:j]
                self.buf = self.buf[j + 3:]
                self.s.sendall(b'+')
                return body
            data = self.s.recv(65536)
            if not data:
                raise SystemExit('cow-harvest: GDB stub closed the connection')
            self.buf += data

    def cmd(self, text):
        body = text.encode()
        self.s.sendall(b'$%s#%02x' % (body, sum(body) & 0xff))
        while True:
            reply = self._read_packet()
            # skip console output and the stop notice the interrupt causes
            if not reply.startswith((b'O', b'T', b'S')) or text == '?':
                return reply

    def resume(self, monitor=None):
        """Let the guest run on. QEMU can leave it paused after a GDB
        detach, so with the HMP monitor also send 'cont' there."""
        self.cmd('D')
        self.s.close()
        if monitor:
            import time
            time.sleep(1)  # let QEMU finish the detach first
            m = socket.socket(socket.AF_UNIX)
            m.connect(monitor)
            m.sendall(b'cont\ninfo status\n')
            out = b''
            while b'VM status' not in out:
                data = m.recv(65536)
                if not data:
                    break
                out += data
            m.close()

    def settle(self, addr, stop_at=None):
        """Stop the guest in privileged mode: QEMU's debug reads use the
        current mode, and kernel pages are not readable from user mode."""
        if not self.cmd('m%x,4' % addr).startswith(b'E'):
            return
        if stop_at is None:
            raise SystemExit('cow-harvest: guest stopped in user mode; pass --stop-at '
                             'with the address of a busy kernel function '
                             '(thread_block_reason from the kernel\'s symbols)')
        self.cmd('Z1,%x,2' % stop_at)
        for _ in range(20):
            self.s.sendall(b'$c#63')
            self._read_packet()  # waits for the breakpoint's stop notice
            if not self.cmd('m%x,4' % addr).startswith(b'E'):
                self.cmd('z1,%x,2' % stop_at)
                return
        raise SystemExit('cow-harvest: kernel memory at %#x never readable' % addr)

    def read(self, addr, n):
        out = bytearray()
        while n:
            k = min(n, 2048)
            r = self.cmd('m%x,%x' % (addr, k))
            if r.startswith(b'E') or len(r) != 2 * k:
                raise SystemExit('cow-harvest: cannot read %#x (%r)' % (addr, r[:8]))
            out += bytes.fromhex(r.decode())
            addr += k
            n -= k
        return bytes(out)


def memsave(sock_path, addr, n):
    """Read guest virtual memory in one go through QEMU's HMP monitor (the
    guest must already be stopped in privileged mode, see Gdb.settle)."""
    import os
    import tempfile
    import time
    fd, tmp = tempfile.mkstemp(prefix='cow-harvest-')
    os.close(fd)
    s = socket.socket(socket.AF_UNIX)
    s.connect(sock_path)
    s.sendall(b'memsave 0x%x %d "%s"\n' % (addr, n, tmp.encode()))
    s.sendall(b'info version\n')
    out = b''
    while out.count(b'(qemu)') < 3:  # banner prompt, memsave, info version
        data = s.recv(65536)
        if not data:
            break
        out += data
    s.close()
    for _ in range(100):
        if os.path.getsize(tmp) == n:
            break
        time.sleep(0.1)
    data = open(tmp, 'rb').read()
    os.unlink(tmp)
    if len(data) != n:
        raise SystemExit('cow-harvest: memsave of %#x returned %d of %d bytes: %s'
                         % (addr, len(data), n, out.decode(errors='replace')[-1500:]))
    return data


def fat_file_map(f, path=('IOS7', 'ROOTFS  HFS')):
    """(cluster byte offsets, cluster size, file size) of IOS7/ROOTFS.HFS."""
    f.seek(0)
    mbr = f.read(512)
    if mbr[510:512] != b'\x55\xaa':
        raise SystemExit('cow-harvest: no MBR')
    base = struct.unpack_from('<I', mbr, 446 + 8)[0] * 512
    f.seek(base)
    bpb = f.read(512)
    bps, spc = struct.unpack_from('<HB', bpb, 11)
    reserved, nfats = struct.unpack_from('<HB', bpb, 14)
    fatsz, root = struct.unpack_from('<I', bpb, 36)[0], struct.unpack_from('<I', bpb, 44)[0]
    csize = bps * spc
    fat_off = base + reserved * bps
    data_off = base + (reserved + nfats * fatsz) * bps

    def chain(c):
        out = []
        while 2 <= c < 0x0ffffff8:
            out.append(data_off + (c - 2) * csize)
            f.seek(fat_off + 4 * c)
            c = struct.unpack('<I', f.read(4))[0] & 0x0fffffff
        return out

    cluster, size = root, None
    for name in path:
        found = None
        for off in chain(cluster):
            f.seek(off)
            d = f.read(csize)
            for i in range(0, csize, 32):
                e = d[i:i + 32]
                if e[0] == 0:
                    break
                if e[11] == 0x0f or e[0] == 0xe5:
                    continue
                if e[:11].decode('latin-1') == name.ljust(11):
                    hi, lo = struct.unpack_from('<H', e, 20)[0], struct.unpack_from('<H', e, 26)[0]
                    found = (hi << 16 | lo, struct.unpack_from('<I', e, 28)[0])
                    break
            if found:
                break
        if not found:
            raise SystemExit('cow-harvest: %s not found on the card' % '/'.join(path))
        cluster, size = found
    return chain(cluster), csize, size


def main():
    args = []
    skip = False
    for a in sys.argv[1:]:
        if skip:
            skip = False
        elif a in ('--gdb', '--stop-at', '--monitor'):
            skip = True
        elif not a.startswith('--'):
            args.append(a)
    opts = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    addr = int(args[0], 16)
    target = args[1]
    host, port = 'localhost', 1234
    if '--gdb' in opts:
        host, port = opts[opts.index('--gdb') + 1].rsplit(':', 1)
        port = int(port)
    dry = '--dry-run' in opts
    resume = '--resume' in opts

    g = Gdb(host, port)
    stop_at = int(opts[opts.index('--stop-at') + 1], 16) if '--stop-at' in opts else None
    g.settle(addr, stop_at)
    hdr = g.read(addr, 40)
    sectors, capacity, used, dirty, slots, arena, entries = struct.unpack_from('<QIIIIII', hdr, 0)
    print('cow-harvest: image %d sectors, %d of %d overlay pages used, %d hash slots'
          % (sectors, used, capacity, slots))
    monitor = opts[opts.index('--monitor') + 1] if '--monitor' in opts else None
    if monitor:
        table = memsave(monitor, entries, slots * 12)
    else:
        table = g.read(entries, slots * 12)
    pages = []
    for i in range(slots):
        page, slot, flags = struct.unpack_from('<III', table, i * 12)
        if flags & 1:
            pages.append((page, slot))
    pages.sort()
    print('cow-harvest: %d pages (%.1f MiB) to save' % (len(pages), len(pages) * PAGE / 1048576))
    if dry:
        if resume:
            g.resume(monitor)
        return

    with open(target, 'r+b') as f:
        head = f.read(512)
        if head[510:512] == b'\x55\xaa':
            clusters, csize, fsize = fat_file_map(f)
            if fsize != sectors * 512:
                raise SystemExit('cow-harvest: card holds a %d-byte root image, overlay covers %d'
                                 % (fsize, sectors * 512))

            step = min(csize, PAGE)

            def where(off):
                return clusters[off // csize] + off % csize
        else:
            step = PAGE

            def where(off):
                return off
        blob = memsave(monitor, arena, capacity * PAGE) if monitor else None
        for n, (page, slot) in enumerate(pages):
            if blob is not None:
                data = blob[slot * PAGE:(slot + 1) * PAGE]
            else:
                data = g.read(arena + slot * PAGE, PAGE)
            for k in range(0, PAGE, step):  # a page may span clusters
                f.seek(where(page * PAGE + k))
                f.write(data[k:k + step])
            if n % 1000 == 999:
                print('cow-harvest: %d/%d' % (n + 1, len(pages)))
        if '--mark-clean' in opts:
            # The kernel only mounts a root image marked cleanly unmounted
            # (kHFSVolumeUnmountedBit); a snapshot of a mounted volume isn't.
            for off in (1024, sectors * 512 - 1024):
                f.seek(where(off) + 4)
                attrs = struct.unpack('>I', f.read(4))[0]
                f.seek(where(off) + 4)
                f.write(struct.pack('>I', attrs | 0x100))
            print('cow-harvest: volume marked cleanly unmounted')
    print('cow-harvest: wrote %d pages into %s' % (len(pages), target))
    if resume:
        g.resume(monitor)


if __name__ == '__main__':
    main()
