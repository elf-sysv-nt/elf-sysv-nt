#!/usr/bin/env python3
"""Write a versioned ELF shared object, and the program that needs it.

Usage:
  synth-libc.py [options]

Options:
  -o FILE, --output=FILE    Where to write. [default: libc.so.6]
  -s NAME, --soname=NAME    DT_SONAME, and by default the name of the base
                            verdef node too. [default: libc.so.6]
  -b NAME, --base=NAME      Name the base verdef node separately from
                            DT_SONAME. The two are the same in any library a
                            linker produced; splitting them is how the probe
                            shows which of the two a provide is read from.
  -n LIST, --versions=LIST  Comma-separated version nodes, in order.
                            [default: GLIBC_2.2.5]
  -S NAME, --symbol=NAME    The symbol bound to each node. [default: printf]
  -c, --consumer            Emit the executable that needs the library, with
                            .gnu.version_r, rather than the library itself.
      --no-consumer         Emit the library. This is the default.
  -q, --quiet               Write the file and say nothing.
  -V, --version             Print the version and exit.
  -h, --help                Print this message and exit.

Nothing here is compiled and nothing is linked. The point of the spike is what
a dependency generator reads out of a library this project synthesized, and a
library gcc and ld produced would answer a different question. So the file is
laid out byte by byte: a load segment, a dynamic segment, the two hash tables
a modern DSO carries, .dynsym and .dynstr, .gnu.version, and either
.gnu.version_d or .gnu.version_r. It is small, it is a valid ELF object that
readelf will parse, and every field that matters is one this program chose.

One symbol per version node, distinguished by a suffix past the first, because
what is being measured is the version records rather than the symbol table.
"""
import argparse
import os
import struct
import sys

EI_NIDENT = 16
ET_EXEC, ET_DYN = 2, 3
EM_X86_64 = 62
PT_LOAD, PT_DYNAMIC, PT_INTERP = 1, 2, 3
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_DYNAMIC, SHT_DYNSYM = 1, 2, 3, 6, 11
SHT_HASH = 5
SHT_GNU_HASH = 0x6FFFFFF6
SHT_GNU_VERDEF = 0x6FFFFFFD
SHT_GNU_VERNEED = 0x6FFFFFFE
SHT_GNU_VERSYM = 0x6FFFFFFF
SHF_ALLOC, SHF_EXECINSTR, SHF_WRITE = 0x2, 0x4, 0x1
VER_FLG_BASE = 1
INTERP = "/lib64/ld-linux-x86-64.so.2"

DT_NULL, DT_NEEDED, DT_HASH, DT_STRTAB, DT_SYMTAB = 0, 1, 4, 5, 6
DT_STRSZ, DT_SYMENT, DT_SONAME = 10, 11, 14
DT_GNU_HASH = 0x6FFFFEF5
DT_VERSYM = 0x6FFFFFF0
DT_VERDEF, DT_VERDEFNUM = 0x6FFFFFFC, 0x6FFFFFFD
DT_VERNEED, DT_VERNEEDNUM = 0x6FFFFFFE, 0x6FFFFFFF


def elf_hash(name):
    """The SysV hash, as the ELF gABI specifies it."""
    h = 0
    for c in name.encode("ascii"):
        h = (h << 4) + c
        g = h & 0xF0000000
        if g:
            h ^= g >> 24
        h &= ~g & 0xFFFFFFFF
    return h


class Strtab:
    def __init__(self):
        self.blob = bytearray(b"\0")
        self.at = {"": 0}

    def add(self, s):
        if s not in self.at:
            self.at[s] = len(self.blob)
            self.blob += s.encode("utf-8") + b"\0"
        return self.at[s]


class Section:
    def __init__(self, name, type_, flags=0, link=0, info=0, align=1,
                 entsize=0, data=b""):
        self.name = name
        self.type = type_
        self.flags = flags
        self.link = link
        self.info = info
        self.align = align
        self.entsize = entsize
        self.data = bytes(data)
        self.offset = 0
        self.addr = 0
        self.index = 0


def gnu_hash(name):
    h = 5381
    for c in name.encode("ascii"):
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def sysv_hash_table(nsyms):
    """One bucket, every symbol on its chain. Small, and actually correct."""
    bucket = [1 if nsyms > 1 else 0]
    chain = [0] * nsyms
    for i in range(1, nsyms - 1):
        chain[i] = i + 1
    return struct.pack("<II", 1, nsyms) + struct.pack(
        "<%dI" % len(bucket), *bucket) + struct.pack("<%dI" % nsyms, *chain)


def gnu_hash_table(names, defined):
    """One bucket and a one-word bloom filter, which is a valid table.

    An undefined symbol is never in the GNU hash, so a consumer's table is
    the empty form: symoffset past the end and a cleared bloom word.
    """
    nsyms = len(names) + 1
    if not defined:
        return struct.pack("<IIII", 1, nsyms, 1, 0) + struct.pack(
            "<Q", 0) + struct.pack("<I", 0)
    hashes = [gnu_hash(n) for n in names]
    bloom = 0
    for h in hashes:
        bloom |= 1 << (h % 64)
    vals = [h & ~1 for h in hashes]
    vals[-1] |= 1
    return (struct.pack("<IIII", 1, 1, 1, 0)
            + struct.pack("<Q", bloom)
            + struct.pack("<I", 1)
            + struct.pack("<%dI" % len(vals), *vals))


def verdef_table(strtab, soname, versions):
    """.gnu.version_d: a base node naming the library, then one node each."""
    out = b""
    nodes = [(VER_FLG_BASE, soname)] + [(0, v) for v in versions]
    for i, (flags, name) in enumerate(nodes):
        last = i == len(nodes) - 1
        aux = struct.pack("<II", strtab.add(name), 0)
        out += struct.pack("<HHHHIII", 1, flags, i + 1, 1, elf_hash(name),
                           20, 0 if last else 28) + aux
    return out, len(nodes)


def verneed_table(strtab, soname, versions):
    """.gnu.version_r: one entry for the library, one aux per node needed."""
    file_off = strtab.add(soname)
    aux = b""
    for i, name in enumerate(versions):
        last = i == len(versions) - 1
        aux += struct.pack("<IHHII", elf_hash(name), 0, i + 2,
                           strtab.add(name), 0 if last else 16)
    head = struct.pack("<HHIII", 1, len(versions), file_off, 16, 0)
    return head + aux, 1


def dynsym_table(strtab, names, defined, text_index):
    """A null entry, then one global symbol per version node."""
    out = struct.pack("<IBBHQQ", 0, 0, 0, 0, 0, 0)
    for name in names:
        info = (1 << 4) | 2                      # STB_GLOBAL, STT_FUNC
        shndx = text_index if defined else 0
        value = 0
        out += struct.pack("<IBBHQQ", strtab.add(name), info, 0, shndx,
                           value, 0)
    return out


def build(soname, base, versions, symbol, consumer):
    strtab = Strtab()
    names = [symbol] + ["%s_%d" % (symbol, i) for i in range(1, len(versions))]
    strtab.add(soname)

    if consumer:
        vertab, verinfo = verneed_table(strtab, soname, versions)
        vertype, vername = SHT_GNU_VERNEED, ".gnu.version_r"
    else:
        vertab, verinfo = verdef_table(strtab, base, versions)
        vertype, vername = SHT_GNU_VERDEF, ".gnu.version_d"

    versym = struct.pack("<H", 0) + b"".join(
        struct.pack("<H", i + 2) for i in range(len(versions)))
    text = b"\xc3"                                   # one ret, so .text is real

    secs = [Section("", 0)]
    if consumer:
        secs.append(Section(".interp", SHT_PROGBITS, SHF_ALLOC, align=1,
                            data=INTERP.encode() + b"\0"))
    secs += [
        Section(".hash", SHT_HASH, SHF_ALLOC, align=8, entsize=4),
        Section(".gnu.hash", SHT_GNU_HASH, SHF_ALLOC, align=8),
        Section(".dynsym", SHT_DYNSYM, SHF_ALLOC, info=1, align=8, entsize=24),
        Section(".dynstr", SHT_STRTAB, SHF_ALLOC, align=1),
        Section(".gnu.version", SHT_GNU_VERSYM, SHF_ALLOC, align=2, entsize=2),
        Section(vername, vertype, SHF_ALLOC, info=verinfo, align=8),
        Section(".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, align=16,
                data=text),
        Section(".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, align=8,
                entsize=16),
        Section(".shstrtab", SHT_STRTAB, align=1),
    ]
    idx = {s.name: i for i, s in enumerate(secs)}
    for i, s in enumerate(secs):
        s.index = i
    return secs, idx, strtab, names, versym, vertab


def emit(soname, base, versions, symbol, consumer):
    secs, idx, strtab, names, versym, vertab = build(
        soname, base, versions, symbol, consumer)
    defined = not consumer
    nsyms = len(names) + 1

    secs[idx[".hash"]].data = sysv_hash_table(nsyms)
    secs[idx[".gnu.hash"]].data = gnu_hash_table(names, defined)
    secs[idx[".dynsym"]].data = dynsym_table(strtab, names, defined,
                                             idx[".text"])
    secs[idx[".gnu.version"]].data = versym
    secs[idx[".gnu.version_r" if consumer else ".gnu.version_d"]].data = vertab
    secs[idx[".dynsym"]].link = idx[".dynstr"]
    secs[idx[".hash"]].link = idx[".dynsym"]
    secs[idx[".gnu.hash"]].link = idx[".dynsym"]
    secs[idx[".gnu.version"]].link = idx[".dynsym"]
    secs[idx[".gnu.version_r" if consumer else ".gnu.version_d"]].link = \
        idx[".dynstr"]
    secs[idx[".dynamic"]].link = idx[".dynstr"]

    ndyn = 11        # the same count either way; see the two lists below
    secs[idx[".dynamic"]].data = b"\0" * (16 * ndyn)
    secs[idx[".dynstr"]].data = bytes(strtab.blob)

    shstr = Strtab()
    for s in secs:
        s.shname = shstr.add(s.name)
    secs[idx[".shstrtab"]].data = bytes(shstr.blob)

    base = 0x400000 if consumer else 0
    nph = 3 if consumer else 2
    off = 64 + 56 * nph
    for s in secs[1:]:
        off += -off % max(s.align, 1)
        s.offset = off
        s.addr = (base + off) if (s.flags & SHF_ALLOC) else 0
        off += len(s.data)
    end = off

    dyn = []
    if consumer:
        dyn.append((DT_NEEDED, strtab.at[soname]))
    dyn += [
        (DT_HASH, secs[idx[".hash"]].addr),
        (DT_GNU_HASH, secs[idx[".gnu.hash"]].addr),
        (DT_STRTAB, secs[idx[".dynstr"]].addr),
        (DT_SYMTAB, secs[idx[".dynsym"]].addr),
        (DT_STRSZ, len(secs[idx[".dynstr"]].data)),
        (DT_SYMENT, 24),
    ]
    if consumer:
        dyn += [(DT_VERSYM, secs[idx[".gnu.version"]].addr),
                (DT_VERNEED, secs[idx[".gnu.version_r"]].addr),
                (DT_VERNEEDNUM, 1)]
    else:
        dyn += [(DT_SONAME, strtab.at[soname]),
                (DT_VERSYM, secs[idx[".gnu.version"]].addr),
                (DT_VERDEF, secs[idx[".gnu.version_d"]].addr),
                (DT_VERDEFNUM, len(versions) + 1)]
    dyn.append((DT_NULL, 0))
    secs[idx[".dynamic"]].data = b"".join(
        struct.pack("<qQ", t, v) for t, v in dyn)

    if defined:                       # now that .text has an address
        sym = bytearray(secs[idx[".dynsym"]].data)
        for i in range(1, nsyms):
            struct.pack_into("<Q", sym, 24 * i + 8, secs[idx[".text"]].addr)
        secs[idx[".dynsym"]].data = bytes(sym)

    shoff = end + (-end % 8)
    ehdr = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF\x02\x01\x01" + b"\0" * 9,
        ET_EXEC if consumer else ET_DYN, EM_X86_64, 1,
        secs[idx[".text"]].addr if consumer else 0,
        64, shoff, 0, 64, 56, nph, 64, len(secs), idx[".shstrtab"])

    phdrs = [struct.pack("<IIQQQQQQ", PT_LOAD, 5, 0, base, base, end, end,
                         0x1000)]
    d = secs[idx[".dynamic"]]
    phdrs.append(struct.pack("<IIQQQQQQ", PT_DYNAMIC, 6, d.offset, d.addr,
                             d.addr, len(d.data), len(d.data), 8))
    if consumer:
        it = secs[idx[".interp"]]
        phdrs.append(struct.pack("<IIQQQQQQ", PT_INTERP, 4, it.offset,
                                 it.addr, it.addr, len(it.data),
                                 len(it.data), 1))

    blob = bytearray(ehdr + b"".join(phdrs))
    for s in secs[1:]:
        blob += b"\0" * (s.offset - len(blob))
        blob += s.data
    blob += b"\0" * (shoff - len(blob))
    for s in secs:
        blob += struct.pack("<IIQQQQIIQQ", s.shname, s.type, s.flags, s.addr,
                            s.offset, len(s.data), s.link, s.info,
                            s.align, s.entsize)
    return bytes(blob)


def main(argv):
    p = argparse.ArgumentParser(add_help=False, usage=__doc__)
    p.add_argument("-o", "--output", default="libc.so.6")
    p.add_argument("-s", "--soname", default="libc.so.6")
    p.add_argument("-b", "--base", default=None)
    p.add_argument("-n", "--versions", default="GLIBC_2.2.5")
    p.add_argument("-S", "--symbol", default="printf")
    p.add_argument("-c", "--consumer", action="store_true", default=False)
    p.add_argument("--no-consumer", dest="consumer", action="store_false")
    p.add_argument("-q", "--quiet", action="store_true")
    p.add_argument("-V", "--version", action="version",
                   version="synth-libc 1.0")
    p.add_argument("-h", "--help", action="help")
    a = p.parse_args(argv[1:])

    versions = [v for v in a.versions.split(",") if v]
    if not versions:
        sys.stderr.write("synth-libc: --versions is empty\n")
        return 2
    blob = emit(a.soname, a.base or a.soname, versions, a.symbol, a.consumer)
    with open(a.output, "wb") as fh:
        fh.write(blob)
    os.chmod(a.output, 0o755)
    if not a.quiet:
        print("%s kind=%s soname=%s nodes=%d bytes=%d" % (
            a.output, "consumer" if a.consumer else "library", a.soname,
            len(versions), len(blob)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
