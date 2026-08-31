#!/usr/bin/env bash
# WP-27: certify the signature-class table.
#
# Four properties: the committed table reproduces byte for byte from its
# inputs; it is total over the sv2ms rows of the face table, in their order;
# no variadic or data export leaks in; and the class counts are the pinned
# ones, so a header drift that silently reclassifies part of the surface
# fails here rather than in the generated faces.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_dir=$here/..
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. reproduce
"$face_dir"/derive-sigclass.sh -o "$tmp/sigclass.tsv" 2> "$tmp/counts"
if cmp -s "$tmp/sigclass.tsv" "$face_dir/sigclass.tsv"; then
  say "ok: sigclass.tsv reproduces byte for byte"
else
  bad "sigclass.tsv does not reproduce"
fi

# 2. total over the sv2ms rows, in face.tsv order
awk -F'\t' '$2 == "sv2ms" { print $1 }' "$face_dir/face.tsv" > "$tmp/want"
cut -f1 "$face_dir/sigclass.tsv" > "$tmp/have"
if cmp -s "$tmp/want" "$tmp/have"; then
  say "ok: total over the sv2ms surface, in face-table order ($(wc -l < "$tmp/want") rows)"
else
  bad "sigclass rows differ from the face table's sv2ms rows"
  diff "$tmp/want" "$tmp/have" | head -10
fi

# 3. no variadic or data export present
awk -F'\t' '$2 != "sv2ms" { print $1 }' "$face_dir/face.tsv" | sort > "$tmp/other"
cut -f1 "$face_dir/sigclass.tsv" | sort > "$tmp/have.sorted"
leak=$(comm -12 "$tmp/other" "$tmp/have.sorted" | head -5)
if [ -z "$leak" ]; then
  say "ok: no variadic or data export in the table"
else
  bad "non-sv2ms exports leaked in: $leak"
fi

# 4. pinned counts
read -r int fp aggr unlisted < <(awk -F'\t' '
  { n[$2]++ }
  END { print n["int"], n["fp"], n["aggr"], n["unlisted"] }
' "$face_dir/sigclass.tsv")
want="1122 307 10 222"
got="$int $fp $aggr $unlisted"
if [ "$got" = "$want" ]; then
  say "ok: pinned counts int/fp/aggr/unlisted = $got"
else
  bad "counts drifted: want $want, got $got"
fi

# 5. every classified row carries a prototype; every unlisted row does not
if awk -F'\t' '($2 == "unlisted") != ($3 == "-") { exit 1 }' "$face_dir/sigclass.tsv"; then
  say "ok: prototypes present exactly on the classified rows"
else
  bad "a row's class and prototype column disagree"
fi

[ $fail = 0 ] && say "verdict: yes" || { say "verdict: no"; exit 1; }
