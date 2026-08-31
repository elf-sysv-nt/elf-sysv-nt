#!/usr/bin/env python3
"""Cut WP-56's slices from the demand census.

Two subcommands:

  map    scan the el8 headers with the compiler's -aux-info and write
         symbol-slice.tsv: each declared function attributed to the
         first header in slices.tsv that declares it, and so to that
         header's slice.
  order  join the census demand-ranking.tsv against symbol-slice.tsv
         and write the slice order plus one worklist per slice.

The header scan is mechanical: for each header named in slices.tsv a
one-line translation unit is compiled with -aux-info, and every function
the unit declares is credited to that header.  Earlier rows in
slices.tsv win, so the table's order is the attribution priority.
Symbols the scan never sees fall into the 'unassigned' slice, which
keeps the gap visible instead of silently dropped.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DECL_RE = re.compile(r"([A-Za-z_]\w*)\s*\(")


def read_slices(path):
    rows = []
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        header, slice_ = line.split("\t")
        rows.append((header, slice_))
    return rows


def aux_symbols(cc, incdir, header):
    """Declared function names for one header, via -aux-info."""
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "t.c")
        aux = os.path.join(td, "t.aux")
        with open(src, "w") as f:
            f.write("#include <%s>\n" % header)
        r = subprocess.run(
            [cc, "-fsyntax-only", "-aux-info", aux, "-I", incdir, src],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode != 0 or not os.path.exists(aux):
            return None
        out = {}
        for line in open(aux, encoding="utf-8", errors="replace"):
            if "*/" not in line or "/*" not in line:
                continue
            head, decl = line.split("*/", 1)
            src = head.split("/*", 1)[1].strip().rsplit(":", 2)[0]
            m = DECL_RE.search(decl)
            if m:
                out.setdefault(m.group(1), src.replace("\\", "/"))
        return out


def cmd_map(args):
    rows = read_slices(args.slices)
    table = dict(rows)
    seen = {}
    skipped = []
    for header, slice_ in rows:
        syms = aux_symbols(args.cc, args.include, header)
        if syms is None:
            skipped.append(header)
            continue
        for s in sorted(syms):
            # credit the header that actually declared the symbol when
            # it is one the table names; the scanned header is only the
            # fallback (bits/ internals and the like)
            src = syms[s]
            owner = next((h for h in table
                          if src.endswith("/" + h) or src == h), None)
            if owner is not None:
                seen.setdefault(s, (table[owner], owner))
            else:
                seen.setdefault(s, (slice_, header))
    with open(args.output, "w", encoding="utf-8") as f:
        for s in sorted(seen):
            slice_, header = seen[s]
            f.write("%s\t%s\t%s\n" % (s, slice_, header))
    print("mapped %d symbols from %d headers -> %s"
          % (len(seen), len(rows) - len(skipped), args.output))
    for h in skipped:
        print("skipped (does not compile alone): %s" % h, file=sys.stderr)
    return 0


def cmd_order(args):
    symslice = {}
    for line in open(args.map, encoding="utf-8"):
        f_ = line.rstrip("\n").split("\t")
        if len(f_) >= 2:
            symslice[f_[0]] = f_[1]
    slices = {}
    for line in open(args.ranking, encoding="utf-8"):
        f_ = line.rstrip("\n").split("\t")
        if len(f_) < 5 or f_[0] != args.soname:
            continue
        sym, ver, count, bucket = f_[1], f_[2], int(f_[3]), f_[4]
        sl = symslice.get(sym, "unassigned")
        slices.setdefault(sl, []).append((count, sym, ver, bucket))
    os.makedirs(args.out, exist_ok=True)
    order = sorted(slices.items(),
                   key=lambda kv: (-sum(c for c, *_ in kv[1]), kv[0]))
    with open(os.path.join(args.out, "slice-order.tsv"), "w") as f:
        for sl, entries in order:
            f.write("%s\t%d\t%d\n"
                    % (sl, len(entries), sum(c for c, *_ in entries)))
    for sl, entries in order:
        with open(os.path.join(args.out, "slice-%s.tsv" % sl), "w") as f:
            for count, sym, ver, bucket in sorted(
                    entries, key=lambda e: (-e[0], e[1])):
                f.write("%s\t%s\t%d\t%s\n" % (sym, ver, count, bucket))
    print("wrote %d slices -> %s" % (len(order), args.out))
    return 0


def main(argv):
    p = argparse.ArgumentParser(
        description="cut WP-56's slices from the demand census")
    sub = p.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("map", help="scan headers, write symbol-slice.tsv")
    m.add_argument("--slices", default=os.path.join(HERE, "slices.tsv"))
    m.add_argument("--include",
                   default=os.path.normpath(os.path.join(HERE, "..", "include")))
    m.add_argument("--cc", default="x86_64-elfsysvnt-linux-gnu-gcc")
    m.add_argument("-o", "--output",
                   default=os.path.join(HERE, "symbol-slice.tsv"))
    m.set_defaults(fn=cmd_map)
    o = sub.add_parser("order", help="rank slices from demand-ranking.tsv")
    o.add_argument("--map", default=os.path.join(HERE, "symbol-slice.tsv"))
    o.add_argument("--ranking", required=True)
    o.add_argument("--soname", default="libc.so.6")
    o.add_argument("--out", required=True)
    o.set_defaults(fn=cmd_order)
    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
