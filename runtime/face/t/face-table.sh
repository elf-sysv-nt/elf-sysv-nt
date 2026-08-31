#!/bin/bash
# Usage: face-table.sh
#
# Certify the committed face table: it reproduces from the generator, it is
# total over WP-20's inventory, every WP-24 variadic export is disposed
# variadic, and every alias target names a real export.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face=$here/../face.tsv
exports=$here/../../exports/cygwin-exports.tsv
variadic=$here/../../varargs/variadic-exports.tsv
fail=0
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

"$here"/../gen-face.sh -o "$tmp"
if ! diff -q "$tmp" "$face" > /dev/null; then
  echo "FAIL: committed face.tsv does not reproduce"; fail=1
else
  echo "ok: face.tsv reproduces from the generator"
fi

if diff <(cut -f1 "$exports") <(cut -f1 "$face") > /dev/null; then
  echo "ok: total over the inventory, in its order ($(wc -l < "$face") rows)"
else
  echo "FAIL: face table names diverge from the inventory"; fail=1
fi

missing=$(comm -23 <(cut -f1 "$variadic" | sort -u) \
  <(awk -F'\t' '$2=="variadic"{print $1}' "$face" | sort -u))
if [ -z "$missing" ]; then
  echo "ok: all $(cut -f1 "$variadic" | sort -u | wc -l) variadic exports disposed variadic"
else
  echo "FAIL: variadic exports not disposed variadic: $missing"; fail=1
fi

bad=$(awk -F'\t' '$4==""||$4=="-"{print $1}' "$face")
if [ -z "$bad" ]; then
  echo "ok: every row binds a target"
else
  echo "FAIL: rows with no bind target: $bad"; fail=1
fi

bad=$(awk -F'\t' 'NR==FNR{if($2=="data")data[$1]=1;next}
  $2=="data" && $4!=$1 && !($4 in data){print $1"->"$4}' "$exports" "$face")
if [ -z "$bad" ]; then
  echo "ok: every data alias binds to a data export"
else
  echo "FAIL: data aliases with non-data targets: $bad"; fail=1
fi

bad=$(awk -F'\t' '$2=="variadic" && $4!=$1{print $1"->"$4}' "$face")
if [ -z "$bad" ]; then
  echo "ok: no variadic export is an alias"
else
  echo "FAIL: aliased variadic exports (veneer binding unclear): $bad"; fail=1
fi

n=$(awk -F'\t' '$2!~/^(data|variadic|sv2ms)$/' "$face" | wc -l)
if [ "$n" = 0 ]; then
  echo "ok: every disposition is one of data|variadic|sv2ms"
else
  echo "FAIL: $n rows with an unknown disposition"; fail=1
fi

[ $fail = 0 ] && echo PASS || echo FAIL
exit $fail
