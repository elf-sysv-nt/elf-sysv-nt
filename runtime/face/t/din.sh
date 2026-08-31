#!/usr/bin/env bash
# WP-27: certify the .def/.din seam.
#
# Five properties. face.din reproduces byte for byte from its inputs; it
# carries one export row per face-table row, in face-table order; the
# dispositions land at the pinned counts (1716 faced, 39 DATA, 12 asis);
# every faced row renames __face_<name> back to its export name and keeps
# the face table's fence marker; and the vendor's own gendef consumes the
# file, emitting a sigfe stub for exactly the SIGFE-fenced faces.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_dir=$here/..
tree=/c/-/repo/newlib-cygwin
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. reproduce
"$face_dir"/gen-din.sh -o "$tmp/face.din"
if cmp -s "$tmp/face.din" "$face_dir/face.din"; then
  say "ok: face.din reproduces byte for byte"
else
  bad "face.din does not reproduce"
fi

# 2. one export row per face-table row, in order
cut -f1 "$face_dir/face.tsv" > "$tmp/want.names"
awk 'NR > 4 { print $1 }' "$face_dir/face.din" > "$tmp/have.names"
if cmp -s "$tmp/want.names" "$tmp/have.names"; then
  say "ok: total over the face table, in face-table order ($(wc -l < "$tmp/want.names") exports)"
else
  bad "export rows differ from the face table"
  diff "$tmp/want.names" "$tmp/have.names" | head -10
fi

# 3. the pinned disposition counts
faced=$(grep -c ' = __face_' "$face_dir/face.din" || true)
data=$(grep -c ' DATA$' "$face_dir/face.din" || true)
total=$(awk 'NR > 4' "$face_dir/face.din" | wc -l)
asis=$((total - faced - data))
if [ "$faced" = 1716 ] && [ "$data" = 39 ] && [ "$asis" = 12 ]; then
  say "ok: pinned counts faced/DATA/asis = $faced $data $asis"
else
  bad "counts faced/DATA/asis = $faced $data $asis, want 1716 39 12"
fi

# 4. every faced row renames its own face and keeps the face-table fence
awk -F'\t' 'NR==FNR {
    if ($2 != "data") fence[$1] = ($3 == "none" || $3 == "-") ? "" : $3
    next
  }
  / = __face_/ {
    split($0, w, " ")
    want = w[1] " = __face_" w[1] (fence[w[1]] == "" ? "" : " " fence[w[1]])
    if ($0 != want) { print "row: " $0 " want: " want; n++ }
  }
  END { exit n > 0 }' \
  "$face_dir/face.tsv" "$face_dir/face.din" > "$tmp/rows.err" \
  && say "ok: every faced row binds __face_<name> under its own fence" \
  || { bad "faced rows disagree with the face table"; head -5 "$tmp/rows.err"; }

# 5. the vendor gendef consumes the seam
if [ -f "$tree/winsup/cygwin/scripts/gendef" ]; then
  ( cd "$tmp" && perl "$tree/winsup/cygwin/scripts/gendef" --cpu=x86_64 \
      --output-def=face.def "$face_dir/face.din" )
  defrows=$(awk 'NR > 2' "$tmp/face.def" | grep -c .)
  sigfe_want=$(grep -c '= __face_.* SIGFE$' "$face_dir/face.din" || true)
  sigfe_have=$(grep -c '= _sigfe___face_' "$tmp/face.def" || true)
  if [ "$defrows" = 1767 ] && [ "$sigfe_have" = "$sigfe_want" ]; then
    say "ok: gendef consumes the seam ($defrows exports, $sigfe_have sigfe-fenced faces)"
  else
    bad "gendef output: $defrows exports, sigfe $sigfe_have want $sigfe_want"
  fi
else
  say "skip: pinned tree not present; gendef parse not run"
fi

[ $fail = 0 ] && say "PASS: din" || say "FAIL: din"
exit $fail
