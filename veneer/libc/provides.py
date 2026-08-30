#!/usr/bin/env python3
"""Read an ELF64 shared object and print the rpm provides el8's elfdeps writes.

WP-53. This is a reader, written against the file format rather than against
generate.py, so that a field the generator puts in the wrong place is caught
here instead of being round-tripped. It walks the section headers itself and
does not shell out to readelf.

Two kinds of line come out, in elfdeps' own order and spelling:

  <soname>()(64bit)              once, from DT_SONAME.
  <base>(<node>)(64bit)          once per non-base .gnu.version_d node.

The name in the version provides is the BASE VERDEF NODE, not DT_SONAME.
Spike 4 measured that difference on purpose: a library whose base node was not
its soname provided `libmisnamed.so.1(GLIBC_2.2.5)(64bit)` alongside
`libc.so.6()(64bit)`, and nothing an el8 binary requires would have been
satisfied by either. Keeping the two in step is the invariant, so they are read
from their two separate places here and compared by the caller.
"""
import argparse
import struct
import sys

SHT_DYNAMIC = 6
SHT_GNU_VERDEF = 0x6FFFFFFD
DT_NULL = 0
DT_SONAME = 14
VER_FLG_BASE = 1


class ElfError(Exception):
    pass


def u(fmt, buf, off):
    size = struct.calcsize(fmt)
    if off < 0 or off + size > len(buf):
        raise ElfError("read of %d bytes at %d runs past the %d-byte file"
                       % (size, off, len(buf)))
    return struct.unpack_from(fmt, buf, off)


def cstr(buf, off):
    if off < 0 or off >= len(buf):
        raise ElfError("string offset %d outside the file" % off)
    end = buf.find(b"\0", off)
    if end < 0:
        raise ElfError("unterminated string at %d" % off)
    return buf[off:end].decode("utf-8", "replace")


class Elf64:
    def __init__(self, buf):
        self.buf = buf
        if buf[:4] != b"\x7fELF":
            raise ElfError("not an ELF file")
        if buf[4] != 2:
            raise ElfError("not ELFCLASS64: elfdeps would not write (64bit)")
        if buf[5] != 1:
            raise ElfError("not little-endian")
        e_shoff, = u("<Q", buf, 0x28)
        e_shentsize, e_shnum, e_shstrndx = u("<HHH", buf, 0x3A)
        if e_shoff == 0 or e_shnum == 0:
            raise ElfError("no section headers")
        if e_shentsize != 64:
            raise ElfError("section header entry size %d, want 64" % e_shentsize)
        self.sections = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            name, styp, flags, addr, soff, size, link, info, align, entsize = \
                u("<IIQQQQIIQQ", buf, off)
            self.sections.append({
                "name": name, "type": styp, "addr": addr, "offset": soff,
                "size": size, "link": link, "info": info, "entsize": entsize,
            })
        if e_shstrndx >= len(self.sections):
            raise ElfError("section name string table index out of range")
        self.shstr = self.sections[e_shstrndx]["offset"]

    def section_name(self, sec):
        return cstr(self.buf, self.shstr + sec["name"])

    def find(self, styp):
        for sec in self.sections:
            if sec["type"] == styp:
                return sec
        return None

    def dynamic(self):
        """[(tag, value)] out of the SHT_DYNAMIC section."""
        sec = self.find(SHT_DYNAMIC)
        if sec is None:
            raise ElfError("no .dynamic section: not a dynamic object")
        out = []
        off = sec["offset"]
        for _ in range(sec["size"] // 16):
            tag, val = u("<qQ", self.buf, off)
            off += 16
            if tag == DT_NULL:
                break
            out.append((tag, val))
        return out

    def soname(self):
        sec = self.find(SHT_DYNAMIC)
        link = sec["link"] if sec else 0
        if link == 0 or link >= len(self.sections):
            raise ElfError(".dynamic links to no string table")
        base = self.sections[link]["offset"]
        for tag, val in self.dynamic():
            if tag == DT_SONAME:
                return cstr(self.buf, base + val)
        raise ElfError("no DT_SONAME")

    def verdef(self):
        """[(node name, flags, parents)] in .gnu.version_d order."""
        sec = self.find(SHT_GNU_VERDEF)
        if sec is None:
            return []
        link = sec["link"]
        if link == 0 or link >= len(self.sections):
            raise ElfError(".gnu.version_d links to no string table")
        strtab = self.sections[link]["offset"]
        out = []
        off = sec["offset"]
        seen = 0
        while True:
            version, flags, ndx, cnt, vhash, aux, nxt = \
                u("<HHHHIII", self.buf, off)
            if version != 1:
                raise ElfError("verdef revision %d, want 1" % version)
            if cnt < 1:
                raise ElfError("verdef entry %d names nobody" % seen)
            names = []
            aoff = off + aux
            for _ in range(cnt):
                aname, anext = u("<II", self.buf, aoff)
                names.append(cstr(self.buf, strtab + aname))
                if anext == 0:
                    break
                aoff += anext
            out.append((names[0], flags, names[1:]))
            seen += 1
            if nxt == 0:
                break
            if seen > sec["size"]:
                raise ElfError("verdef chain does not terminate")
            off += nxt
        if sec["info"] and sec["info"] != seen:
            raise ElfError("verdef holds %d entries, sh_info says %d"
                           % (seen, sec["info"]))
        return out


def provides(elf):
    soname = elf.soname()
    nodes = elf.verdef()
    base = [n for n, f, _p in nodes if f & VER_FLG_BASE]
    if len(base) != 1:
        raise ElfError("%d base verdef nodes, want exactly 1" % len(base))
    lines = ["%s()(64bit)" % soname]
    for name, flags, _parents in nodes:
        if flags & VER_FLG_BASE:
            continue
        lines.append("%s(%s)(64bit)" % (base[0], name))
    return soname, base[0], lines


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("object")
    ap.add_argument("--check-soname", metavar="NAME",
                    help="fail unless DT_SONAME and the base node are both NAME")
    ap.add_argument("--ladder", action="store_true",
                    help="print the node ladder as name<TAB>parent instead")
    args = ap.parse_args()
    with open(args.object, "rb") as fh:
        buf = fh.read()
    try:
        elf = Elf64(buf)
        soname, base, lines = provides(elf)
        ladder = elf.verdef()
    except ElfError as exc:
        sys.exit("%s: %s" % (args.object, exc))
    if args.check_soname is not None:
        if soname != args.check_soname:
            sys.exit("DT_SONAME is %r, want %r" % (soname, args.check_soname))
        if base != args.check_soname:
            sys.exit("base verdef node is %r, want %r"
                     % (base, args.check_soname))
    if args.ladder:
        for name, flags, parents in ladder:
            print("%s\t%s" % (name, parents[0] if parents else "-"))
        return
    for line in lines:
        print(line)


if __name__ == "__main__":
    main()
