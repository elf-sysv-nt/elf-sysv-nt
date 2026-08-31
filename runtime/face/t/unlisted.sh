#!/usr/bin/env bash
# WP-27: certify the unlisted-face resolution.
#
# Five properties: the committed table reproduces byte for byte from the
# probe and the residue table; it is total over sigclass.tsv's unlisted
# rows, in their order; the class counts are the pinned ones; every residue
# row's citation names a file in the pinned tree that really carries the
# declaration it claims; and prototypes sit exactly where a face generator
# will need them -- on every fp and aggr row.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_dir=$here/..
tree=${ELFSYSVNT_NEWLIB_TREE:-/c/-/repo/newlib-cygwin}
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. reproduce
"$face_dir"/derive-unlisted.sh -o "$tmp/unlisted.tsv" --tree "$tree" 2> /dev/null
if cmp -s "$tmp/unlisted.tsv" "$face_dir/unlisted.tsv"; then
  say "ok: unlisted.tsv reproduces byte for byte"
else
  bad "unlisted.tsv does not reproduce"
fi

# 2. total over sigclass.tsv's unlisted rows, in order
awk -F'\t' '$2 == "unlisted" { print $1 }' "$face_dir/sigclass.tsv" > "$tmp/want"
cut -f1 "$face_dir/unlisted.tsv" > "$tmp/have"
if cmp -s "$tmp/want" "$tmp/have"; then
  say "ok: total over the unlisted surface, in sigclass order ($(wc -l < "$tmp/want") rows)"
else
  bad "unlisted rows differ from sigclass's unlisted rows"
  diff "$tmp/want" "$tmp/have" | head -10
fi

# 3. pinned counts
read -r cint cfp caggr casis cdata probe residue < <(awk -F'\t' '
  { n[$2]++; o[$4]++ }
  END { print n["int"], n["fp"], n["aggr"]+0, n["asis"], n["data"], o["probe"], o["residue"] }
' "$face_dir/unlisted.tsv")
want="193 16 0 12 1 144 78"
got="$cint $cfp $caggr $casis $cdata $probe $residue"
if [ "$got" = "$want" ]; then
  say "ok: pinned counts int/fp/aggr/asis/data probe/residue = $got"
else
  bad "counts drifted: want $want, got $got"
fi

# 4. every residue citation resolves in the pinned tree
miss=0
while IFS=$'\t' read -r name cls proto src pat; do
  [ "$pat" = "-" ] && pat=$name
  if [ ! -f "$tree/$src" ]; then
    bad "residue $name cites a missing file: $src"; miss=1; continue
  fi
  if ! grep -qF "$pat" "$tree/$src"; then
    bad "residue $name: '$pat' not found in $src"; miss=1
  fi
done < "$face_dir/unlisted-residue.tsv"
[ $miss = 0 ] && say "ok: all $(wc -l < "$face_dir/unlisted-residue.tsv") residue citations resolve in the pinned tree"

# 5. prototypes exactly where generation needs them
if awk -F'\t' '($2 == "fp" || $2 == "aggr") && $3 == "-" { exit 1 }' "$face_dir/unlisted.tsv"; then
  say "ok: every fp and aggr row carries its prototype"
else
  bad "an fp or aggr row lacks a prototype"
fi

[ $fail = 0 ] && say "verdict: yes" || { say "verdict: no"; exit 1; }
