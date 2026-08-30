#!/usr/bin/env python3
"""Sort every symbol in the WP-51 version map into one of four resolution
buckets against the WP-20 runtime export surface.

The map (veneer/version-map/glibc-version-map.tsv) is the domain; the runtime
surface (runtime/exports/cygwin-exports.tsv) is what stands behind the veneer.
Each map row is classified by name and availability into:

  1 forward-alias  glibc symbol resolves to a runtime export under another name
  2 forward-same   glibc symbol resolves to a runtime export of the same name
  3 shim           a runtime export exists but the semantics differ; needs a
                   translating wrapper -- flagged 'review', never asserted final
  4 stub           nothing behind it; becomes a stub that fails predictably

Version-node identity objects (a .dynsym entry whose name is a version string
such as GLIBC_2.14) are versioning scaffold WP-53 regenerates from the node
ladder, not an API provide; they carry the disposition 'scaffold' so a version
anchor is never mis-filed as a failing stub. See DR-0017.

This is a first pass by name and availability. The mechanical rules decide
buckets 1, 2 and 4 and the compiler-helper corner of 3; the semantic calls of
bucket 3 that no name match can make are read from semantic-review.tsv, a
curated queue where every entry is flagged for a human to confirm.
"""
import argparse, collections, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, os.pardir, os.pardir))
MAP_DEFAULT = os.path.join(ROOT, "veneer", "version-map", "glibc-version-map.tsv")
CYG_DEFAULT = os.path.join(ROOT, "runtime", "exports", "cygwin-exports.tsv")
REVIEW_DEFAULT = os.path.join(HERE, "semantic-review.tsv")

VNODE = re.compile(r"^(GLIBC_|XCRYPT_)")

DISPOSITION = {
    "1": "forward-alias",
    "2": "forward-same",
    "3": "shim",
    "4": "stub",
    "scaffold": "scaffold",
}


def load_map(path):
    rows = []
    for ln in open(path):
        f = ln.rstrip("\n").split("\t")
        if len(f) < 6:
            raise SystemExit("map row has %d fields, expected 6: %r" % (len(f), ln))
        rows.append(f)  # soname symbol version binding type bind
    return rows


def load_cyg(path):
    names = set()
    for ln in open(path):
        f = ln.rstrip("\n").split("\t")
        names.add(f[0])
    return names


def load_review(path):
    out = {}
    if not os.path.exists(path):
        return out
    for ln in open(path):
        ln = ln.rstrip("\n")
        if not ln or ln.startswith("#"):
            continue
        f = ln.split("\t")
        if len(f) != 2:
            raise SystemExit("review row must be 'symbol<TAB>reason': %r" % ln)
        out[f[0]] = f[1]
    return out


def alias_target(sym, cyg):
    """Reserved-name aliases: the runtime export the veneer forwards to."""
    if sym.startswith("__") and sym[2:] in cyg:
        return sym[2:]
    if sym.startswith("_") and not sym.startswith("__") and sym[1:] in cyg:
        return sym[1:]
    if ("_" + sym) in cyg:
        return "_" + sym
    if ("__" + sym) in cyg:
        return "__" + sym
    return None


def lfs_base(sym, cyg):
    """Large-file 64-bit variant that on LP64 aliases its base function."""
    if sym.endswith("64") and not sym.endswith("_64"):
        base = sym[:-2]
        if base in cyg:
            return base
        if base.startswith("__") and base[2:] in cyg:
            return base[2:]
    return None


def chk_base(sym, cyg):
    if sym.endswith("_chk"):
        base = sym[2:-4] if sym.startswith("__") else sym[:-4]
        return base, (base in cyg)
    return None, False


def finite_base(sym, cyg):
    if sym.startswith("__") and sym.endswith("_finite"):
        base = sym[2:-7]
        return base, (base in cyg)
    return None, False


def classify(sym, cyg, review):
    """Return (bucket, target, flag, reason). Rule order is the contract."""
    if VNODE.match(sym):
        return ("scaffold", "-", "-", "version-node identity object (WP-53 scaffold)")
    if sym in review:
        tgt = sym if sym in cyg else (alias_target(sym, cyg) or "-")
        return ("3", tgt, "review", review[sym])
    if sym in cyg:
        return ("2", sym, "-", "runtime exports the same name")
    tgt = alias_target(sym, cyg)
    if tgt is not None:
        return ("1", tgt, "-", "reserved-name alias of runtime export %s" % tgt)
    tgt = lfs_base(sym, cyg)
    if tgt is not None:
        return ("1", tgt, "-", "LFS 64-bit variant; on LP64 aliases %s" % tgt)
    base, present = chk_base(sym, cyg)
    if base is not None:
        if present:
            return ("3", base, "review", "FORTIFY _chk variant; shim over %s" % base)
        return ("4", "-", "-", "FORTIFY _chk variant; base %s also absent" % base)
    base, present = finite_base(sym, cyg)
    if base is not None:
        if present:
            return ("3", base, "review", "fast-math finite variant; shim/alias over %s" % base)
        return ("4", "-", "-", "fast-math finite variant; base %s absent" % base)
    return ("4", "-", "-", "absent from runtime export surface")


def build(maprows, cyg, review):
    out = []
    for soname, sym, version, binding, typ, bind in maprows:
        bucket, target, flag, reason = classify(sym, cyg, review)
        out.append([soname, sym, version, bucket, DISPOSITION[bucket],
                    target, flag, reason])
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", default=MAP_DEFAULT)
    ap.add_argument("--cygwin", default=CYG_DEFAULT)
    ap.add_argument("--review", default=REVIEW_DEFAULT)
    ap.add_argument("-o", "--out", default="-", help="classification TSV, - for stdout")
    ap.add_argument("--summary", action="store_true",
                    help="print bucket and fourth-bucket-category counts to stderr")
    ap.add_argument("--bucket4", metavar="FILE",
                    help="write the categorized fourth-bucket inventory TSV here")
    args = ap.parse_args()

    maprows = load_map(args.map)
    cyg = load_cyg(args.cygwin)
    review = load_review(args.review)

    # Every review-queue symbol must exist in the map, or the queue has drifted.
    mapsyms = set(r[1] for r in maprows)
    missing = sorted(s for s in review if s not in mapsyms)
    if missing:
        raise SystemExit("semantic-review.tsv names symbols not in the map: %s"
                         % ", ".join(missing))

    rows = build(maprows, cyg, review)

    fh = sys.stdout if args.out == "-" else open(args.out, "w", newline="\n")
    for r in rows:
        fh.write("\t".join(r) + "\n")
    if fh is not sys.stdout:
        fh.close()

    if args.bucket4:
        write_bucket4(rows, args.bucket4)

    if args.summary:
        summarize(rows, sys.stderr)


def category(sym):
    """Group a fourth-bucket symbol for the honest inventory."""
    if sym.endswith("_chk"):
        return "fortify-chk-no-base"
    if sym.startswith("_IO_"):
        return "stdio-internals"
    if sym.startswith("_dl_") or sym.startswith("__dl") or sym.endswith("_hook"):
        return "loader-internals"
    if sym.startswith("__pthread") or (sym.startswith("pthread_") and sym not in ()):
        return "pthread-internals"
    if sym.startswith("__res") or sym.startswith("res_") or sym.startswith("__gai") \
       or sym.startswith("__ns_") or sym.startswith("ns_"):
        return "resolver-internals"
    if sym.endswith("_finite"):
        return "fast-math-no-base"
    if re.search(r"(f128|f32x?|f64x?)$", sym) or re.search(r"(f128|f32x?|f64x?)_", sym):
        return "float-n-math"
    if sym.startswith("argp_") or sym.startswith("__argp"):
        return "argp"
    if sym.startswith("__"):
        return "internal-helpers"
    if sym.startswith("_"):
        return "underscore-internals"
    return "public-absent"


def write_bucket4(rows, path):
    b4 = [r for r in rows if r[3] == "4"]
    b4.sort(key=lambda r: (category(r[1]), r[0], r[1]))
    with open(path, "w", newline="\n") as fh:
        for soname, sym, version, bucket, disp, target, flag, reason in b4:
            fh.write("\t".join([category(sym), soname, sym, version, reason]) + "\n")


def summarize(rows, out):
    cnt = collections.Counter(r[3] for r in rows)
    out.write("map rows: %d\n" % len(rows))
    for b in ["1", "2", "3", "4", "scaffold"]:
        out.write("  bucket %-9s %-13s %5d\n" % (b, DISPOSITION[b], cnt[b]))
    out.write("  distinct symbols: %d\n" % len(set(r[1] for r in rows)))
    out.write("  bucket-3 flagged review: %d\n"
              % sum(1 for r in rows if r[3] == "3"))
    catc = collections.Counter(category(r[1]) for r in rows if r[3] == "4")
    out.write("fourth-bucket categories (stub, %d rows):\n" % cnt["4"])
    for c, n in sorted(catc.items(), key=lambda kv: (-kv[1], kv[0])):
        out.write("  %-22s %5d\n" % (c, n))


if __name__ == "__main__":
    main()
