#!/usr/bin/env python3
"""Which el8 packages carry a raw syscall instruction, or a Go runtime, in
their shipped text?

Substrate N rebuilds the userland with a toolchain that emits a gate call
where the compiler would emit `syscall`, and a `%gs` thread pointer where it
would emit `%fs`. A post-link check (0011 § 16) then refuses any image that
still carries an `0f 05` in executable text. What the rebuild cannot fix is
what the compiler never emitted: `syscall` in hand-written assembly, and the
runtimes (Go first among them) that issue their own. This counts those in the
shipped x86_64 packages, per package, so the size of N's userland is a list
rather than a recollection.

The `%fs` half of the post-link check is not censused here on purpose. Every
el8 binary reads `%fs:0x28` for the stack protector and `%fs:<tpoff>` for
initial-exec TLS, both of which the rebuild changes; a hand-written `%fs`
access in shipped text looks the same as a compiled one, and a count would
be a count of packages built with -fstack-protector, which is all of them.
The Go marker (a `.gopclntab` or `.go.buildinfo` section) is the one
runtime known to hand-write `%fs`, and it is reported.

Usage:
  census.py run   --worklist FILE --root DIR [--jobs N] [--filter]
  census.py report --root DIR [--objdump PATH] [--refdir DIR]

`run` streams every package in the worklist (the demand census's, reused:
name, url, size), extracts each 64-bit ELF from the rpm in memory, and
counts `0f 05` in PF_X PT_LOAD segments. A package with no hit writes a
one-line fragment; a package with hits writes the hit files' names and
counts. Done markers make it resumable; errors are recorded, not fatal.
`--filter` skips -devel, -doc, -docs, -javadoc, -debuginfo, -debugsource,
-static and -headers names, which carry no runnable text.

`report` reads the fragments and, with an objdump, confirms each package's
byte-scan hits by disassembly: the mnemonic count on the extracted file is
the exact number, the byte count the upper bound. It prints the transcript
body measure.sh wraps.
"""
import io
import os
import struct
import subprocess
import sys
import tempfile
import threading
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "demand-census"))
from census import rpm_payload, cpio_files, fetch  # noqa: E402

SKIP_SUFFIXES = ("-devel", "-doc", "-docs", "-javadoc", "-debuginfo",
                 "-debugsource", "-static", "-headers", "-tests", "-test")
GO_SECTIONS = (b".gopclntab", b".go.buildinfo")


def elf_scan(data):
    """(text_bytes, syscall_hits, go_marker) for one 64-bit little-endian ELF."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        return None
    if len(data) < 64:
        return None
    phoff, shoff = struct.unpack("<QQ", data[0x20:0x30])
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack("<HHHHH", data[0x36:0x40])
    text, hits = 0, 0
    for i in range(phnum):
        o = phoff + i * phentsize
        if o + 56 > len(data):
            break
        ptype, pflags = struct.unpack("<II", data[o:o + 8])
        offset, = struct.unpack("<Q", data[o + 8:o + 16])
        filesz, = struct.unpack("<Q", data[o + 32:o + 40])
        if ptype != 1 or not (pflags & 1):
            continue
        seg = data[offset:offset + filesz]
        text += len(seg)
        hits += seg.count(b"\x0f\x05")
    go = False
    if shoff and shnum and shstrndx < shnum and shoff + shnum * shentsize <= len(data):
        so = shoff + shstrndx * shentsize
        stroff, strsize = struct.unpack("<QQ", data[so + 24:so + 40])
        names = data[stroff:stroff + strsize]
        for i in range(shnum):
            o = shoff + i * shentsize
            nameidx, = struct.unpack("<I", data[o:o + 4])
            end = names.find(b"\x00", nameidx)
            nm = names[nameidx:end if end >= 0 else None]
            if nm in GO_SECTIONS:
                go = True
                break
    return text, hits, go


def process_one(name, url, fragdir, donedir, refdir):
    marker = os.path.join(donedir, name + ".done")
    if os.path.exists(marker):
        return "skip"
    try:
        blob = fetch(url)
        rows = []
        for fname, body in cpio_files(rpm_payload(blob)):
            r = elf_scan(body)
            if r is None:
                continue
            text, hits, go = r
            rows.append((fname, text, hits, go))
            if (hits or go) and refdir:
                # keep the file for the report's disassembly confirmation
                dst = os.path.join(refdir, name, fname.lstrip("./").replace("/", "__"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "wb") as f:
                    f.write(body)
        tmp = os.path.join(fragdir, "%s.%d.tmp" % (name, threading.get_ident()))
        with open(tmp, "w") as f:
            for fname, text, hits, go in rows:
                f.write("%s\t%d\t%d\t%d\n" % (fname, text, hits, 1 if go else 0))
        dst = os.path.join(fragdir, name + ".tsv")
        try:
            os.replace(tmp, dst)
        except OSError:
            time.sleep(1)
            os.replace(tmp, dst)
        with open(marker, "w") as f:
            f.write(url + "\n")
    except Exception as e:
        with open(os.path.join(donedir, name + ".err"), "w") as f:
            f.write("%s\n%s\n" % (url, e))
        return "err"
    return "ok"


def run(worklist, root, jobs, filt):
    fragdir, donedir, refdir = (os.path.join(root, d) for d in ("frag", "done", "ref"))
    for d in (fragdir, donedir, refdir):
        os.makedirs(d, exist_ok=True)
    work = [l.rstrip("\n").split("\t") for l in open(worklist) if l.strip()]
    if filt:
        work = [w for w in work if not w[0].endswith(SKIP_SUFFIXES)]
    counts = {"ok": 0, "skip": 0, "err": 0}
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = [ex.submit(process_one, w[0], w[1], fragdir, donedir, refdir) for w in work]
        for i, fu in enumerate(futs):
            counts[fu.result()] += 1
            if (i + 1) % 50 == 0 or i + 1 == len(futs):
                print("%d/%d ok=%d skip=%d err=%d" % (i + 1, len(futs), counts["ok"],
                                                       counts["skip"], counts["err"]), flush=True)
    return counts


def confirm(objdump, path):
    """The exact count of syscall instructions objdump finds in a file.

    Counted as objdump writes, a line at a time. Holding the disassembly in a
    string and splitting it costs twice the text, and the text of a large Go
    binary runs to gigabytes -- the run of 2026-09-06 was killed here, with
    3737 packages already scanned and nothing to show for them.
    """
    n = 0
    deadline = time.time() + 600
    try:
        p = subprocess.Popen([objdump, "-d", "--no-show-raw-insn", path],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             text=True, errors="replace", bufsize=1 << 20)
    except Exception:
        return -1
    try:
        seen = 0
        for l in p.stdout:
            l = l.rstrip()
            if l.endswith("\tsyscall") or l.endswith(" syscall"):
                n += 1
            seen += 1
            if not seen & 0xfffff and time.time() > deadline:
                raise TimeoutError(path)
    except Exception:
        p.kill()
        p.stdout.close()
        p.wait()
        return -1
    p.stdout.close()
    p.wait()
    return n


def report(root, objdump):
    fragdir, donedir, refdir = (os.path.join(root, d) for d in ("frag", "done", "ref"))
    pkgs = {}
    for fn in sorted(os.listdir(fragdir)):
        if not fn.endswith(".tsv"):
            continue
        name = fn[:-4]
        rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(fragdir, fn)) if l.strip()]
        pkgs[name] = [(r[0], int(r[1]), int(r[2]), r[3] == "1") for r in rows]
    errs = sorted(f[:-4] for f in os.listdir(donedir) if f.endswith(".err"))
    scanned = len(pkgs)
    with_elf = sum(1 for p in pkgs.values() if p)
    elf_files = sum(len(p) for p in pkgs.values())
    byte_hit_pkgs = sorted(n for n, p in pkgs.items() if any(r[2] for r in p))
    go_pkgs = sorted(n for n, p in pkgs.items() if any(r[3] for r in p))

    confirmed = {}
    if objdump and os.path.isdir(refdir):
        for name in byte_hit_pkgs:
            d = os.path.join(refdir, name)
            if not os.path.isdir(d):
                continue
            total = 0
            for f in sorted(os.listdir(d)):
                c = confirm(objdump, os.path.join(d, f))
                if c > 0:
                    total += c
            confirmed[name] = total
    exact_pkgs = sorted(n for n, c in confirmed.items() if c > 0)
    glibc_like = {"glibc", "glibc-common", "glibc-langpack-en", "glibc-utils", "glibc-all-langpacks",
                  "glibc-minimal-langpack", "glibc-locale-source", "glibc-gconv-extra", "nss_db", "nss_nis", "nss_hesiod"}
    outside_glibc = [n for n in exact_pkgs if n not in glibc_like]

    print("packages_scanned=%d" % scanned)
    print("packages_with_elf=%d" % with_elf)
    print("elf_files_scanned=%d" % elf_files)
    print("packages_errored=%d" % len(errs))
    print("packages_with_0f05_bytes=%d" % len(byte_hit_pkgs))
    print("packages_confirmed_syscall=%d" % len(exact_pkgs))
    print("packages_confirmed_syscall_outside_glibc=%d" % len(outside_glibc))
    print("packages_go_runtime=%d" % len(go_pkgs))
    both = sorted(set(outside_glibc) | set(go_pkgs))
    print("packages_n_cannot_rebuild_as_shipped=%d" % len(both))
    print("")
    print("confirmed syscall instructions outside glibc, by package (count, go?):")
    for n in outside_glibc:
        print("    %s\t%d\t%s" % (n, confirmed[n], "go" if n in go_pkgs else ""))
    print("")
    print("go runtime without a confirmed syscall (static section marker only):")
    for n in go_pkgs:
        if n not in outside_glibc:
            print("    %s" % n)
    print("")
    print("glibc's own:")
    for n in exact_pkgs:
        if n in glibc_like:
            print("    %s\t%d" % (n, confirmed[n]))
    if errs:
        print("")
        print("errored packages:")
        for n in errs:
            print("    %s" % n)


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    cmd, args = argv[1], argv[2:]
    opt = {"--jobs": "6"}
    flags = set()
    i = 0
    while i < len(args):
        if args[i] in ("--filter",):
            flags.add(args[i]); i += 1
        elif args[i].startswith("--") and i + 1 < len(args):
            opt[args[i]] = args[i + 1]; i += 2
        else:
            sys.exit("bad argument %r" % args[i])
    if cmd == "run":
        c = run(opt["--worklist"], opt["--root"], int(opt["--jobs"]), "--filter" in flags)
        print("done: %r" % c)
    elif cmd == "report":
        report(opt["--root"], opt.get("--objdump"))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
