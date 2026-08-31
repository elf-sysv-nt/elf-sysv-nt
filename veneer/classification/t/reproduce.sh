#!/usr/bin/env bash
#
# WP-52's certification: the committed classification reproduces from the two
# inputs, it partitions the version map with nothing left out and nothing
# invented, every shim is flagged for review, no alias is classified less
# strictly than its target, and the published fourth-bucket document still
# states the numbers the generator produces.
#
# Five things are checked, each a pass or a fail rather than a reading.
#
#   reproduce   classify.py is rerun over veneer/version-map and
#               runtime/exports and its classification.tsv and
#               bucket4-inventory.tsv are diffed against the committed files.
#               A byte of difference fails.
#   partition   The committed classification covers the version map exactly:
#               one classified row per map row, the same (soname, symbol,
#               version) set, no row unclassified and none extra, every bucket
#               drawn from {1,2,3,4,scaffold}, and the five dispositions
#               mutually exclusive. This is the "no symbol unclassified" bar.
#   review      Every bucket-3 (shim) row carries the review flag: WP-52 makes
#               no semantic judgement it did not flag for a human.
#   strict      No bucket-1 forward targets a name whose own map row is a shim
#               or a stub. The alias-strictness invariant of the F2 redo: an
#               alias is never classified less strictly than its target.
#   doc         doc/what-the-veneer-lacks.md states the same bucket-4 total and
#               category counts the generator reports, so the published
#               document cannot drift from the data.
#
# Usage:
#   reproduce.sh [options]
#
# Options:
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 reproduces and partitions, 1 differs, 2 usage, 77 no python3.

set -u

prog=reproduce
here=$(cd "$(dirname "$0")" && pwd)
cl=$(cd "$here/.." && pwd)
root=$(cd "$cl/../.." && pwd)
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	opt=$1; shift
	case $opt in
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

smon_session build wp52-classification
smon_plan reproduce partition review strict doc

command -v python3 >/dev/null 2>&1 || {
	smon_step_skip reproduce
	smon_item WP-52 partial "no python3; skipped"
	smon_end 77
	say "no python3 on PATH; skipping (77)"
	exit 77
}

gen=$cl/classify.py
map=$root/veneer/version-map/glibc-version-map.tsv
cyg=$root/runtime/exports/cygwin-exports.tsv
committed=$cl/classification.tsv
committed_b4=$cl/bucket4-inventory.tsv
doc=$root/doc/what-the-veneer-lacks.md
for f in "$gen" "$map" "$cyg" "$committed" "$committed_b4" "$doc"; do
	[ -f "$f" ] || fail "missing input: $f"
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

# --- reproduce ---------------------------------------------------------------
smon_step_start reproduce
python3 "$gen" --map "$map" --cygwin "$cyg" --review "$cl/semantic-review.tsv" \
	-o "$tmp/cls.tsv" --bucket4 "$tmp/b4.tsv" 2>"$tmp/gen.err" \
	|| { smon_step_fail reproduce 1; cat "$tmp/gen.err" >&2; fail "generator failed"; }
ok=1
if ! diff -u "$committed" "$tmp/cls.tsv" >"$tmp/cls.diff" 2>&1; then
	ok=0; printf '%s: committed classification differs from a fresh run:\n' "$prog" >&2
	head -40 "$tmp/cls.diff" >&2
fi
if ! diff -u "$committed_b4" "$tmp/b4.tsv" >"$tmp/b4.diff" 2>&1; then
	ok=0; printf '%s: committed fourth-bucket inventory differs:\n' "$prog" >&2
	head -40 "$tmp/b4.diff" >&2
fi
[ "$ok" = 1 ] || { smon_step_fail reproduce 1; exit 1; }
say "reproduces: $(wc -l < "$committed" | tr -d ' ') classified rows, $(wc -l < "$committed_b4" | tr -d ' ') fourth-bucket rows"
smon_step_ok reproduce

# --- partition ---------------------------------------------------------------
smon_step_start partition
# the map key set and the classification key set must be identical
awk -F'\t' '{print $1"\t"$2"\t"$3}' "$map"       | sort > "$tmp/map.keys"
awk -F'\t' '{print $1"\t"$2"\t"$3}' "$committed" | sort > "$tmp/cls.keys"
if ! diff "$tmp/map.keys" "$tmp/cls.keys" >"$tmp/keys.diff" 2>&1; then
	smon_step_fail partition 1
	printf '%s: classification key set differs from the map:\n' "$prog" >&2
	head -20 "$tmp/keys.diff" >&2
	fail "the classification does not cover the map one-to-one"
fi
nmap=$(wc -l < "$map" | tr -d ' ')
ncls=$(wc -l < "$committed" | tr -d ' ')
[ "$nmap" = "$ncls" ] || { smon_step_fail partition 1
	fail "row counts differ: map $nmap, classification $ncls"; }
# every bucket value legal, and duplicate map keys carry one disposition each
bad=$(awk -F'\t' '$4!="1" && $4!="2" && $4!="3" && $4!="4" && $4!="scaffold"' "$committed")
[ -z "$bad" ] || { smon_step_fail partition 1
	fail "rows with an illegal bucket:\n$bad"; }
# a (soname,symbol) may repeat across versions, but must not straddle buckets
straddle=$(awk -F'\t' '{k=$1"\t"$2; if(k in b && b[k]!=$4) print k; b[k]=$4}' "$committed" | sort -u)
[ -z "$straddle" ] || { smon_step_fail partition 1
	fail "a symbol falls in two buckets:\n$straddle"; }
# counts, and their sum equals the whole map
read b1 b2 b3 b4 bs <<EOF
$(awk -F'\t' '{c[$4]++} END{print c["1"]+0, c["2"]+0, c["3"]+0, c["4"]+0, c["scaffold"]+0}' "$committed")
EOF
sum=$((b1 + b2 + b3 + b4 + bs))
[ "$sum" = "$nmap" ] || { smon_step_fail partition 1
	fail "bucket counts $b1+$b2+$b3+$b4+$bs=$sum do not sum to $nmap"; }
say "partition: $nmap map rows = b1 $b1 + b2 $b2 + b3 $b3 + b4 $b4 + scaffold $bs; key sets identical"
smon_step_ok partition

# --- review ------------------------------------------------------------------
smon_step_start review
unflagged=$(awk -F'\t' '$4=="3" && $7!="review"{print $2}' "$committed" | sort -u)
[ -z "$unflagged" ] || { smon_step_fail review 1
	fail "bucket-3 rows not flagged for review:\n$unflagged"; }
strayflag=$(awk -F'\t' '$4!="3" && $7=="review"{print $2}' "$committed" | sort -u)
[ -z "$strayflag" ] || { smon_step_fail review 1
	fail "review flag outside bucket 3:\n$strayflag"; }
say "review: all $b3 shim rows flagged for review, no stray flags"
smon_step_ok review

# --- strict ------------------------------------------------------------------
# The generator enforces the invariant by construction; this re-derives it
# from the committed file alone, so a hand-edited classification cannot pass.
smon_step_start strict
viol=$(awk -F'\t' 'NR==FNR { if ($4=="3" || $4=="4") s[$2]=$4; next }
	$4=="1" && ($6 in s) { print $2" -> "$6" (bucket "s[$6]")" }' \
	"$committed" "$committed" | sort -u)
[ -z "$viol" ] || { smon_step_fail strict 1
	fail "aliases classified less strictly than their target:\n$viol"; }
say "strict: no bucket-1 forward targets a shim or a stub"
smon_step_ok strict

# --- doc ---------------------------------------------------------------------
smon_step_start doc
python3 "$gen" --map "$map" --cygwin "$cyg" --review "$cl/semantic-review.tsv" \
	-o /dev/null --summary 2>"$tmp/sum.txt"
docfail=0
grep -q "fourth bucket holds $b4 " "$doc" || { docfail=1; say "doc missing bucket-4 total $b4"; }
# each category count in the summary must appear in the doc
while read -r cat n; do
	case $cat in public-absent|float-n-math|internal-helpers|stdio-internals|resolver-internals|underscore-internals|fast-math-no-base|pthread-internals|loader-internals|argp|fortify-chk-no-base)
		grep -Eq "$cat[^0-9]+$n\b|$n[^0-9]+$cat" "$doc" || { docfail=1; say "doc missing $cat=$n"; } ;;
	esac
done < <(awk '/^  [a-z].* [0-9]+$/{print $1, $2}' "$tmp/sum.txt")
[ "$docfail" = 0 ] || { smon_step_fail doc 1
	fail "doc/what-the-veneer-lacks.md has drifted from the generated counts"; }
say "doc: fourth-bucket total and category counts match the generator"
smon_step_ok doc

smon_item WP-52 met "classification reproduces, partitions the map, shims flagged, aliases strict, doc in sync"
smon_end 0
say "all five checks passed"
exit 0
