#!/usr/bin/env python3
"""Extract the symbol-to-version-node map from vendor glibc binaries.

Usage:
  extract-version-map.py [options]

Options:
  -t DIR, --tree=DIR        Root the unpacked vendor RPMs live under; each
                            library path in the manifest is taken relative to
                            it. [default: .]
  -l FILE, --libraries=FILE The manifest: one 'soname<TAB>pkg<TAB>relpath' row
                            per library, in the order they are emitted.
                            [default: beside this script]
  -m FILE, --map=FILE       Write the symbol map here; - is stdout.
                            [default: -]
  -n FILE, --nodes=FILE     Also write the version-node ladders here. Omitted
                            means do not write them.
  -c, --counts              Print counts to stderr, one key=value per line.
  -V, --version             Print the version and exit.
  -h, --help                Print this message and exit.

Nothing is compiled and nothing calls readelf. The library is parsed field by
field -- section headers, .dynsym, .gnu.version, .gnu.version_d, .dynamic --
so the map is this program's own reading of the vendor file rather than a
scrape of another tool's formatting. The reproduce test cross-checks the
result against readelf, which is a second parser and so a real check.

The map row, tab-separated, no header (the README carries the legend):

  soname  symbol  version  binding  type  bind

version is the .gnu.version_d node the symbol is bound to; '-' for a defined
symbol carrying no version. binding is 'default' for the @@ form and 'hidden'
for the @ form. type is func|object|tls|ifunc|notype|other and bind is
global|weak|local. Only defined symbols are emitted; an undefined import
belongs to a .gnu.version_r requirement rather than to what the library
provides.

The node row, tab-separated:

  soname  index  node  flags  parents

flags is base|none|weak; parents is the parent chain of the node, nearest
first, comma-joined, or '-'. The base node is the one carrying VER_FLG_BASE,
and spike 4's trap is enforced here: its name must equal DT_SONAME, or the
extraction fails rather than emit a map that satisfies no package.
"""
import os
import struct
import sys

SHT_STRTAB = 3
SHT_DYNAMIC = 6
SHT_DYNSYM = 11
SHT_GNU_VERDEF = 0x6FFFFFFD
SHT_GNU_VERSYM = 0x6FFFFFFF

DT_STRTAB = 5
DT_SONAME = 14

SHN_UNDEF = 0
VER_FLG_BASE = 1
VER_FLG_WEAK = 2

STB = {0: "local", 1: "global", 2: "weak"}
STT = {0: "notype", 1: "object", 2: "func", 6: "tls", 10: "ifunc"}


class Elf:
    """Enough of an ELF64 little-endian reader for the version tables."""

    def __init__(self, path):
        with open(path, "rb") as fh:
            self.blob = fh.read()
        b = self.blob
        if b[:4] != b"\x7fELF":
            raise ValueError("%s: not ELF" % path)
        if b[4] != 2 or b[5] != 1:
            raise ValueError("%s: not 64-bit little-endian" % path)
        (self.shoff,) = struct.unpack_from("<Q", b, 0x28)
        self.shentsize, self.shnum, self.shstrndx = struct.unpack_from(
            "<HHH", b, 0x3A)
        self.secs = [self._shdr(i) for i in range(self.shnum)]

    def _shdr(self, i):
        off = self.shoff + i * self.shentsize
        (name, typ, flags, addr, offset, size, link, info, align,
         entsize) = struct.unpack_from("<IIQQQQIIQQ", self.blob, off)
        return dict(name=name, type=typ, flags=flags, addr=addr,
                    offset=offset, size=size, link=link, info=info,
                    entsize=entsize)

    def _bytes(self, sec):
        return self.blob[sec["offset"]:sec["offset"] + sec["size"]]

    def by_type(self, typ):
        for s in self.secs:
            if s["type"] == typ:
                return s
        return None


def cstr(blob, at):
    end = blob.find(b"\0", at)
    return blob[at:end].decode("utf-8", "surrogateescape")


def read_verdef(elf, sec, dynstr):
    """Return (ndx -> name), base_ndx, base_name, and the ladder rows.

    Each ladder row is (ndx, name, flags_word, [parent names]). The node's own
    name is its first verdaux; any further auxes are the parent chain.
    """
    blob = elf.blob
    base = sec["offset"]
    count = sec["info"]
    ndx_name = {}
    base_ndx = None
    base_name = None
    ladder = []
    off = base
    for _ in range(count):
        (ver, flags, ndx, cnt, vhash, aux, nxt) = struct.unpack_from(
            "<HHHHIII", blob, off)
        auxoff = off + aux
        names = []
        for _a in range(cnt):
            (vda_name, vda_next) = struct.unpack_from("<II", blob, auxoff)
            names.append(cstr(dynstr, vda_name))
            if vda_next == 0:
                break
            auxoff += vda_next
        node = names[0]
        parents = names[1:]
        ndx_name[ndx] = node
        if flags & VER_FLG_BASE:
            base_ndx = ndx
            base_name = node
        ladder.append((ndx, node, flags, parents))
        if nxt == 0:
            break
        off += nxt
    return ndx_name, base_ndx, base_name, ladder


def soname_of(elf):
    dyn = elf.by_type(SHT_DYNAMIC)
    if dyn is None:
        return None
    # DT_STRTAB is a virtual address; map it back to a file offset through the
    # section whose addr it names, which is .dynstr.
    blob = elf.blob
    data = elf._bytes(dyn)
    strtab_addr = None
    soname_off = None
    for i in range(0, len(data), 16):
        tag, val = struct.unpack_from("<qQ", data, i)
        if tag == 0:
            break
        if tag == DT_STRTAB:
            strtab_addr = val
        elif tag == DT_SONAME:
            soname_off = val
    if soname_off is None:
        return None
    for s in elf.secs:
        if s["type"] == SHT_STRTAB and s["addr"] == strtab_addr:
            return cstr(elf._bytes(s), soname_off)
    return None


def extract(path):
    """Parse one library. Return (soname, map_rows, node_rows)."""
    elf = Elf(path)
    dynsym = elf.by_type(SHT_DYNSYM)
    if dynsym is None:
        raise ValueError("%s: no .dynsym" % path)
    dynstr = elf._bytes(elf.secs[dynsym["link"]])
    soname = soname_of(elf) or os.path.basename(path)

    verdef = elf.by_type(SHT_GNU_VERDEF)
    ndx_name, base_ndx, base_name, ladder = ({}, None, None, [])
    if verdef is not None:
        ndx_name, base_ndx, base_name, ladder = read_verdef(
            elf, verdef, dynstr)
        if base_name is not None and base_name != soname:
            raise ValueError(
                "%s: base version-def node %r is not DT_SONAME %r "
                "(spike 4's trap)" % (path, base_name, soname))

    versym = elf.by_type(SHT_GNU_VERSYM)
    vs = elf._bytes(versym) if versym is not None else b""

    symblob = elf._bytes(dynsym)
    nsyms = len(symblob) // 24
    map_rows = []
    for i in range(1, nsyms):
        (st_name, st_info, st_other, st_shndx, st_value,
         st_size) = struct.unpack_from("<IBBHQQ", symblob, i * 24)
        if st_shndx == SHN_UNDEF:
            continue
        name = cstr(dynstr, st_name)
        if not name:
            continue
        bind = STB.get(st_info >> 4, "other")
        typ = STT.get(st_info & 0xF, "other")
        raw = struct.unpack_from("<H", vs, i * 2)[0] if i * 2 + 2 <= len(vs) \
            else 0
        hidden = bool(raw & 0x8000)
        vndx = raw & 0x7FFF
        if vndx <= 1:
            version = "-"
            binding = "default"
        else:
            version = ndx_name.get(vndx, "?%d" % vndx)
            binding = "hidden" if hidden else "default"
        map_rows.append((soname, name, version, binding, typ, bind))

    node_rows = []
    for (ndx, node, flags, parents) in ladder:
        f = "base" if flags & VER_FLG_BASE else (
            "weak" if flags & VER_FLG_WEAK else "none")
        node_rows.append((soname, ndx, node, f,
                          ",".join(parents) if parents else "-"))
    return soname, map_rows, node_rows


def main(argv):
    tree = "."
    here = os.path.dirname(os.path.abspath(__file__))
    libraries = os.path.join(here, "libraries.tsv")
    mapout = "-"
    nodesout = None
    counts = False
    args = argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-h", "--help"):
            sys.stdout.write(__doc__)
            return 0
        if a in ("-V", "--version"):
            print("extract-version-map 1.0")
            return 0
        if a in ("-c", "--counts"):
            counts = True
            i += 1
            continue

        def val(a, i):
            if "=" in a:
                return a.split("=", 1)[1], i + 1
            return args[i + 1], i + 2
        if a in ("-t", "--tree") or a.startswith("--tree="):
            tree, i = val(a, i)
        elif a in ("-l", "--libraries") or a.startswith("--libraries="):
            libraries, i = val(a, i)
        elif a in ("-m", "--map") or a.startswith("--map="):
            mapout, i = val(a, i)
        elif a in ("-n", "--nodes") or a.startswith("--nodes="):
            nodesout, i = val(a, i)
        else:
            sys.stderr.write("extract-version-map: unknown option %s\n" % a)
            return 2

    order = []
    with open(libraries) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            soname, pkg, rel = line.split("\t")
            order.append((soname, os.path.join(tree, pkg, rel)))

    all_map = []
    all_nodes = []
    per_soname = {}
    for want_soname, path in order:
        got, map_rows, node_rows = extract(path)
        if got != want_soname:
            sys.stderr.write(
                "extract-version-map: %s reports soname %s, manifest says "
                "%s\n" % (path, got, want_soname))
            return 3
        all_map.extend(map_rows)
        all_nodes.extend(node_rows)
        per_soname[want_soname] = len(map_rows)

    index = {so: n for n, (so, _p) in enumerate(order)}
    all_map.sort(key=lambda r: (index[r[0]], r[1], r[2], r[3], r[4]))
    all_nodes.sort(key=lambda r: (index[r[0]], r[1]))

    def write(dest, rows):
        text = "".join("\t".join(str(c) for c in r) + "\n" for r in rows)
        if dest == "-":
            sys.stdout.write(text)
        else:
            with open(dest, "w", newline="\n") as fh:
                fh.write(text)

    write(mapout, all_map)
    if nodesout is not None:
        write(nodesout, all_nodes)

    if counts:
        sys.stderr.write("libraries=%d\n" % len(order))
        sys.stderr.write("symbols=%d\n" % len(all_map))
        sys.stderr.write("nodes=%d\n" % len(all_nodes))
        defaults = sum(1 for r in all_map if r[3] == "default")
        hidden = sum(1 for r in all_map if r[3] == "hidden")
        ifuncs = sum(1 for r in all_map if r[4] == "ifunc")
        sys.stderr.write("default=%d\n" % defaults)
        sys.stderr.write("hidden=%d\n" % hidden)
        sys.stderr.write("ifunc=%d\n" % ifuncs)
        for so, _p in order:
            sys.stderr.write("map.%s=%d\n" % (so, per_soname[so]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
