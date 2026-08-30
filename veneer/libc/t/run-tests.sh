#!/usr/bin/env bash
#
# WP-53's certification: the built libc.so.6 is the vendor's surface.
#
# The work package's exit criterion has two halves, and both are checked here
# against a library this script builds rather than against a recorded output.
#
#   build      build-libc runs clean from the committed map and classification.
#   bindings   memcpy@GLIBC_2.2.5 and memcpy@@GLIBC_2.14 both live in .dynsym,
#              at two addresses, with two version indices. That is the "bind
#              independently" half: one is the default binding a fresh link
#              picks up, the other is the compat binding an old object asks for
#              by name, and a library that collapsed them would satisfy an el8
#              binary's verneed while calling the wrong code.
#   surface    Every row of the version map for this soname is in .dynsym at
#              the node the map assigns it, with the map's own binding, and
#              nothing else is. The 29 version-node identity objects are
#              present as absolute objects, emitted by the linker rather than
#              by us (DR-0017).
#   ladder     The .gnu.version_d ladder read back out of the file equals the
#              vendor's node list, name and parent, in order.
#   elfdeps    The rpm provides the file yields reproduce spike 4's ladder
#              result: 30 lines, the soname provide plus one per node, all
#              written against the base verdef node, which is the soname.
#              Spike 4 ran el8's own elfdeps and recorded "identical, 30 lines
#              each" against the vendor. It needed a network, an el8 mirror and
#              a Linux host; the derivation is short enough to reimplement, so
#              this runs anywhere and asserts the same numbers.
#   archive    libc.a carries the bare-name default bindings and no versioned
#              symbol, since an archive has nowhere to put a version table.
#   fuzz       provides.py refuses mutated libraries rather than crashing on
#              them. It is the one thing here that reads a file it did not
#              write, and every offset in its walk comes out of that file.
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
libc=$(cd "$here/.." && pwd)
root=$(cd "$libc/../.." && pwd)
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

smon_session build wp53-libc
smon_plan build bindings surface ladder elfdeps archive fuzz

readelf=$prefix/bin/$target-readelf
nm=$prefix/bin/$target-nm
if ! command -v python3 >/dev/null 2>&1 || [ ! -x "$readelf" ]; then
	smon_step_skip build
	smon_item WP-53 partial "no python3 or no binutils under $prefix; skipped"
	smon_end 77
	say "no python3 or no $readelf; skipping (77)"
	exit 77
fi

soname=libc.so.6
map=$root/veneer/version-map/glibc-version-map.tsv
nodes=$root/veneer/version-map/glibc-version-nodes.tsv
tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

# --- build -------------------------------------------------------------------
smon_step_start build
"$libc/build-libc" -P "$prefix" -T "$target" -B "$tmp/build" -q \
	|| { smon_step_fail build 1; fail 'build-libc failed'; }
so=$tmp/build/$soname
ar=$tmp/build/libc.a
fwd=$tmp/build/libc-forward.tsv
for f in "$so" "$ar" "$fwd"; do
	[ -f "$f" ] || { smon_step_fail build 1; fail "build-libc produced no $f"; }
done
say "build: $soname and libc.a from $(wc -l < "$fwd" | tr -d ' ') mapped rows"
smon_step_ok build

# .dynsym as name, address, type, bind, section; versioned names kept whole.
"$readelf" --dyn-syms -W "$so" | awk '
	$1 ~ /^[0-9]+:$/ && $NF != "" && NF >= 8 {
		print $NF"\t"$2"\t"$4"\t"$5"\t"$7 }' > "$tmp/dynsym.tsv"

# --- bindings ----------------------------------------------------------------
smon_step_start bindings
compat=$(awk -F'\t' '$1=="memcpy@GLIBC_2.2.5"' "$tmp/dynsym.tsv")
default=$(awk -F'\t' '$1=="memcpy@@GLIBC_2.14"' "$tmp/dynsym.tsv")
[ -n "$compat" ]  || { smon_step_fail bindings 1; fail 'no memcpy@GLIBC_2.2.5'; }
[ -n "$default" ] || { smon_step_fail bindings 1; fail 'no memcpy@@GLIBC_2.14'; }
ca=$(printf '%s' "$compat"  | cut -f2)
da=$(printf '%s' "$default" | cut -f2)
[ "$ca" != "$da" ] || { smon_step_fail bindings 1
	fail "both memcpy bindings sit at $ca: they are one symbol, not two"; }
# the map says which is which; the file must not have swapped them
mdef=$(awk -F'\t' -v s="$soname" '$1==s && $2=="memcpy" && $4=="default"{print $3}' "$map")
mcom=$(awk -F'\t' -v s="$soname" '$1==s && $2=="memcpy" && $4!="default"{print $3}' "$map")
[ "$mdef" = GLIBC_2.14 ] && [ "$mcom" = GLIBC_2.2.5 ] || { smon_step_fail bindings 1
	fail "the map no longer puts memcpy default at 2.14 and compat at 2.2.5"; }
say "bindings: memcpy@GLIBC_2.2.5 at $ca, memcpy@@GLIBC_2.14 at $da"
smon_step_ok bindings

# --- surface -----------------------------------------------------------------
smon_step_start surface
# The scaffold rows are the version-node identity objects the linker emits out
# of the version script (DR-0017). They are in the map and they are in the file,
# but nothing in veneer/libc writes them, so they are set aside on both sides
# and counted separately below.
awk -F'\t' -v s="$soname" '$1==s && $4=="scaffold"{print $2}' \
	"$root/veneer/classification/classification.tsv" | sort > "$tmp/scaffold.txt"
# What the map says the file should carry, spelled the way readelf spells it.
awk -F'\t' -v s="$soname" 'NR==FNR{sc[$1]=1; next}
	$1==s && !($2 in sc) { print $2 (($4=="default") ? "@@" : "@") $3 }' \
	"$tmp/scaffold.txt" "$map" | sort > "$tmp/want.txt"
awk -F'\t' 'NR==FNR{sc[$1]=1; next}
	{ n=$1; sub(/@@?[^@]*$/, "", n); if (!(n in sc)) print $1 }' \
	"$tmp/scaffold.txt" "$tmp/dynsym.tsv" | sort > "$tmp/have.txt"
if ! diff "$tmp/want.txt" "$tmp/have.txt" > "$tmp/surface.diff" 2>&1; then
	smon_step_fail surface 1
	printf '%s: the exported surface is not the map:\n' "$prog" >&2
	head -20 "$tmp/surface.diff" >&2
	fail "$(wc -l < "$tmp/surface.diff" | tr -d ' ') lines of difference"
fi
nscaf=$(wc -l < "$tmp/scaffold.txt" | tr -d ' ')
nabs=$(awk -F'\t' 'NR==FNR{sc[$1]=1; next} $5=="ABS" && ($1 in sc)' \
	"$tmp/scaffold.txt" "$tmp/dynsym.tsv" | wc -l | tr -d ' ')
[ "$nabs" = "$nscaf" ] || { smon_step_fail surface 1
	fail "$nabs of $nscaf version-node identity objects present"; }
say "surface: $(wc -l < "$tmp/want.txt" | tr -d ' ') mapped symbols exact, $nscaf node objects from the linker"
smon_step_ok surface

# --- ladder ------------------------------------------------------------------
smon_step_start ladder
awk -F'\t' -v s="$soname" '$1==s {print $3"\t"$5}' "$nodes" > "$tmp/vendor-ladder.tsv"
python3 "$libc/provides.py" --ladder "$so" > "$tmp/ours-ladder.tsv" \
	|| { smon_step_fail ladder 1; fail 'cannot read the verdef ladder back'; }
if ! diff "$tmp/vendor-ladder.tsv" "$tmp/ours-ladder.tsv" > "$tmp/ladder.diff" 2>&1; then
	smon_step_fail ladder 1
	head -20 "$tmp/ladder.diff" >&2
	fail 'the emitted node ladder is not the vendor node list'
fi
say "ladder: $(wc -l < "$tmp/ours-ladder.tsv" | tr -d ' ') nodes, names and parents identical to the vendor"
smon_step_ok ladder

# --- elfdeps -----------------------------------------------------------------
smon_step_start elfdeps
python3 "$libc/provides.py" "$so" --check-soname "$soname" > "$tmp/provides.txt" \
	|| { smon_step_fail elfdeps 1
		fail 'DT_SONAME and the base verdef node disagree'; }
nnode=$(awk -F'\t' -v s="$soname" '$1==s && $4!="base"' "$nodes" | wc -l | tr -d ' ')
want=$((nnode + 1))
got=$(wc -l < "$tmp/provides.txt" | tr -d ' ')
[ "$got" = "$want" ] || { smon_step_fail elfdeps 1
	fail "provides has $got lines, want $want (soname plus $nnode nodes)"; }
grep -qx "$soname()(64bit)" "$tmp/provides.txt" || { smon_step_fail elfdeps 1
	fail "no $soname()(64bit) provide"; }
# spike 4's own two strings, quoted from results-2026-08-29.txt
for s in "libc.so.6(GLIBC_2.2.5)(64bit)" "libc.so.6(GLIBC_2.14)(64bit)"; do
	grep -qx "$s" "$tmp/provides.txt" || { smon_step_fail elfdeps 1
		fail "spike 4's string $s is not provided"; }
done
# every node in the vendor list is provided, and nothing beyond them is
awk -F'\t' -v s="$soname" '$1==s && $4!="base"{printf "%s(%s)(64bit)\n", s, $3}' \
	"$nodes" | sort > "$tmp/want-prov.txt"
grep -v "^$soname()(64bit)$" "$tmp/provides.txt" | sort > "$tmp/have-prov.txt"
if ! diff "$tmp/want-prov.txt" "$tmp/have-prov.txt" > "$tmp/prov.diff" 2>&1; then
	smon_step_fail elfdeps 1
	head -20 "$tmp/prov.diff" >&2
	fail 'the version provides are not the vendor node set'
fi
say "elfdeps: $got provides, spike 4 recorded 30 identical against the vendor"
smon_step_ok elfdeps

# --- archive -----------------------------------------------------------------
smon_step_start archive
"$readelf" --syms -W "$ar" | awk '$1 ~ /^[0-9]+:$/ && $5=="GLOBAL" || $5=="WEAK" {print $NF}' \
	| grep -c '@' > "$tmp/ar-versioned" 2>/dev/null || true
nver=$(tr -d ' \n' < "$tmp/ar-versioned")
[ "${nver:-0}" = 0 ] || { smon_step_fail archive 1
	fail "libc.a carries $nver versioned symbols; an archive has no version table"; }
"$readelf" --syms -W "$ar" | awk '$1 ~ /^[0-9]+:$/ && ($5=="GLOBAL" || $5=="WEAK") && $NF!="" {print $NF}' \
	| sort -u > "$tmp/ar-syms.txt"
for s in memcpy printf malloc; do
	grep -qx "$s" "$tmp/ar-syms.txt" || { smon_step_fail archive 1
		fail "libc.a does not define $s"; }
done
say "archive: $(wc -l < "$tmp/ar-syms.txt" | tr -d ' ') bare-name definitions, none versioned"
smon_step_ok archive

# --- fuzz --------------------------------------------------------------------
smon_step_start fuzz
# provides.py trusts nothing in the file it reads, and every offset in the walk
# comes out of the file. Truncations and single-byte flips at the header, the
# section header table and the two sections the walk crosses; a mutant that
# still parses is fine, an unhandled exception is not.
out=$(python3 "$here/fuzz-provides.py" "$so" 2>&1) || { smon_step_fail fuzz 1
	printf '%s\n' "$out" >&2; fail 'the reader crashed on a mutated library'; }
say "${out#fuzz-provides: }"
smon_step_ok fuzz

smon_item WP-53 met "libc.so.6 carries the vendor surface; both memcpy bindings independent; provides reproduce spike 4"
smon_end 0
say "all seven checks passed"
exit 0
