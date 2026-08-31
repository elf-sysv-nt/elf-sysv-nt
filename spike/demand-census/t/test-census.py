"""Tests for census.py: the version compare, the cpio walker, the rpm
container, and the ELF demand reader (the last against a real el8 object
from the cross sysroot, skipped when the sysroot is absent)."""

import gzip
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import census

failures = []


def check(name, cond):
    if not cond:
        failures.append(name)
        print("FAIL %s" % name)
    else:
        print("ok   %s" % name)


def test_rpmvercmp():
    v = census.rpmvercmp
    check("vercmp equal", v("1.2.3", "1.2.3") == 0)
    check("vercmp numeric", v("1.10", "1.9") > 0)
    check("vercmp alpha vs num", v("1.a", "1.1") < 0)
    check("vercmp tilde", v("1~rc1", "1") < 0)
    check("vercmp length", v("1.2.3", "1.2") > 0)
    check("evrcmp epoch", census.evrcmp(("1", "1", "1"), ("0", "9", "9")) > 0)


def newc_entry(name, body, mode=0o100644):
    n = name.encode() + b"\x00"
    hdr = b"070701" + b"".join(
        b"%08X" % x for x in
        [1, mode, 0, 0, 1, 0, len(body), 0, 0, 0, 0, len(n), 0])
    out = hdr + n
    out += b"\x00" * ((4 - len(out) % 4) % 4)
    out += body
    out += b"\x00" * ((4 - len(body) % 4) % 4)
    return out


def make_cpio():
    a = newc_entry("payload/one", b"hello")
    b = newc_entry("payload/dir", b"", mode=0o040755)
    c = newc_entry("payload/two", b"x" * 7)
    t = newc_entry("TRAILER!!!", b"")
    return a + b + c + t


def test_cpio():
    files = list(census.cpio_files(make_cpio()))
    check("cpio count (dirs skipped)", len(files) == 2)
    check("cpio names", [f[0] for f in files]
          == ["payload/one", "payload/two"])
    check("cpio bodies", files[0][1] == b"hello" and files[1][1] == b"x" * 7)


def make_rpm(payload):
    def hdr(nindex, hsize):
        return (b"\x8e\xad\xe8\x01" + b"\x00" * 4
                + struct.pack(">II", nindex, hsize)
                + b"\x00" * (nindex * 16 + hsize))
    lead = b"\xed\xab\xee\xdb" + b"\x00" * 92
    sig = hdr(1, 5)
    pad = b"\x00" * ((8 - (len(lead) + len(sig)) % 8) % 8)
    return lead + sig + pad + hdr(2, 11) + gzip.compress(payload)


def test_rpm_container():
    got = census.rpm_payload(make_rpm(make_cpio()))
    check("rpm payload roundtrip", got == make_cpio())


def test_elf_demand():
    path = os.path.join(os.path.dirname(__file__), "fixture.elf")
    data = open(path, "rb").read()
    demand = set(census.elf_demand(data))
    syms = {s for s, v, f in demand}
    libs = {f for s, v, f in demand}
    check("elf_demand nonempty", len(demand) > 0)
    check("elf_demand sees libc", "libc.so.6" in libs)
    check("elf_demand versions",
          all(v.startswith("GLIBC_") for s, v, f in demand))
    check("elf_demand known symbol",
          syms & {"_exit", "printf", "__printf_chk", "__libc_start_main"})


if __name__ == "__main__":
    test_rpmvercmp()
    test_cpio()
    test_rpm_container()
    test_elf_demand()
    if failures:
        print("%d failure(s)" % len(failures))
        sys.exit(1)
    print("all tests passed")
