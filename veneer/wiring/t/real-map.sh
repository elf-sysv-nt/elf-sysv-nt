#!/bin/sh
# The real forward map against the real slice map (WP-56).
#
# Everything before this judged the wiring machinery over fixtures. This
# derives libc-forward.tsv the same way build-libc does -- generate.py
# over the committed WP-51/WP-52 inputs -- and certifies that the slice
# map accounts for the map's wired function rows, pinning the counts so
# a drift in either input is a conscious re-pin, not a silent shift.
set -e
cd "$(dirname "$0")"
root=$(cd ../../.. && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 "$root/veneer/libc/generate.py" --soname libc.so.6 \
    --map "$root/veneer/version-map/glibc-version-map.tsv" \
    --nodes "$root/veneer/version-map/glibc-version-nodes.tsv" \
    --classification "$root/veneer/classification/classification.tsv" \
    --asm "$tmp/sym.s" --version-script "$tmp/libc.map" \
    --forward-map "$tmp/fwd.tsv" --static-asm "$tmp/static.s" \
    >/dev/null

# The wired dispositions, counted from the derived map.
count() { awk -F'\t' -v d="$1" '$7==d' "$tmp/fwd.tsv" | wc -l; }
test "$(count forward-same)" = 1077
test "$(count forward-alias)" = 185
test "$(count shim)" = 136

# Wired function rows the slice map does not place. 275 today: the
# underscore internals no public header declares, gets (hidden from a
# _GNU_SOURCE scan, as el8's stdio.h is C11), and the SunRPC xdr_*
# family, whose headers el8 moved out of glibc into libtirpc.
awk -F'\t' 'NR==FNR{m[$1]=1;next}
    ($7=="forward-same"||$7=="forward-alias"||$7=="shim") &&
    ($4=="func"||$4=="ifunc") && !($1 in m){print $1}' \
    ../symbol-slice.tsv "$tmp/fwd.tsv" | sort -u > "$tmp/unassigned.txt"
test "$(wc -l < "$tmp/unassigned.txt")" = 275
# and every non-underscore one is on the two documented residues
if grep -v '^_' "$tmp/unassigned.txt" | grep -v '^xdr' | grep -qvx gets
then
    echo 'real-map: a new public symbol fell into unassigned:' >&2
    grep -v '^_' "$tmp/unassigned.txt" | grep -v '^xdr' | grep -vx gets >&2
    exit 1
fi

# One real slice generates and, when the cross toolchain is present, its
# thunks assemble and carry their versioned names.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice stdio \
    --table "$tmp/wire-stdio.gen.c" --thunks "$tmp/wire-stdio.gen.s" \
    --shims "$tmp/wire-stdio.shims.tsv" >/dev/null
grep -q '"fopen"' "$tmp/wire-stdio.gen.c"
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-stdio-table.o" "$tmp/wire-stdio.gen.c"
XCC="${XCC:-x86_64-elfsysvnt-linux-gnu-gcc}"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-stdio.o" "$tmp/wire-stdio.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-stdio.o" | grep -q 'fopen@@GLIBC_2.2.5'
fi

echo 'real-map: ok'
