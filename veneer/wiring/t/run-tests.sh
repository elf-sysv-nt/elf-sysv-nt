#!/bin/sh
# Network-free tests for cut-slices.py: map over fixture headers with
# the native compiler, order over a synthetic ranking. Then the xlat
# core: regenerate, require byte-identity with the committed files, and
# run the compiled spot checks.
set -e
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 ../cut-slices.py map --slices fixture-slices.tsv \
    --include fixtures --cc "${CC:-gcc}" -o "$tmp/map.tsv"
awk -F'\t' '$1=="frob_alloc"{print $2}' "$tmp/map.tsv" | grep -qx memory
awk -F'\t' '$1=="frob_open"{print $2}' "$tmp/map.tsv" | grep -qx files
# frob_shared is declared by both headers; the earlier row wins
awk -F'\t' '$1=="frob_shared"{print $2}' "$tmp/map.tsv" | grep -qx memory

python3 ../cut-slices.py order --map "$tmp/map.tsv" \
    --ranking fixture-ranking.tsv --out "$tmp/out"
# files leads (demand 10), then memory (5), then unassigned (2);
# the libother row must not appear anywhere
head -1 "$tmp/out/slice-order.tsv" | grep -q '^files'
awk -F'\t' '$1=="memory"{print $3}' "$tmp/out/slice-order.tsv" | grep -qx 5
grep -q unknown_sym "$tmp/out/slice-unassigned.tsv"
! grep -rq nope "$tmp/out"

python3 ../gen-xlat.py --out "$tmp" >/dev/null
diff -u ../xlat-core.gen.c "$tmp/xlat-core.gen.c"
diff -u ../xlat-core.gen.h "$tmp/xlat-core.gen.h"
"${CC:-gcc}" -Wall -Wextra -Werror -o "$tmp/test-xlat" \
    test-xlat.c ../xlat-core.gen.c
"$tmp/test-xlat"

python3 ../gen-wire.py --forward-map fixture-forward.tsv     --slice-map fixture-wire-slices.tsv --slice files     --table "$tmp/wire-files.gen.c" --thunks "$tmp/wire-files.gen.s"     --shims "$tmp/wire-files.shims.tsv" 2>/dev/null
"${CC:-gcc}" -Wall -Wextra -Werror -I.. -o "$tmp/test-wire"     test-wire.c ../wire.c "$tmp/wire-files.gen.c"
"$tmp/test-wire"
# the shim worklist carries frob_shim and its table index, nothing else
grep -q "^frob_shim	GLIBC_2.2.5	default	-	3$" "$tmp/wire-files.shims.tsv"
test "$(grep -cv '^#' "$tmp/wire-files.shims.tsv")" = 1
# the thunks assemble for the triple and carry the versioned names,
# when the cross toolchain is present
XAS="${XCC:-x86_64-elfsysvnt-linux-gnu-gcc}"
if command -v "$XAS" >/dev/null 2>&1; then
    "$XAS" -c -o "$tmp/wire-files.o" "$tmp/wire-files.gen.s"
    xnm="${XAS%gcc}nm"
    "$xnm" "$tmp/wire-files.o" | grep -q 'frob_open@@GLIBC_2.2.5'
    "$xnm" "$tmp/wire-files.o" | grep -q 'frob_open@GLIBC_2.0'
    "$xnm" "$tmp/wire-files.o" | grep -q 'frob_alias@@GLIBC_2.2.5'
    "$xnm" "$tmp/wire-files.o" | grep -q "frob_weak@@GLIBC_2.2.5"
    ! "$xnm" "$tmp/wire-files.o" | grep -q frob_shim
fi

# The per-slice differential harness, judged before it judges any slice:
# both sides host gcc so no WSL and no wired veneer is needed. Same source
# on both sides must pass; a runner that garbles the candidate's output
# must be reported as a divergence, not a pass.
mkdir -p "$tmp/diffwork/diff/files"
cp diff-cases/hello.c "$tmp/diffwork/diff/files/"
cp ../diff-slice.sh "$tmp/diffwork/"
ESN_REF_CC="${CC:-gcc}" ESN_CC="${CC:-gcc}" \
    bash "$tmp/diffwork/diff-slice.sh" files | grep -q 'all match'
printf '#!/bin/sh\n"$1"; echo garble\n' > "$tmp/garble"
chmod 755 "$tmp/garble"
if ESN_REF_CC="${CC:-gcc}" ESN_CC="${CC:-gcc}" ESN_RUN="$tmp/garble" \
    bash "$tmp/diffwork/diff-slice.sh" files > "$tmp/diverge.txt"; then
    echo "diff-slice missed a divergence" >&2; exit 1
fi
grep -q 'DIVERGED files/hello' "$tmp/diverge.txt"

# The real forward map against the real slice map, pinned counts.
sh real-map.sh

# The ctype-table filled stub: pinned to its generator, and certified
# byte-for-byte against el8's real tables where the image is reachable.
bash ctype-table.sh

echo ok
