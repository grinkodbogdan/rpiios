#!/usr/bin/env python3
"""Fetch the iPhone 5 identity iOS 10 needs from Apple's restore IPSW.

Usage: ios10-identity.py OUT.bin [--model n41ap]

iOS 10's MobileGestalt identifies the device from the device tree's
/product node (a 20-byte product-id it looks up in its device table, plus
product-name, idiom, artwork scale and per-feature subnodes). Without it
every "what device is this" question is unanswerable: CommCenter crashes
on an unknown hardware model, and backboardd and MobileGestalt wait for
iPhone 7 hardware (haptics) that the iPhone 5 never had.

The iOS 10.3.4 (14G61) iPhone 5 IPSW's DeviceTree.<model>.img3 is not
encrypted. This reads only that file from Apple's CDN (HTTP range
requests into the 2 GB zip), extracts the /product subtree and writes it
as a flattened device-tree node that tools/pack-kernel.py --product adds
to the Pi's tree. The kernel shows it to iOS 10 only (iOS 7 keeps the
Pi's identity). Apple's firmware is fetched at build time, never stored in
this repository.
"""
import struct
import sys
import urllib.request
import zlib

IPSW = ('https://updates.cdn-apple.com/2019/ios/091-25277-20190722-0C1B94DE-992C-11E9-'
        'A2EE-E2C9A77C2E40/iPhone_4.0_32bit_10.3.4_14G61_Restore.ipsw')
IPSW_SIZE = 2000796302
# Only /product: with the iPhone's /baseband as well, SpringBoard never got
# past its check-in with backboardd.
NODES = ('product',)


def fetch(start, end):
    req = urllib.request.Request(IPSW, headers={'Range': 'bytes=%d-%d' % (start, end)})
    data = urllib.request.urlopen(req, timeout=120).read()
    if len(data) != end - start + 1:
        sys.exit('ios10-identity: short read from the IPSW')
    return data


def zip_member(name):
    """One member of the remote zip (central directory, then local header)."""
    tail = fetch(IPSW_SIZE - 65536, IPSW_SIZE - 1)
    i = tail.rfind(b'PK\x05\x06')
    cd_size, cd_off = struct.unpack_from('<II', tail, i + 12)
    cd = fetch(cd_off, cd_off + cd_size - 1)
    p = 0
    while p < len(cd):
        meth, csize, usize = struct.unpack_from('<H', cd, p + 10)[0], *struct.unpack_from('<II', cd, p + 20)
        nlen, elen, clen = struct.unpack_from('<HHH', cd, p + 28)
        local = struct.unpack_from('<I', cd, p + 42)[0]
        if cd[p + 46:p + 46 + nlen].decode() == name:
            h = fetch(local, local + 29)
            lnl, lel = struct.unpack_from('<HH', h, 26)
            body = fetch(local + 30 + lnl + lel, local + 30 + lnl + lel + csize - 1)
            data = zlib.decompress(body, -15) if meth == 8 else body
            if len(data) != usize:
                sys.exit('ios10-identity: %s did not inflate to its size' % name)
            return data
        p += 46 + nlen + elen + clen
    sys.exit('ios10-identity: %s not in the IPSW' % name)


def img3_data(img3):
    if img3[:4] != b'3gmI':
        sys.exit('ios10-identity: not an IMG3 file')
    p, payload = 20, None
    while p + 12 <= len(img3):
        tag, total, size = struct.unpack_from('<4sII', img3, p)
        tag = tag[::-1]
        if tag == b'KBAG':
            sys.exit('ios10-identity: the device tree is encrypted')
        if tag == b'DATA':
            payload = img3[p + 12:p + 12 + size]
        if total == 0:
            break
        p += total
    if payload is None:
        sys.exit('ios10-identity: no DATA in the IMG3')
    return payload


def parse(dt, off=0):
    """(props, children, end) of a flattened device-tree node."""
    nprops, nchildren = struct.unpack_from('<II', dt, off)
    off += 8
    props = []
    for _ in range(nprops):
        name = dt[off:off + 32].split(b'\0')[0].decode()
        length = struct.unpack_from('<I', dt, off + 32)[0] & 0x7fffffff
        props.append((name, dt[off + 36:off + 36 + length]))
        off += 36 + ((length + 3) & ~3)
    children = []
    for _ in range(nchildren):
        child, off = parse(dt, off)
        children.append(child)
    return (props, children), off


def encode(node):
    props, children = node
    out = struct.pack('<II', len(props), len(children))
    for name, value in props:
        out += name.encode().ljust(32, b'\0') + struct.pack('<I', len(value))
        out += value + b'\0' * (-len(value) % 4)
    for child in children:
        out += encode(child)
    return out


# Links into the iPhone's own tree: its phandles can clash with the Pi's,
# and the kernel maps interrupts against controllers the Pi does not have
# (a kernel data abort in IODTMapInterrupts). iOS reads only the rest.
DROPPED = ('AAPL,phandle', 'interrupt-parent', 'interrupts')


def portable(node):
    props, children = node
    return ([(n, v) for n, v in props if n not in DROPPED],
            [portable(c) for c in children])


def name_of(node):
    return dict(node[0]).get('name', b'').rstrip(b'\0').decode()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        sys.exit(__doc__)
    model = sys.argv[sys.argv.index('--model') + 1] if '--model' in sys.argv else 'n41ap'
    dt = img3_data(zip_member('Firmware/all_flash/DeviceTree.%s.img3' % model))
    (root, _) = parse(dt)
    out = b''
    for want in NODES:
        node = next((c for c in root[1] if name_of(c) == want), None)
        if node is None:
            sys.exit('ios10-identity: no /%s node' % want)
        node = portable(node)
        # Shipped under a neutral name; the kernel renames it for iOS 10 only.
        props = [(n, b'rpi3-ios10-' + want.encode() + b'\0' if n == 'name' else v)
                 for n, v in node[0]]
        out += encode((props, node[1]))
    open(args[0], 'wb').write(out)
    product = next(c for c in root[1] if name_of(c) == 'product')
    print('ios10-identity: %s %s (%s, product-id %s) -> %s'
          % (model, ' '.join('/' + n for n in NODES),
             dict(product[0]).get('product-name', b'?').rstrip(b'\0').decode(),
             dict(product[0]).get('product-id', b'').hex(), args[0]))


if __name__ == '__main__':
    main()
