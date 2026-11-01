#!/usr/bin/env python3
"""Read single members of an IPSW (a zip) on Apple's CDN, with HTTP range requests.

  ipsw.py URL list                 the members, with sizes
  ipsw.py URL get MEMBER OUT       one member, decompressed (stored or deflated)
  ipsw.py URL manifest             BuildManifest.plist's paths for the first identity:
                                   OS, Cryptex1,SystemOS, Cryptex1,AppOS, DeviceTree

Zip64 aware (the disk images are larger than 4 GB). Standard library only.
"""
import plistlib, struct, sys, urllib.request, zlib

def fetch(url, start, end):
    req = urllib.request.Request(url, headers={'Range': f'bytes={start}-{end}'})
    with urllib.request.urlopen(req) as r:
        return r.read()

def size_of(url):
    req = urllib.request.Request(url, method='HEAD')
    with urllib.request.urlopen(req) as r:
        return int(r.headers['Content-Length'])

def directory(url):
    total = size_of(url)
    tail = fetch(url, max(0, total - 65558), total - 1)
    e = tail.rfind(b'PK\x05\x06')
    count, cd_size, cd_off = struct.unpack_from('<HII', tail, e + 10)
    z = tail.rfind(b'PK\x06\x07')
    if z >= 0 and (cd_off == 0xffffffff or count == 0xffff):
        (z64,) = struct.unpack_from('<Q', tail, z + 8)
        rec = fetch(url, z64, z64 + 55)
        count, cd_size, cd_off = struct.unpack_from('<QQQ', rec, 32)
    cd = fetch(url, cd_off, cd_off + cd_size - 1)
    out, p = {}, 0
    for _ in range(count):
        method, csize, usize, nlen, xlen, clen = struct.unpack_from('<H8xII HHH', cd, p + 10)[0:1] + \
            struct.unpack_from('<IIHHH', cd, p + 20)
        (loff,) = struct.unpack_from('<I', cd, p + 42)
        name = cd[p + 46:p + 46 + nlen].decode()
        extra = cd[p + 46 + nlen:p + 46 + nlen + xlen]
        q = 0
        while q + 4 <= len(extra):
            hid, hlen = struct.unpack_from('<HH', extra, q)
            if hid == 1:
                vals, k = extra[q + 4:q + 4 + hlen], 0
                if usize == 0xffffffff: (usize,) = struct.unpack_from('<Q', vals, k); k += 8
                if csize == 0xffffffff: (csize,) = struct.unpack_from('<Q', vals, k); k += 8
                if loff == 0xffffffff: (loff,) = struct.unpack_from('<Q', vals, k); k += 8
            q += 4 + hlen
        out[name] = (method, csize, usize, loff)
        p += 46 + nlen + xlen + clen
    return out

def get(url, entries, name, out_path):
    method, csize, usize, loff = entries[name]
    hdr = fetch(url, loff, loff + 29)
    nlen, xlen = struct.unpack_from('<HH', hdr, 26)
    start = loff + 30 + nlen + xlen
    d = zlib.decompressobj(-15) if method == 8 else None
    with open(out_path, 'wb') as f:
        pos, chunk = 0, 64 << 20
        while pos < csize:
            data = fetch(url, start + pos, start + min(pos + chunk, csize) - 1)
            f.write(d.decompress(data) if d else data)
            pos += len(data)
            print(f'\r{name}: {pos >> 20} of {csize >> 20} MB', end='', file=sys.stderr)
        if d:
            f.write(d.flush())
    print(file=sys.stderr)

def main():
    url, cmd = sys.argv[1], sys.argv[2]
    entries = directory(url)
    if cmd == 'list':
        for n, (m, c, u, o) in sorted(entries.items()):
            print(f'{u:>12} {n}')
    elif cmd == 'get':
        get(url, entries, sys.argv[3], sys.argv[4])
    elif cmd == 'manifest':
        get(url, entries, 'BuildManifest.plist', '/dev/null') if False else None
        import io, tempfile
        with tempfile.NamedTemporaryFile() as t:
            get(url, entries, 'BuildManifest.plist', t.name)
            m = plistlib.load(open(t.name, 'rb'))
        ident = m['BuildIdentities'][0]['Manifest']
        for k in ('OS', 'Cryptex1,SystemOS', 'Cryptex1,AppOS', 'DeviceTree'):
            if k in ident:
                print(k, ident[k]['Info']['Path'])

if __name__ == '__main__':
    main()
