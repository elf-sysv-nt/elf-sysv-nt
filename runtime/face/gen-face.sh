#!/bin/bash
# Usage:
#   gen-face.sh [-o FILE] [--exports FILE] [--variadic FILE]
#   gen-face.sh --terse
#
# Derive the face table: one row per export of elfsysv1.dll, with the
# disposition WP-27 gives it. Reads WP-20's inventory and WP-24's variadic
# list; writes tab-separated rows in the inventory's own order:
#
#     name   disposition               sigfe   target
#            data | variadic | sv2ms
#
# data     exported unchanged; a data object carries no calling convention.
# variadic WP-24's generated veneer entry (unpack-and-repass, DR-0015).
# sv2ms    a System V-faced entry over the Microsoft-ABI body.
#
# target is the body the face binds to: the .din alias target where the row
# is an alias (accept = cygwin_accept binds the face to cygwin_accept, a
# DLL-internal symbol rather than another export), the name itself otherwise.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
exports=$here/../exports/cygwin-exports.tsv
variadic=$here/../varargs/variadic-exports.tsv
out=$here/face.tsv
terse=0
while [ $# -gt 0 ]; do
  case $1 in
    -o) out=$2; shift 2;;
    --exports) exports=$2; shift 2;;
    --variadic) variadic=$2; shift 2;;
    --terse) terse=1; shift;;
    *) echo "unknown option: $1" >&2; exit 2;;
  esac
done
gen() {
  awk -F'\t' -v OFS='\t' -v var="$variadic" '
    BEGIN { while ((getline line < var) > 0) { split(line, f, "\t"); isvar[f[1]] = 1 } }
    {
      name=$1; kind=$2; sigfe=$3; alias=$4
      if (kind == "data")          d = "data"
      else if (name in isvar)      d = "variadic"
      else                         d = "sv2ms"
      target = (alias != "-") ? alias : name
      n[d]++; total++; if (alias != "-") aliased++
      print name, d, sigfe, target
    }
    END {
      printf "total\t%d\ndata\t%d\nvariadic\t%d\nsv2ms\t%d\naliased\t%d\n", \
        total, n["data"], n["variadic"], n["sv2ms"], aliased > "/dev/stderr"
    }
  ' "$exports"
}
if [ $terse = 1 ]; then gen > /dev/null; exit 0; fi
if [ "$out" = - ]; then gen 2>/dev/null; else gen > "$out" 2>/dev/null; fi
