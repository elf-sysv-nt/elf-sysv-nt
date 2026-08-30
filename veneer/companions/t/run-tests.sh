#!/usr/bin/env bash
#
# WP-54's certification: the eight companion libraries are the vendor's
# surface, and a vendor-shaped binary's requirements resolve entirely into the
# built tree.
#
#   build     build-companions runs clean from the committed map and
#             classification; eight shared objects come out. libc.so.6 is
#             built beside them, since the needed check links against all nine.
#   surface   Per companion, every row of the version map is in .dynsym at the
#             node the map assigns it, with the map's own binding, and nothing
#             else is. WP-53 proved this for libc; here it holds for the other
#             eight.
#   ladder    Per companion, the .gnu.version_d ladder read back out of the
#             file equals the vendor's node list, name and parent, in order.
#   needed    The exit criterion. A stand-in vendor binary is linked carrying
#             a DT_NEEDED entry and a verneed entry against each of the nine
#             libraries, and elfneeds.py resolves every requirement into the
#             built tree with no name left over. The stand-in reimplements the
#             vendor binary the way WP-53's provides.py reimplemented elfdeps:
#             same shape, read back out of the file format.
#   leftover  The checker can say no: with one library withheld from the tree,
#             the same binary's requirements must come back left over.
#   fuzz      elfneeds.py refuses mutated binaries rather than crashing. It
#             reads files it did not write, and every offset in its walk comes
#             out of the file.
#
# Usage:
#   run-tests.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR   Where the toolchain is installed.
#                          [default: $HOME/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE
#                          [default: x86_64-elfsysvnt-linux-gnu]
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 all pass, 1 a check failed, 2 usage, 77 no toolchain or no python3.

set -u

prog=run-tests
here=$(cd "$(dirname "$0")" && pwd)
comp=$(cd "$here/.." && pwd)
root=$(cd "$comp/../.." && pwd)
prefix=${RUN_TESTS_PREFIX:-$HOME/x-elfsysvnt}
target=${RUN_TESTS_TARGET:-x86_64-elfsysvnt-linux-gnu}
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	opt=$1; shift
	val=
	case $opt in --*=*) val=${opt#*=}; opt=${opt%%=*} ;; esac
	case $opt in
		-P|--prefix|-T|--target)
			[ -n "$val" ] || { [ $# -gt 0 ] || { printf '%s: %s wants a value\n' "$prog" "$opt" >&2; exit 2; }; val=$1; shift; } ;;
	esac
	case $opt in
		-P|--prefix) prefix=$val ;;
		-T|--target) target=$val ;;
		-q|--quiet) quiet=1 ;;
		-h|--help) usage; exit 0 ;;
		--) break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$opt" >&2; exit 2 ;;
		*) break ;;
	esac
done

smon=/c/-/repo/session-monitor/lib/smon.sh
[ -f "$smon" ] && . "$smon"
type smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_step_skip() { :; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

smon_session build wp54-companions
smon_plan build surface ladder needed leftover fuzz

readelf=$prefix/bin/$target-readelf
as=$prefix/bin/$target-as
ld=$prefix/bin/$target-ld
if ! command -v python3 >/dev/null 2>&1 || [ ! -x "$readelf" ]; then
	smon_step_skip build
	smon_item WP-54 partial "no python3 or no binutils under $prefix; skipped"
	smon_end 77
	say "no python3 or no $readelf; skipping (77)"
	exit 77
fi

map=$root/veneer/version-map/glibc-version-map.tsv
nodes=$root/veneer/version-map/glibc-version-nodes.tsv
libraries=$root/veneer/version-map/libraries.tsv
cls=$root/veneer/classification/classification.tsv
tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

sonames=$(awk -F'\t' '/^[^#]/ && $1 != "libc.so.6" { print $1 }' "$libraries")

# --- build -------------------------------------------------------------------
smon_step_start build
"$comp/../libc/build-libc" -P "$prefix" -T "$target" -B "$tmp/libc" -q \
	|| { smon_step_fail build 1; fail 'build-libc failed'; }
"$comp/build-companions" -P "$prefix" -T "$target" -B "$tmp/companions" -q \
	|| { smon_step_fail build 1; fail 'build-companions failed'; }
tree="$tmp/libc/libc.so.6"
n=0
for soname in $sonames; do
	stem=${soname%%.so.*}
	so=$tmp/companions/$stem/$soname
	[ -f "$so" ] || { smon_step_fail build 1; fail "no $so"; }
	tree="$tree $so"
	n=$((n + 1))
done
[ "$n" = 8 ] || { smon_step_fail build 1; fail "$n companions built, want 8"; }
say "build: libc.so.6 and $n companions"
smon_step_ok build

# --- surface -----------------------------------------------------------------
smon_step_start surface
total=0
for soname in $sonames; do
	stem=${soname%%.so.*}
	so=$tmp/companions/$stem/$soname
	"$readelf" --dyn-syms -W "$so" | awk '
		$1 ~ /^[0-9]+:$/ && $NF != "" && NF >= 8 {
			print $NF"\t"$5 }' > "$tmp/dynsym.tsv"
	awk -F'\t' -v s="$soname" '$1==s && $4=="scaffold"{print $2}' "$cls" \
		| sort > "$tmp/scaffold.txt"
	awk -F'\t' -v s="$soname" 'NR==FNR{sc[$1]=1; next}
		$1==s && !($2 in sc) { print $2 (($4=="default") ? "@@" : "@") $3 }' \
		"$tmp/scaffold.txt" "$map" | sort > "$tmp/want.txt"
	awk -F'\t' 'NR==FNR{sc[$1]=1; next}
		{ n=$1; sub(/@@?[^@]*$/, "", n); if (!(n in sc)) print $1 }' \
		"$tmp/scaffold.txt" "$tmp/dynsym.tsv" | sort > "$tmp/have.txt"
	if ! diff "$tmp/want.txt" "$tmp/have.txt" > "$tmp/surface.diff" 2>&1; then
		smon_step_fail surface 1
		printf '%s: %s exported surface is not the map:\n' "$prog" "$soname" >&2
		head -20 "$tmp/surface.diff" >&2
		fail "$(wc -l < "$tmp/surface.diff" | tr -d ' ') lines of difference"
	fi
	total=$((total + $(wc -l < "$tmp/want.txt")))
done
say "surface: $total mapped symbols exact across the eight companions"
smon_step_ok surface

# --- ladder ------------------------------------------------------------------
smon_step_start ladder
lnodes=0
for soname in $sonames; do
	stem=${soname%%.so.*}
	so=$tmp/companions/$stem/$soname
	awk -F'\t' -v s="$soname" '$1==s {print $3"\t"$5}' "$nodes" \
		> "$tmp/vendor-ladder.tsv"
	python3 "$comp/../libc/provides.py" --ladder "$so" > "$tmp/ours-ladder.tsv" \
		|| { smon_step_fail ladder 1; fail "cannot read $soname's verdef back"; }
	if ! diff "$tmp/vendor-ladder.tsv" "$tmp/ours-ladder.tsv" \
			> "$tmp/ladder.diff" 2>&1; then
		smon_step_fail ladder 1
		head -20 "$tmp/ladder.diff" >&2
		fail "$soname's node ladder is not the vendor node list"
	fi
	lnodes=$((lnodes + $(wc -l < "$tmp/ours-ladder.tsv")))
done
say "ladder: $lnodes nodes across eight ladders, names and parents identical"
smon_step_ok ladder

# --- needed ------------------------------------------------------------------
smon_step_start needed
pairs="libc.so.6=$tmp/libc/libc-forward.tsv"
for soname in $sonames; do
	stem=${soname%%.so.*}
	pairs="$pairs $soname=$tmp/companions/$stem/$stem-forward.tsv"
done
# shellcheck disable=SC2086
python3 "$here/mk-standin.py" --out "$tmp/standin.s" $pairs \
	> "$tmp/picks.tsv" || { smon_step_fail needed 1; fail 'mk-standin failed'; }
"$as" --64 -o "$tmp/standin.o" "$tmp/standin.s" \
	|| { smon_step_fail needed 1; fail 'stand-in assembly failed'; }
# libc last, as a real link line has it; --no-as-needed records every library.
compsos=$(printf '%s' "$tree" | cut -d' ' -f2-)
# shellcheck disable=SC2086
"$ld" -o "$tmp/standin" --no-as-needed \
	-dynamic-linker /lib64/ld-linux-x86-64.so.2 \
	"$tmp/standin.o" $compsos "$tmp/libc/libc.so.6" \
	|| { smon_step_fail needed 1; fail 'stand-in link failed'; }
# shellcheck disable=SC2086
python3 "$comp/elfneeds.py" "$tmp/standin" --tree $tree > "$tmp/needed.tsv" \
	|| { smon_step_fail needed 1
		cat "$tmp/needed.tsv" >&2
		fail 'a requirement was left over'; }
nneed=$(awk -F'\t' '$1=="NEEDED"' "$tmp/needed.tsv" | wc -l | tr -d ' ')
[ "$nneed" = 9 ] || { smon_step_fail needed 1
	fail "stand-in carries $nneed DT_NEEDED entries, want 9"; }
for soname in libc.so.6 $sonames; do
	awk -F'\t' -v s="$soname" '$1=="NEEDED" && $2==s' "$tmp/needed.tsv" \
		| grep -q satisfied || { smon_step_fail needed 1
			fail "DT_NEEDED $soname is not satisfied from the tree"; }
	awk -F'\t' -v s="$soname" '$1=="VERNEED" && $2==s' "$tmp/needed.tsv" \
		| grep -q satisfied || { smon_step_fail needed 1
			fail "no satisfied verneed against $soname"; }
done
grep -q 'left over' "$tmp/needed.tsv" && { smon_step_fail needed 1
	fail 'a requirement is left over'; }
nver=$(awk -F'\t' '$1=="VERNEED"' "$tmp/needed.tsv" | wc -l | tr -d ' ')
say "needed: 9 DT_NEEDED and $nver verneed pairs, all satisfied, none left over"
smon_step_ok needed

# --- leftover ----------------------------------------------------------------
smon_step_start leftover
# shellcheck disable=SC2086
short=$(printf '%s\n' $tree | grep -v 'libnsl' | tr '\n' ' ')
# shellcheck disable=SC2086
if python3 "$comp/elfneeds.py" "$tmp/standin" --tree $short \
		> "$tmp/short.tsv" 2>/dev/null; then
	smon_step_fail leftover 1
	fail 'the checker passed a tree with libnsl.so.1 withheld'
fi
grep -q "NEEDED	libnsl.so.1	left over" "$tmp/short.tsv" \
	|| { smon_step_fail leftover 1
		fail 'the withheld library is not reported left over'; }
say "leftover: withholding libnsl.so.1 is caught and named"
smon_step_ok leftover

# --- fuzz --------------------------------------------------------------------
smon_step_start fuzz
python3 "$here/fuzz-elfneeds.py" "$tmp/standin" > "$tmp/fuzz.log" 2>&1 \
	|| { smon_step_fail fuzz 1; tail -25 "$tmp/fuzz.log" >&2
		fail 'elfneeds.py crashed on a mutant'; }
say "fuzz: $(head -1 "$tmp/fuzz.log")"
smon_step_ok fuzz

smon_item WP-54 ok "companions certified: surface, ladder, needed, leftover, fuzz"
smon_end 0
say 'all checks passed'
exit 0
