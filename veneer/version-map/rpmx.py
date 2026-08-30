#!/usr/bin/env python3
"""Extract files from a binary RPM without rpm, rpm2cpio or cpio.

Usage: rpmx.py ARCHIVE DEST [PREFIX ...]

Unpacks the cpio payload of ARCHIVE under DEST, keeping only members whose
path begins with one of the PREFIXes; with none given, everything. Paths are
taken relative to the payload root, so a member recorded as ./usr/lib/rpm/foo
lands at DEST/usr/lib/rpm/foo. Symlinks are recreated as symlinks.

The probe host is not assumed to have rpm on it. It is assumed to have a
Python with lzma, zlib and bz2, which every build since 3.3 has had.
"""
import bz2
import gzip
import lzma
import os
import stat
import sys

LEAD = 96
HDR_MAGIC = b"\x8e\xad\xe8\x01\x00\x00\x00\x00"
TRAILER = "TRAILER!!!"


def header_len(blob, off):
    """Length of the rpm header structure starting at off, magic included."""
    if blob[off:off + 8] != HDR_MAGIC:
        raise ValueError("no rpm header magic at offset %d" % off)
    nindex = int.from_bytes(blob[off + 8:off + 12], "big")
    hsize = int.from_bytes(blob[off + 12:off + 16], "big")
    return 16 + 16 * nindex + hsize


def payload(blob):
    """The compressed payload of an rpm, located by walking its headers."""
    off = LEAD
    off += header_len(blob, off)
    off += -off % 8                      # the signature header is padded
    off += header_len(blob, off)
    return blob[off:]


def decompress(raw):
    if raw[:6] == b"\xfd7zXZ\x00":
        return lzma.decompress(raw), "xz"
    if raw[:2] == b"\x1f\x8b":
        return gzip.decompress(raw), "gzip"
    if raw[:3] == b"BZh":
        return bz2.decompress(raw), "bzip2"
    if raw[:4] == b"\x28\xb5\x2f\xfd":
        import compression.zstd as z    # Python 3.14 and later only
        return z.decompress(raw), "zstd"
    raise ValueError("unrecognized payload compression %r" % raw[:6])


def members(cpio):
    """Walk a newc-format cpio archive, yielding (name, mode, data)."""
    off = 0
    while True:
        if cpio[off:off + 6] not in (b"070701", b"070702"):
            raise ValueError("no cpio header at offset %d" % off)
        f = [int(cpio[off + 6 + 8 * i:off + 14 + 8 * i], 16) for i in range(13)]
        mode, size, namesize = f[1], f[6], f[11]
        name = cpio[off + 110:off + 110 + namesize - 1].decode("utf-8",
                                                              "surrogateescape")
        off += 110 + namesize
        off += -off % 4
        data = cpio[off:off + size]
        off += size
        off += -off % 4
        if name == TRAILER:
            return
        yield name, mode, data


def unpack(archive, dest, prefixes):
    with open(archive, "rb") as fh:
        blob = fh.read()
    cpio, how = decompress(payload(blob))
    kept = 0
    for name, mode, data in members(cpio):
        rel = name[2:] if name.startswith("./") else name
        if prefixes and not any(rel.startswith(p) for p in prefixes):
            continue
        out = os.path.join(dest, rel)
        if stat.S_ISDIR(mode):
            os.makedirs(out, exist_ok=True)
            continue
        os.makedirs(os.path.dirname(out), exist_ok=True)
        if stat.S_ISLNK(mode):
            target = data.decode("utf-8", "surrogateescape")
            if os.path.islink(out) or os.path.exists(out):
                os.unlink(out)
            os.symlink(target, out)
        elif stat.S_ISREG(mode):
            with open(out, "wb") as fh:
                fh.write(data)
            os.chmod(out, stat.S_IMODE(mode))
        else:
            continue
        kept += 1
    return how, kept


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    how, kept = unpack(argv[1], argv[2], argv[3:])
    print("%s compression=%s members=%d" % (os.path.basename(argv[1]),
                                            how, kept))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
