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

# The committed string-slice wiring re-derives byte-identical from the
# same inputs; a drift in either input is a conscious regeneration.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice string \
    --table "$tmp/wire-string.gen.c" --thunks "$tmp/wire-string.gen.s" \
    --shims "$tmp/wire-string.shims.tsv" >/dev/null
cmp ../wire-string.gen.c "$tmp/wire-string.gen.c"
cmp ../wire-string.gen.s "$tmp/wire-string.gen.s"
cmp ../wire-string.shims.tsv "$tmp/wire-string.shims.tsv"

# The committed stdio wiring re-derives byte-identical too, and when the
# cross toolchain is present its thunks assemble and carry their
# versioned names.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice stdio \
    --table "$tmp/wire-stdio.gen.c" --thunks "$tmp/wire-stdio.gen.s" \
    --shims "$tmp/wire-stdio.shims.tsv" >/dev/null
cmp ../wire-stdio.gen.c "$tmp/wire-stdio.gen.c"
cmp ../wire-stdio.gen.s "$tmp/wire-stdio.gen.s"
cmp ../wire-stdio.shims.tsv "$tmp/wire-stdio.shims.tsv"
grep -q '"fopen"' "$tmp/wire-stdio.gen.c"
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-stdio-table.o" "$tmp/wire-stdio.gen.c"
XCC="${XCC:-x86_64-elfsysvnt-linux-gnu-gcc}"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-stdio.o" "$tmp/wire-stdio.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-stdio.o" | grep -q 'fopen@@GLIBC_2.2.5'
fi

# The committed posix wiring re-derives byte-identical as well, and
# when the cross toolchain is present its thunks assemble and carry
# their versioned names.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice posix \
    --table "$tmp/wire-posix.gen.c" --thunks "$tmp/wire-posix.gen.s" \
    --shims "$tmp/wire-posix.shims.tsv" >/dev/null
cmp ../wire-posix.gen.c "$tmp/wire-posix.gen.c"
cmp ../wire-posix.gen.s "$tmp/wire-posix.gen.s"
cmp ../wire-posix.shims.tsv "$tmp/wire-posix.shims.tsv"
grep -q '"execvp"' "$tmp/wire-posix.gen.c"
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-posix-table.o" "$tmp/wire-posix.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-posix.o" "$tmp/wire-posix.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-posix.o" | grep -q 'execvp@@GLIBC_2.2.5'
fi

# The committed stdlib wiring re-derives byte-identical as well, and
# when the cross toolchain is present its thunks assemble and carry
# their versioned names.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice stdlib \
    --table "$tmp/wire-stdlib.gen.c" --thunks "$tmp/wire-stdlib.gen.s" \
    --shims "$tmp/wire-stdlib.shims.tsv" >/dev/null
cmp ../wire-stdlib.gen.c "$tmp/wire-stdlib.gen.c"
cmp ../wire-stdlib.gen.s "$tmp/wire-stdlib.gen.s"
cmp ../wire-stdlib.shims.tsv "$tmp/wire-stdlib.shims.tsv"
grep -q '"strtol"' "$tmp/wire-stdlib.gen.c"
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-stdlib-table.o" "$tmp/wire-stdlib.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-stdlib.o" "$tmp/wire-stdlib.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-stdlib.o" | grep -q 'strtol@@GLIBC_2.2.5'
fi

echo 'real-map: ok'

# The committed filesystem wiring re-derives byte-identical as well.
# This is the first slice with a real shim worklist: the stat family
# and friends cross with layout-bearing structs, so their rows land in
# the shims file rather than as thunks, and the counts are pinned.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice filesystem \
    --table "$tmp/wire-filesystem.gen.c" \
    --thunks "$tmp/wire-filesystem.gen.s" \
    --shims "$tmp/wire-filesystem.shims.tsv" >/dev/null
cmp ../wire-filesystem.gen.c "$tmp/wire-filesystem.gen.c"
cmp ../wire-filesystem.gen.s "$tmp/wire-filesystem.gen.s"
cmp ../wire-filesystem.shims.tsv "$tmp/wire-filesystem.shims.tsv"
grep -q '"mkdir"' "$tmp/wire-filesystem.gen.c"
grep -q '^__xstat	' "$tmp/wire-filesystem.shims.tsv"
test "$(grep -vc '^#' "$tmp/wire-filesystem.shims.tsv")" = 36
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-filesystem-table.o" "$tmp/wire-filesystem.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-filesystem.o" "$tmp/wire-filesystem.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-filesystem.o" | grep -q 'mkdir@@GLIBC_2.2.5'
fi

# The committed memory wiring re-derives byte-identical as well. The
# slice is small and all thunks: malloc and its family forward whole,
# and the mmap family's flags translate downstream of the bind, so no
# row lands in the shims file yet.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice memory \
    --table "$tmp/wire-memory.gen.c" \
    --thunks "$tmp/wire-memory.gen.s" \
    --shims "$tmp/wire-memory.shims.tsv" >/dev/null
cmp ../wire-memory.gen.c "$tmp/wire-memory.gen.c"
cmp ../wire-memory.gen.s "$tmp/wire-memory.gen.s"
cmp ../wire-memory.shims.tsv "$tmp/wire-memory.shims.tsv"
grep -q '"malloc"' "$tmp/wire-memory.gen.c"
grep -q '"mmap"' "$tmp/wire-memory.gen.c"
test "$(grep -vc '^#' "$tmp/wire-memory.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-memory-table.o" "$tmp/wire-memory.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-memory.o" "$tmp/wire-memory.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-memory.o" | grep -q 'malloc@@GLIBC_2.2.5'
fi

# The committed sockets wiring re-derives byte-identical as well. The
# slice is all thunks: the sockaddr family crosses by pointer and
# length, laid out the same on both sides, and the flag translation
# the mmap family taught us belongs downstream of the bind here too,
# so no row lands in the shims file yet.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice sockets \
    --table "$tmp/wire-sockets.gen.c" \
    --thunks "$tmp/wire-sockets.gen.s" \
    --shims "$tmp/wire-sockets.shims.tsv" >/dev/null
cmp ../wire-sockets.gen.c "$tmp/wire-sockets.gen.c"
cmp ../wire-sockets.gen.s "$tmp/wire-sockets.gen.s"
cmp ../wire-sockets.shims.tsv "$tmp/wire-sockets.shims.tsv"
grep -q '"socket"' "$tmp/wire-sockets.gen.c"
grep -q '"getaddrinfo"' "$tmp/wire-sockets.gen.c"
test "$(grep -vc '^#' "$tmp/wire-sockets.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-sockets-table.o" "$tmp/wire-sockets.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-sockets.o" "$tmp/wire-sockets.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-sockets.o" | grep -q 'socket@@GLIBC_2.2.5'
fi
