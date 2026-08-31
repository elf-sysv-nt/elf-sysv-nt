#!/usr/bin/env bash
#
# WP-27: resolve the unlisted faces from Cygwin's own declarations.
#
# derive-sigclass.sh classed 222 sv2ms faces unlisted: no prototype in its
# probe's reach. This tool settles them from the one authority left -- the
# pinned newlib-cygwin tree itself -- in two layers:
#
#   probe    a second aux-info probe over the headers the first one had no
#            reason to include: the fortified string/stdio surface, the
#            windows-typed half of sys/cygwin.h, threads.h, uchar.h, the
#            xdr headers the tree ships but the host does not install, and
#            the acl, dbm, pty, resolver, signalfd and timerfd sets.
#   residue  what no header anywhere declares, curated by hand in
#            unlisted-residue.tsv with a citation into the pinned tree per
#            row. Classes beyond int/fp: asis (the PE-side startup and
#            compiler protocol -- faced by nothing, exported unchanged)
#            and data (an object the .din failed to mark).
#
# The two layers must partition the unlisted set exactly: a name both the
# probe and the residue table claim, or one neither claims, is an error.
#
# Output is unlisted.tsv: name, class, prototype, origin (probe|residue),
# in sigclass.tsv's own order, one row per unlisted face.
#
# Usage:
#   derive-unlisted.sh [-o FILE] [--sigclass FILE] [--residue FILE]
#                      [--tree DIR] [--terse]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
sigclass=$here/sigclass.tsv
residue=$here/unlisted-residue.tsv
tree=/c/-/repo/newlib-cygwin
out=$here/unlisted.tsv
terse=0
while [ $# -gt 0 ]; do
  case $1 in
    -o) out=$2; shift 2;;
    --sigclass) sigclass=$2; shift 2;;
    --residue) residue=$2; shift 2;;
    --tree) tree=$2; shift 2;;
    --terse) terse=1; shift;;
    *) echo "unknown option: $1" >&2; exit 2;;
  esac
done

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The supplementary probe. windows.h first: sys/cygwin.h only declares its
# handle-typed entries once the windows types exist. Fortification at -O2
# turns on the __*_chk declarations in newlib's ssp headers. The tree's own
# newlib include directory rides behind the host's (-idirafter) purely for
# rpc/xdr.h, which Cygwin exports but does not install.
cat > "$tmp/probe.c" <<'EOF'
#define _GNU_SOURCE 1
#include <windows.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <threads.h>
#include <uchar.h>
#include <math.h>
#include <libgen.h>
#include <pty.h>
#include <utmp.h>
#include <ndbm.h>
#include <resolv.h>
#include <rpc/xdr.h>
#include <sys/acl.h>
#include <cygwin/acl.h>
#include <acl/libacl.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/cygwin.h>
#include <process.h>
#include <io.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <wchar.h>
#include <search.h>
#include <fenv.h>
EOF

gcc -O2 -D_FORTIFY_SOURCE=2 -aux-info "$tmp/probe.aux" -fsyntax-only \
    -idirafter "$tree/newlib/libc/include" "$tmp/probe.c"

# aux-info rows -> "name<TAB>prototype", first declaration wins. static
# too: stdio_ext.h declares its surface as elidable inlines, and the DLL
# exports the out-of-line bodies under the same shapes.
sed -n 's,^/\* [^*]*\*/ \(extern\|static\) ,,p' "$tmp/probe.aux" \
| awk '
  {
    line = $0
    sub(/;[ \t]*$/, "", line)
    if (match(line, /[A-Za-z_][A-Za-z0-9_]*[ \t]*\(/) == 0) next
    name = substr(line, RSTART)
    sub(/[ \t]*\(.*/, "", name)
    if (!(name in seen)) { seen[name] = 1; printf "%s\t%s\n", name, line }
  }
' > "$tmp/protos.tsv"

# Merge: every unlisted row of sigclass.tsv resolves through exactly one
# layer. Probe rows classify with derive-sigclass.sh's own tests; residue
# rows carry their class in the table.
awk -F'\t' -v OFS='\t' -v protos="$tmp/protos.tsv" -v residue="$residue" '
  BEGIN {
    while ((getline line < protos) > 0) {
      i = index(line, "\t")
      proto[substr(line, 1, i - 1)] = substr(line, i + 1)
    }
    while ((getline line < residue) > 0) {
      split(line, f, "\t")
      rclass[f[1]] = f[2]; rproto[f[1]] = f[3]
    }
  }
  $2 != "unlisted" { next }
  {
    name = $1
    inp = (name in proto); inr = (name in rclass)
    if (inp && inr) { printf "both layers claim %s\n", name > "/dev/stderr"; bad = 1; next }
    if (!inp && !inr) { printf "no layer covers %s\n", name > "/dev/stderr"; bad = 1; next }
    if (inr) { print name, rclass[name], rproto[name], "residue"; n[rclass[name]]++; nr++; next }
    p = proto[name]
    cls = "int"
    if (p ~ /(^|[^A-Za-z0-9_])(float|double|_Complex|_Float[0-9]+)([^A-Za-z0-9_]|$)/)
      cls = "fp"
    else {
      q = p
      gsub(/(struct|union)[ \t]+[A-Za-z_][A-Za-z0-9_]*[ \t]*\*+/, "PTR ", q)
      gsub(/(div_t|ldiv_t|lldiv_t|imaxdiv_t|fenv_t|fexcept_t)[ \t]*\*+/, "PTR ", q)
      if (q ~ /(^|[^A-Za-z0-9_])(struct|union)([^A-Za-z0-9_]|$)/ ||
          q ~ /(^|[^A-Za-z0-9_])(div_t|ldiv_t|lldiv_t|imaxdiv_t|fenv_t)([^A-Za-z0-9_]|$)/)
        cls = "aggr"
    }
    print name, cls, p, "probe"
    n[cls]++; np++
  }
  END {
    printf "total\t%d\nprobe\t%d\nresidue\t%d\nint\t%d\nfp\t%d\naggr\t%d\nasis\t%d\ndata\t%d\n", \
      np + nr, np, nr, n["int"], n["fp"], n["aggr"], n["asis"], n["data"] > "/dev/stderr"
    if (bad) exit 1
  }
' "$sigclass" > "$tmp/unlisted.tsv" 2> "$tmp/counts" \
  || { cat "$tmp/counts" >&2; exit 1; }

if [ $terse = 1 ]; then cat "$tmp/counts"; exit 0; fi
cp "$tmp/unlisted.tsv" "$out"
cat "$tmp/counts" >&2
