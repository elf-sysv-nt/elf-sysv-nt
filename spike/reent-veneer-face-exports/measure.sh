#!/usr/bin/env bash
# reent-veneer-face-exports 1.0 -- every run-time resolution key the veneer's
# FUNC-forward thunks emit is a real export of the WP-27 face.
#
# spike/reent-veneer-thunk pinned the link-time shape of one thunk: it names its
# target (e.g. "strtol") as a .rodata string and resolves that name at run time
# through the WP-27 crossing's walk of elfsysv1.dll's PE export directory. That
# spike measured the shape at a single symbol and asserted nothing about the
# other thousand forwards, nor that the name each keys on is one the face in
# fact exports. This spike closes that gap across the whole forward set: it is
# the honest bridge between "the thunk keys on a name" (link-time, proven) and
# "the run reaches the face" (item 3, behind the built face DLL) -- if a key
# named no face export, the run-time walk would return null and the thunk would
# fault, whatever its link-time shape.
#
# It reads two committed truths rather than a run:
#
#   the keys   veneer/libc/build-libc emits libc-forward.tsv, one row per mapped
#              symbol; a FUNC row classed forward-same or forward-alias carries
#              in its target column the export name its thunk hands the resolver.
#              These are exactly the names the run-time walk will look up.
#
#   the face   runtime/face/face.tsv is the committed export table gen-din.sh
#              turns into face.din's EXPORTS -- elfsysv1.dll's PE export
#              directory, the set the crossing's walk searches. Building the DLL
#              itself needs the heavy WP-26 winsup tree; its export *names* are
#              this committed table, so the cross-check needs no native build.
#
# The findings, reproduced (measure.sh, cross toolchain + committed face table):
#
#   func_forward_keys_extracted=<n>. n unique FUNC forward-same/forward-alias
#   target names, the run-time resolution keys.
#
#   all_keys_are_face_exports=yes. Every one of those keys is a name in
#   face.tsv's export column: the run-time walk finds a face export for each.
#
#   exemplars_present=yes. strtol (the reent-consuming exemplar the thunk spike
#   used) and memcpy (the bindings exemplar WP-53 certifies) are both among the
#   keys and both face exports.
#
# SKIPs (verdict yes, exit 0) when the cross toolchain is absent, build-libc's
# output being an uncommitted build product, as the crossing spikes do.
set -u

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }

emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
emit_lines() { while IFS= read -r ln; do emit "$ln"; done; }
[ -n "$dest" ] && : >"$dest"

prefix=${BUILD_LIBC_PREFIX:-$HOME/x-elfsysvnt}
target=${BUILD_LIBC_TARGET:-x86_64-elfsysvnt-linux-gnu}
GCC=$prefix/bin/$target-gcc
face=$root/runtime/face/face.tsv

emit "script  reent-veneer-face-exports 1.0"
emit ""
emit "host        $(hostname)"
emit "cross gcc   $($GCC --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -x "$GCC" ] || ! command -v python3 >/dev/null 2>&1; then
emit "skip=no cross toolchain at $prefix or no python3; build it first"
emit "verdict=yes"
exit 0
fi
if [ ! -f "$face" ]; then
emit "face_table_present=no  ($face missing)"
emit "verdict=no"
exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-veneer-face-exports.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# Build the veneer to get the forward map. build-libc reseeds its build dir.
if ! "$root/veneer/libc/build-libc" -P "$prefix" -T "$target" \
        -B "$tmp/build" -q >"$tmp/build.err" 2>&1; then
emit "build_libc=no"
sed 's/^/    /' "$tmp/build.err" | emit_lines
emit "verdict=no"
exit 1
fi
fwd=$tmp/build/libc-forward.tsv
[ -f "$fwd" ] || { emit "build_libc=no  (no libc-forward.tsv)"; emit "verdict=no"; exit 1; }

# The keys: FUNC rows classed forward-same/forward-alias, their target column.
awk -F'\t' '$4=="func" && ($7=="forward-same"||$7=="forward-alias"){print $8}' \
"$fwd" | sort -u >"$tmp/keys.txt"
nkeys=$(wc -l <"$tmp/keys.txt" | tr -d ' ')
emit "func_forward_keys_extracted=$nkeys  (unique forward-same/forward-alias FUNC targets)"

# The face's exported names: the first column of face.tsv (== face.din EXPORTS).
awk -F'\t' 'NF>=1 && $1!~/^#/{print $1}' "$face" | sort -u >"$tmp/faceexp.txt"
nface=$(wc -l <"$tmp/faceexp.txt" | tr -d ' ')

# Keys that name no face export.
comm -23 "$tmp/keys.txt" "$tmp/faceexp.txt" >"$tmp/missing.txt"
nmiss=$(wc -l <"$tmp/missing.txt" | tr -d ' ')
if [ "$nmiss" -eq 0 ]; then
emit "all_keys_are_face_exports=yes  (all $nkeys keys among $nface face exports)"
else
emit "all_keys_are_face_exports=no  ($nmiss of $nkeys keys export nothing)"
sed 's/^/    missing: /' "$tmp/missing.txt" | head -20 | emit_lines
fi

# Exemplars: strtol (reent) and memcpy (bindings) are keys and face exports.
ex_ok=yes
for s in strtol memcpy; do
grep -qx "$s" "$tmp/keys.txt"    || { ex_ok=no; emit "    $s not a forward key"; }
grep -qx "$s" "$tmp/faceexp.txt" || { ex_ok=no; emit "    $s not a face export"; }
done
emit "exemplars_present=$ex_ok  (strtol and memcpy are forward keys and face exports)"

if [ "$nmiss" -eq 0 ] && [ "$ex_ok" = yes ]; then
emit "verdict=yes"
exit 0
fi
emit "verdict=no"
exit 1
