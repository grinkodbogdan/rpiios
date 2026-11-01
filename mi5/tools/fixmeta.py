#!/usr/bin/env python3
"""Give a tree that 7-Zip extracted from an APFS image the metadata of the image.

  sudo python3 -I fixmeta.py LISTING DIR

LISTING is `7z l -slt IMAGE` and DIR is where `7z x` (and unzbm) put the files. 7-Zip
leaves every file owned by whoever ran it and writes hard links as separate copies; the
listing has what the image holds: owner, group, mode (with setuid, setgid and sticky),
the symbolic link target, and the iNode that hard links share. This puts all of it back:
owners and modes, symbolic links where 7-Zip wrote a plain file (or nothing), hard links
for every path of an iNode after the first, and the modification times.

Must run as root (chown). Paths with an alternate stream (`name:com.apple.decmpfs`, left by
7-Zip for LZBITMAP files until unzbm expands them) are skipped. Entries missing from DIR
are reported, and the run fails if there are any, except those excluded on purpose with
--exclude GLOB (repeatable, matched against the path inside DIR, as 7z's -xr! was).
"""
import fnmatch
import os
import stat
import sys

PERM = {'r': 4, 'w': 2, 'x': 1}


def parse_mode(s):
    """'-rwsr-xr-x' -> (type char, permission bits incl. setuid/setgid/sticky)."""
    if len(s) != 10:
        raise ValueError('mode %r' % s)
    bits = 0
    for i, who in enumerate((6, 3, 0)):
        r, w, x = s[1 + 3 * i:4 + 3 * i]
        bits |= (4 if r == 'r' else 0) << who
        bits |= (2 if w == 'w' else 0) << who
        if x in 'xst':
            bits |= 1 << who
        if x in 'sS' and i < 2:
            bits |= stat.S_ISUID if i == 0 else stat.S_ISGID
        if x in 'tT' and i == 2:
            bits |= stat.S_ISVTX
    return s[0], bits


def records(path):
    rec = None
    started = False
    with open(path, encoding='utf-8', errors='surrogateescape') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('----------'):
                started = True
                continue
            if not started:
                continue
            if line.startswith('Path = '):
                if rec:
                    yield rec
                rec = {'Path': line[7:]}
            elif rec is not None and ' = ' in line:
                k, v = line.split(' = ', 1)
                rec[k] = v
    if rec:
        yield rec


def main():
    args = sys.argv[1:]
    skip = []
    while '--exclude' in args:
        i = args.index('--exclude')
        skip.append(args[i + 1])
        del args[i:i + 2]
    listing, top = args
    if os.geteuid() != 0:
        sys.exit('fixmeta.py: run as root (owners are set with chown)')
    seen = {}       # iNode -> first path, for hard links
    dirs = []
    missing = 0
    n = links = symlinks = 0
    for r in records(listing):
        if r.get('Alternate Stream') == '+' or 'Mode' not in r or not r['Mode']:
            continue
        rel = r['Path']
        if any(fnmatch.fnmatchcase(os.path.basename(rel), g) or fnmatch.fnmatchcase(rel, g) for g in skip):
            continue
        p = os.path.join(top, rel)
        kind, bits = parse_mode(r['Mode'])
        uid, gid = int(r['User ID'] or 0), int(r['Group ID'] or 0)
        target = r.get('Symbolic Link', '')
        if kind == 'l':
            if not os.path.islink(p) or os.readlink(p) != target:
                if os.path.lexists(p):
                    os.unlink(p)
                os.makedirs(os.path.dirname(p), exist_ok=True)
                os.symlink(target, p)
                symlinks += 1
            os.lchown(p, uid, gid)
            n += 1
            continue
        if not os.path.lexists(p):
            print('fixmeta.py: missing %s' % rel, file=sys.stderr)
            missing += 1
            continue
        if kind == '-':
            ino = r.get('iNode')
            if r.get('Links', '') not in ('', '1') and ino:
                first = seen.get(ino)
                if first is None:
                    seen[ino] = p
                else:
                    os.unlink(p)
                    os.link(first, p)
                    links += 1
                    n += 1
                    continue
        os.lchown(p, uid, gid)
        if kind == 'd':
            dirs.append((p, bits))
        else:
            os.chmod(p, bits)
        n += 1
    # directories last, deepest first: a restrictive mode never gets in the way above
    for p, bits in reversed(dirs):
        os.chmod(p, bits)
    print('fixmeta.py: %s: %d entries, %d hard links, %d symbolic links made, %d missing'
          % (top, n, links, symlinks, missing))
    sys.exit(1 if missing else 0)


if __name__ == '__main__':
    main()
