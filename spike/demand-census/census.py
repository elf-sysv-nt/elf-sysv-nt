#!/usr/bin/env python3
"""The demand census (spike 12).

Reads every binary package in the el8 set, records which glibc symbols each
one imports, and sorts that demand against the WP-52 classification. See
README.md for what the numbers decide.

Subcommands:
  enumerate  fetch repodata, write the worklist (one newest build per name)
  probe      read one .rpm (path or URL), print its symbol demand as TSV
  run        stream the worklist: fetch, probe, keep the fragment, delete
  report     aggregate fragments against the classification
"""

import argparse
import gzip
import io
import lzma
import os
import struct
import sys
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor

MIRROR = "https://dl.rockylinux.org/pub/rocky/8.10"
REPOS = ["BaseOS", "AppStream", "PowerTools", "extras"]
MD_NS = "{http://linux.duke.edu/metadata/common}"
REPO_NS = "{http://linux.duke.edu/metadata/repo}"


# ---- rpm version comparison (enough of rpmvercmp to pick the newest) ----

def _segments(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c.isdigit():
            j = i
            while j < len(s) and s[j].isdigit():
                j += 1
            out.append((1, int(s[i:j])))
            i = j
        elif c.isalpha():
            j = i
            while j < len(s) and s[j].isalpha():
                j += 1
            out.append((0, s[i:j]))
            i = j
        elif c == "~":
            out.append((-1, ""))
            i += 1
        else:
            i += 1
    return out


def rpmvercmp(a, b):
    sa, sb = _segments(a), _segments(b)
    for x, y in zip(sa, sb):
        if x != y:
            return -1 if x < y else 1
    if len(sa) > len(sb):
        return -1 if sa[len(sb)][0] == -1 else 1
    if len(sb) > len(sa):
        return 1 if sb[len(sa)][0] == -1 else -1
    return 0


def evrcmp(a, b):
    for x, y in zip(a, b):
        c = rpmvercmp(x, y)
        if c:
            return c
    return 0


# ---- the rpm container: lead, two headers, compressed cpio ----

def rpm_payload(data):
    """Return the decompressed cpio payload of one .rpm as bytes."""
    if data[:4] != b"\xed\xab\xee\xdb":
        raise ValueError("not an rpm")
    off = 96
    for align in (8, 0):
        if data[off:off + 3] != b"\x8e\xad\xe8":
            raise ValueError("bad header magic at %d" % off)
        nindex, hsize = struct.unpack(">II", data[off + 8:off + 16])
        off += 16 + nindex * 16 + hsize
        if align:
            off += (align - off % align) % align
    blob = data[off:]
    if blob[:6] == b"\xfd7zXZ\x00":
        return lzma.decompress(blob)
    if blob[:2] == b"\x1f\x8b":
        return gzip.decompress(blob)
    if blob[:4] == b"BZh9"[:4] or blob[:3] == b"BZh":
        import bz2
        return bz2.decompress(blob)
    if blob[:6] == b"070701":
        return blob
    raise ValueError("unknown payload compression %r" % blob[:6])


def cpio_files(payload):
    """Yield (name, bytes) for each regular file in a newc cpio archive."""
    off = 0
    while True:
        if payload[off:off + 6] != b"070701":
            raise ValueError("bad cpio magic at %d" % off)
        f = [int(payload[off + 6 + i * 8:off + 14 + i * 8], 16) for i in range(13)]
        namesize, filesize, mode = f[11], f[6], f[1]
        name = payload[off + 110:off + 110 + namesize - 1].decode("utf-8", "replace")
        off += 110 + namesize
        off += (4 - off % 4) % 4
        if name == "TRAILER!!!":
            return
        body = payload[off:off + filesize]
        off += filesize
        off += (4 - off % 4) % 4
        if (mode & 0o170000) == 0o100000:
            yield name, body


# ---- ELF: the versioned undefined symbols of one 64-bit object ----

def elf_demand(data):
    """Yield (symbol, version, needed-file) for each versioned UND symbol."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        return
    shoff, = struct.unpack("<Q", data[0x28:0x30])
    shentsize, shnum = struct.unpack("<HH", data[0x3a:0x3e])
    if shoff == 0 or shnum == 0 or shoff + shnum * shentsize > len(data):
        return
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        name, typ = struct.unpack("<II", data[o:o + 8])
        offset, size = struct.unpack("<QQ", data[o + 24:o + 40])
        link, info = struct.unpack("<II", data[o + 40:o + 48])
        entsize, = struct.unpack("<Q", data[o + 56:o + 64])
        secs.append((typ, offset, size, link, info, entsize))
    dynsym = versym = verneed = None
    for s in secs:
        if s[0] == 11:
            dynsym = s
        elif s[0] == 0x6FFFFFFF:
            versym = s
        elif s[0] == 0x6FFFFFFE:
            verneed = s
    if not (dynsym and versym and verneed):
        return
    strtab = secs[dynsym[3]]
    vstr = secs[verneed[3]]

    def cstr(tab, off):
        end = data.index(b"\x00", tab[1] + off)
        return data[tab[1] + off:end].decode("latin-1")

    vmap, off = {}, verneed[1]
    for _ in range(verneed[4]):
        cnt, fileoff, aux, nxt = struct.unpack("<HIII", data[off + 2:off + 16])
        fname, ao = cstr(vstr, fileoff), off + aux
        for _ in range(cnt):
            nameoff, anxt = struct.unpack("<II", data[ao + 8:ao + 16])
            other, = struct.unpack("<H", data[ao + 6:ao + 8])
            vmap[other] = (cstr(vstr, nameoff), fname)
            ao += anxt
        off += nxt
    nsyms = dynsym[2] // 24
    for i in range(nsyms):
        o = dynsym[1] + i * 24
        nameoff, = struct.unpack("<I", data[o:o + 4])
        shndx, = struct.unpack("<H", data[o + 6:o + 8])
        if shndx != 0 or nameoff == 0:
            continue
        vs, = struct.unpack("<H", data[versym[1] + i * 2:versym[1] + i * 2 + 2])
        vs &= 0x7FFF
        if vs < 2 or vs not in vmap:
            continue
        ver, fname = vmap[vs]
        yield cstr(strtab, nameoff), ver, fname


# ---- one package's demand ----

GLIBC_SONAMES = {
    "libc.so.6", "libm.so.6", "libpthread.so.0", "librt.so.1", "libdl.so.2",
    "libresolv.so.2", "libcrypt.so.1", "libutil.so.1", "libnsl.so.1",
    "ld-linux-x86-64.so.2",
}


def rpm_demand(data):
    """The set of (soname, symbol, version) one rpm's ELF objects import."""
    out = set()
    for _name, body in cpio_files(rpm_payload(data)):
        if body[:4] != b"\x7fELF":
            continue
        for sym, ver, fname in elf_demand(body):
            if fname in GLIBC_SONAMES:
                out.add((fname, sym, ver))
    return out


def fetch(url, tries=4):
    last = None
    for i in range(tries):
        try:
            with urllib.request.urlopen(url, timeout=120) as r:
                return r.read()
        except Exception as e:
            last = e
    raise last


# ---- enumerate: repodata to worklist ----

def repo_primary_url(repo):
    base = "%s/%s/x86_64/os" % (MIRROR, repo)
    root = ET.fromstring(fetch(base + "/repodata/repomd.xml"))
    for d in root.findall(REPO_NS + "data"):
        if d.get("type") == "primary":
            href = d.find(REPO_NS + "location").get("href")
            return base + "/" + href
    raise ValueError("no primary in %s" % repo)


def enumerate_repos(out_path):
    best = {}
    for repo in REPOS:
        base = "%s/%s/x86_64/os" % (MIRROR, repo)
        xml = gzip.decompress(fetch(repo_primary_url(repo)))
        for _ev, el in ET.iterparse(io.BytesIO(xml)):
            if el.tag != MD_NS + "package":
                continue
            arch = el.findtext(MD_NS + "arch")
            if arch != "x86_64":
                el.clear()
                continue
            name = el.findtext(MD_NS + "name")
            v = el.find(MD_NS + "version")
            evr = (v.get("epoch") or "0", v.get("ver"), v.get("rel"))
            href = el.find(MD_NS + "location").get("href")
            size = el.find(MD_NS + "size").get("package")
            cur = best.get(name)
            if cur is None or evrcmp(evr, cur[0]) > 0:
                best[name] = (evr, base + "/" + href, size)
            el.clear()
    with open(out_path, "w") as f:
        for name in sorted(best):
            evr, url, size = best[name]
            f.write("%s\t%s\t%s\n" % (name, url, size))
    return len(best)


# ---- run: stream the worklist, keep only the fragments ----

def process_one(name, url, fragdir, donedir):
    marker = os.path.join(donedir, name + ".done")
    if os.path.exists(marker):
        return "skip"
    try:
        demand = rpm_demand(fetch(url))
    except Exception as e:
        with open(os.path.join(donedir, name + ".err"), "w") as f:
            f.write("%s\n%s\n" % (url, e))
        return "err"
    tmp = os.path.join(fragdir, name + ".tmp")
    with open(tmp, "w") as f:
        for soname, sym, ver in sorted(demand):
            f.write("%s\t%s\t%s\n" % (soname, sym, ver))
    os.replace(tmp, os.path.join(fragdir, name + ".tsv"))
    with open(marker, "w") as f:
        f.write(url + "\n")
    return "ok"


def run(worklist, root, jobs):
    fragdir = os.path.join(root, "frag")
    donedir = os.path.join(root, "done")
    os.makedirs(fragdir, exist_ok=True)
    os.makedirs(donedir, exist_ok=True)
    work = [l.rstrip("\n").split("\t") for l in open(worklist) if l.strip()]
    counts = {"ok": 0, "skip": 0, "err": 0}
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = [ex.submit(process_one, w[0], w[1], fragdir, donedir) for w in work]
        for i, fu in enumerate(futs):
            counts[fu.result()] += 1
            if (i + 1) % 50 == 0 or i + 1 == len(futs):
                print("%d/%d ok=%d skip=%d err=%d"
                      % (i + 1, len(futs), counts["ok"], counts["skip"],
                         counts["err"]), flush=True)
    return counts


# ---- report: fragments against the WP-52 classification ----

def load_classification(path):
    buckets = {}
    for line in open(path, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 4:
            buckets[(f[0], f[1], f[2])] = f[3]
    return buckets


def report(root, classification, out):
    buckets = load_classification(classification)
    fragdir = os.path.join(root, "frag")
    per_pkg = {}
    for fn in sorted(os.listdir(fragdir)):
        if not fn.endswith(".tsv"):
            continue
        pkg = fn[:-4]
        per_pkg[pkg] = set(
            tuple(l.rstrip("\n").split("\t"))
            for l in open(fragdir + "/" + fn) if l.strip())
    demand, pkg_worst = {}, {}
    for pkg, bindings in per_pkg.items():
        worst = "0"
        for b in bindings:
            demand[b] = demand.get(b, 0) + 1
            bk = buckets.get(b, "unmapped")
            if bk in "1234" and bk > worst:
                worst = bk
            elif bk == "unmapped":
                worst = max(worst, "u")
        pkg_worst[pkg] = worst
    total = len(per_pkg)
    users = sum(1 for w in pkg_worst.values() if w != "0")
    b4 = sum(1 for w in pkg_worst.values() if w in ("4", "u"))
    with open(os.path.join(root, "demand-ranking.tsv"), "w") as f:
        for b, n in sorted(demand.items(), key=lambda kv: (-kv[1], kv[0])):
            f.write("%s\t%s\t%s\t%d\t%s\n"
                    % (b[0], b[1], b[2], n, buckets.get(b, "unmapped")))
    small = sorted(
        (p for p, w in pkg_worst.items() if w in "0123"),
        key=lambda p: len(per_pkg[p]))
    with open(out, "w") as f:
        f.write("packages=%d\n" % total)
        f.write("packages_with_glibc_demand=%d\n" % users)
        f.write("packages_touching_bucket4=%d\n" % b4)
        f.write("bucket4_share_of_all=%.1f%%\n" % (100.0 * b4 / max(total, 1)))
        f.write("bucket4_share_of_users=%.1f%%\n" % (100.0 * b4 / max(users, 1)))
        f.write("distinct_bindings=%d\n" % len(demand))
        f.write("# small clean candidates (all bindings in buckets 1-3),\n")
        f.write("# by distinct-binding count ascending:\n")
        for p in [q for q in small if per_pkg[q]][:25]:
            f.write("candidate=%s bindings=%d\n" % (p, len(per_pkg[p])))
    return out


# ---- entry ----

def main(argv):
    ap = argparse.ArgumentParser(prog="census")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("enumerate")
    p.add_argument("-o", default="worklist.tsv")
    p = sub.add_parser("probe")
    p.add_argument("rpm")
    p = sub.add_parser("run")
    p.add_argument("--worklist", required=True)
    p.add_argument("--root", required=True)
    p.add_argument("--jobs", type=int, default=4)
    p = sub.add_parser("report")
    p.add_argument("--root", required=True)
    p.add_argument("--classification", required=True)
    p.add_argument("-o", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "enumerate":
        n = enumerate_repos(a.o)
        print("worklist: %d names -> %s" % (n, a.o))
    elif a.cmd == "probe":
        if a.rpm.startswith("http"):
            data = fetch(a.rpm)
        else:
            data = open(a.rpm, "rb").read()
        for soname, sym, ver in sorted(rpm_demand(data)):
            print("%s\t%s\t%s" % (soname, sym, ver))
    elif a.cmd == "run":
        c = run(a.worklist, a.root, a.jobs)
        print("done ok=%(ok)d skip=%(skip)d err=%(err)d" % c)
    elif a.cmd == "report":
        print("wrote %s" % report(a.root, a.classification, a.o))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
