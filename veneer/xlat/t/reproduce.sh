#!/usr/bin/env bash
#
# WP-55 reproduce test, in the WP-51 manner: rerun the extraction from
# the pinned inputs and require it to reproduce the committed tables
# byte for byte.  A drift here means a header tree moved under the pin,
# the toolchain changed the answer, or someone edited a generated file
# by hand; all three are findings.
#
# Exit codes: 0 reproduced, 1 drift or failure.

set -u

here=$(cd "$(dirname "$0")" && pwd)
xlat=$here/..

fail() { echo "reproduce: FAIL: $*" >&2; exit 1; }

inc=$("$xlat/fetch-kernel-headers.sh") || fail "kernel headers unavailable"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 "$xlat/extract-tables.py" --kernel-include "$inc" --out "$work" \
  2>"$work/summary" || { cat "$work/summary" >&2; fail "extraction failed"; }

status=0
for t in errno-map.tsv signal-map.tsv flags.tsv layouts.tsv dropped.tsv; do
  if ! cmp -s "$xlat/$t" "$work/$t"; then
    echo "reproduce: $t drifted:" >&2
    diff -u "$xlat/$t" "$work/$t" | head -40 >&2
    status=1
  fi
done
[ $status = 0 ] || fail "the extraction no longer reproduces the commit"

# Spot checks: a known divergence per class, so a table of accidental
# zeros cannot pass as reproduced.
grep -qP "^EADDRINUSE\t98\t112$" "$xlat/errno-map.tsv" \
  || fail "EADDRINUSE 98/112 missing"
grep -qP "^SIGUSR1\t10\t30$" "$xlat/signal-map.tsv" \
  || fail "SIGUSR1 10/30 missing"
grep -qP "^O\tO_CREAT\t64\t512$" "$xlat/flags.tsv" \
  || fail "O_CREAT 64/512 missing"
grep -qP "^stat\tst_size\t48\t8\t40\t8$" "$xlat/layouts.tsv" \
  || fail "stat.st_size 48/40 missing"

echo "reproduce: OK ($(cat "$work/summary"))"
