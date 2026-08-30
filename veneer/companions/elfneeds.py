#!/usr/bin/env python3
"""Read an ELF64 binary's DT_NEEDED list and verneed table, and check both
against a set of built libraries.

WP-54. The exit criterion is that a vendor binary's `DT_NEEDED` list is
satisfied entirely from our tree with no name left over. This is the reader
that decides it: it walks the binary's dynamic array for the needed names and
its `.gnu.version_r` chain for the (library, node) pairs the binary requires,
then reads each candidate library's DT_SONAME and verdef chain — through
WP-53's provides.py, the reader already certified against the format — and
reports every requirement as satisfied or left over.

Like provides.py, this is written against the file format, not against the
linker that made the file. It walks the section headers itself and does not
shell out to readelf.

Without --tree it prints what it read, one line per fact:

  NEEDED<TAB><name>
  VERNEED<TAB><library><TAB><node>

With --tree LIB... it prints one line per requirement with its verdict and
exits 0 only if nothing is left over.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "libc"))
import provides as P  # noqa: E402

SHT_GNU_VERNEED = 0x6FFFFFFE
DT_NEEDED = 1


def needed(elf):
    """The DT_NEEDED names, in dynamic-array order."""
    sec = elf.find(P.SHT_DYNAMIC)
    link = sec["link"] if sec else 0
    if link == 0 or link >= len(elf.sections):
        raise P.ElfError(".dynamic links to no string table")
    base = elf.sections[link]["offset"]
    out = []
    for tag, val in elf.dynamic():
        if tag == DT_NEEDED:
            out.append(P.cstr(elf.buf, base + val))
    return out


def verneed(elf):
    """[(library name, [node, ...])] in .gnu.version_r order."""
    sec = elf.find(SHT_GNU_VERNEED)
    if sec is None:
        return []
    link = sec["link"]
    if link == 0 or link >= len(elf.sections):
        raise P.ElfError(".gnu.version_r links to no string table")
    strtab = elf.sections[link]["offset"]
    out = []
    off = sec["offset"]
    seen = 0
    while True:
        version, cnt, fileoff, aux, nxt = P.u("<HHIII", elf.buf, off)
        if version != 1:
            raise P.ElfError("verneed revision %d, want 1" % version)
        lib = P.cstr(elf.buf, strtab + fileoff)
        nodes = []
        aoff = off + aux
        for _ in range(cnt):
            _hash, _flags, _other, nameoff, anext = \
                P.u("<IHHII", elf.buf, aoff)
            nodes.append(P.cstr(elf.buf, strtab + nameoff))
            if anext == 0:
                break
            aoff += anext
        out.append((lib, nodes))
        seen += 1
        if nxt == 0:
            break
        if seen > sec["size"]:
            raise P.ElfError("verneed chain does not terminate")
        off += nxt
    if sec["info"] and sec["info"] != seen:
        raise P.ElfError("verneed holds %d entries, sh_info says %d"
                         % (seen, sec["info"]))
    return out


def read_binary(path):
    with open(path, "rb") as fh:
        buf = fh.read()
    return P.Elf64(buf)


def load_tree(paths):
    """soname -> set of non-base verdef node names, for each library file.

    The name a requirement is matched against is the library's base verdef
    node, and build-libc already refuses a file whose DT_SONAME disagrees with
    it; both are read here anyway and their agreement is re-checked, so a file
    that never went through build-libc gets the same trap.
    """
    tree = {}
    for path in paths:
        elf = read_binary(path)
        soname, base, _lines = P.provides(elf)
        if soname != base:
            raise P.ElfError("%s: DT_SONAME %r != base verdef node %r"
                             % (path, soname, base))
        if soname in tree:
            raise P.ElfError("two tree libraries share the soname %r" % soname)
        nodes = {name for name, flags, _p in elf.verdef()
                 if not flags & P.VER_FLG_BASE}
        tree[soname] = nodes
    return tree


def check(elf, tree):
    """[(kind, requirement, verdict)]; verdict is 'satisfied' or 'left over'."""
    out = []
    for name in needed(elf):
        verdict = "satisfied" if name in tree else "left over"
        out.append(("NEEDED", name, verdict))
    for lib, nodes in verneed(elf):
        for node in nodes:
            ok = lib in tree and node in tree[lib]
            out.append(("VERNEED", "%s\t%s" % (lib, node),
                        "satisfied" if ok else "left over"))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("binary")
    ap.add_argument("--tree", nargs="+", metavar="LIB",
                    help="library files the requirements must resolve into")
    args = ap.parse_args()
    try:
        elf = read_binary(args.binary)
        if args.tree is None:
            for name in needed(elf):
                print("NEEDED\t%s" % name)
            for lib, nodes in verneed(elf):
                for node in nodes:
                    print("VERNEED\t%s\t%s" % (lib, node))
            return
        tree = load_tree(args.tree)
        results = check(elf, tree)
    except P.ElfError as exc:
        sys.exit("%s: %s" % (args.binary, exc))
    left = 0
    for kind, req, verdict in results:
        print("%s\t%s\t%s" % (kind, req, verdict))
        if verdict != "satisfied":
            left += 1
    if not results:
        sys.exit("%s: no DT_NEEDED at all; nothing was checked" % args.binary)
    if left:
        sys.exit("%s: %d requirements left over" % (args.binary, left))


if __name__ == "__main__":
    main()
