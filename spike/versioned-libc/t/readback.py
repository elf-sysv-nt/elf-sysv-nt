#!/usr/bin/env python3
"""Read an ELF file back and print what elfdeps would look at.

Usage: readback.py FILE

This is deliberately not synth-libc.py's code. An emitter checked against its
own idea of the format agrees with itself no matter where it wrote a field, so
the offsets here are spelled out again from the gABI.
"""
import struct
import sys

SHT = {5: "hash", 0x6FFFFFF6: "gnu.hash", 6: "dynamic", 11: "dynsym",
       0x6FFFFFFD: "verdef", 0x6FFFFFFE: "verneed", 0x6FFFFFFF: "versym"}
DT = {1: "NEEDED", 4: "HASH", 5: "STRTAB", 6: "SYMTAB", 10: "STRSZ",
      11: "SYMENT", 14: "SONAME", 0x6FFFFEF5: "GNU_HASH", 0x6FFFFFF0: "VERSYM",
      0x6FFFFFFC: "VERDEF", 0x6FFFFFFD: "VERDEFNUM", 0x6FFFFFFE: "VERNEED",
      0x6FFFFFFF: "VERNEEDNUM", 0: "NULL"}


def cstr(blob, off):
    end = blob.index(b"\0", off)
    return blob[off:end].decode()


def main(argv):
    blob = open(argv[1], "rb").read()
    if blob[:4] != b"\x7fELF":
        raise SystemExit("not an ELF file")
    etype, = struct.unpack_from("<H", blob, 16)
    shoff, = struct.unpack_from("<Q", blob, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", blob, 0x3A)

    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        name, stype, flags, addr, off, size, link, info = struct.unpack_from(
            "<IIQQQQII", blob, o)
        secs.append(dict(name=name, type=stype, off=off, size=size, link=link,
                         info=info))
    shstr = blob[secs[shstrndx]["off"]:
                 secs[shstrndx]["off"] + secs[shstrndx]["size"]]
    for s in secs:
        s["sname"] = cstr(shstr, s["name"])

    print("type=%s" % {2: "ET_EXEC", 3: "ET_DYN"}.get(etype, etype))
    by = {}
    for s in secs:
        if s["type"] in SHT:
            by.setdefault(SHT[s["type"]], s)

    dyn = by.get("dynamic")
    if dyn:
        dstr = secs[dyn["link"]]
        strblob = blob[dstr["off"]:dstr["off"] + dstr["size"]]
        tags, needed, soname = [], [], None
        for i in range(dyn["size"] // 16):
            tag, val = struct.unpack_from("<qQ", blob, dyn["off"] + i * 16)
            tags.append(DT.get(tag, hex(tag)))
            if tag == 14:
                soname = cstr(strblob, val)
            if tag == 1:
                needed.append(cstr(strblob, val))
        print("dynamic=%s" % ",".join(tags))
        print("soname=%s" % (soname if soname else ""))
        print("needed=%s" % ",".join(needed))

    vd = by.get("verdef")
    if vd:
        dstr = secs[vd["link"]]
        strblob = blob[dstr["off"]:dstr["off"] + dstr["size"]]
        base, nodes, off = "", [], vd["off"]
        for _ in range(vd["info"]):
            ver, flags, ndx, cnt, vhash, aux, nxt = struct.unpack_from(
                "<HHHHIII", blob, off)
            a = off + aux
            names = []
            for _ in range(cnt):
                vda_name, vda_next = struct.unpack_from("<II", blob, a)
                names.append(cstr(strblob, vda_name))
                a += vda_next if vda_next else 0
            if flags & 1:
                base = names[0]
            else:
                nodes += names
            if not nxt:
                break
            off += nxt
        print("verdef_count=%d" % vd["info"])
        print("verdef_base=%s" % base)
        print("verdef_nodes=%s" % ",".join(nodes))

    vn = by.get("verneed")
    if vn:
        dstr = secs[vn["link"]]
        strblob = blob[dstr["off"]:dstr["off"] + dstr["size"]]
        off = vn["off"]
        for _ in range(vn["info"]):
            ver, cnt, vfile, aux, nxt = struct.unpack_from("<HHIII", blob, off)
            a = off + aux
            names = []
            for _ in range(cnt):
                h, flags, other, name, nxt_a = struct.unpack_from(
                    "<IHHII", blob, a)
                names.append(cstr(strblob, name))
                a += nxt_a if nxt_a else 0
            print("verneed_file=%s" % cstr(strblob, vfile))
            print("verneed_nodes=%s" % ",".join(names))
            if not nxt:
                break
            off += nxt

    vs = by.get("versym")
    if vs:
        n = vs["size"] // 2
        vals = struct.unpack_from("<%dH" % n, blob, vs["off"])
        print("versym=%s" % ",".join(str(v) for v in vals))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
