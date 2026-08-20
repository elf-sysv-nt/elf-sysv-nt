#!/usr/bin/env bash
#
# How many el8 packages mishandle a nonstandard vendor field?
#
# The target triple has four fields and only two carry weight. config.sub
# passes an unrecognized vendor through untouched, so x86_64-elfsysvnt-linux-gnu
# puts the project's name where it costs least and leaves linux-gnu standing
# where configure looks. The residual cost is the package that matches a
# literal *-pc-linux-gnu or *-unknown-linux-gnu and misses silently, taking a
# branch nobody intended. That cost is a number, and this takes it.
#
# Two probes over one source tree. Each package's own config.sub is fed three
# triples and its verdict recorded; the same walk greps for literal vendor
# matches in the places a host test lives. Nothing is built and nothing is
# installed.
#
# Usage:
#   count-vendor-misses.sh [options]
#
# Options:
#   -o FILE, --output=FILE     Transcript destination; - is stdout. [default: -]
#   -r DIR, --root=DIR         Unpacked el8 source tree to walk.
#   -f FILE, --dump=FILE       Classify a recorded probe instead of walking.
#   -k FILE, --keep-dump=FILE  Write the raw probe here as the walk runs.
#   -m T, --masquerade=T       [default: x86_64-pc-linux-gnu]
#   -c T, --candidate=T        [default: x86_64-elfsysvnt-linux-gnu]
#   -x T, --control=T          [default: x86_64-pc-elfsysvnt]
#   -n N, --top=N              Rows per ranking. [default: 15]
#   -t, --terse                The summary block alone, one key=value per line.
#   -q, --quiet                Errors only. Only useful with --output.
#   -v, --verbose              Name every offending package, not just the count.
#   -d, --debug                Trace execution; implies --verbose.
#   -V, --version              Print the version and exit.
#   -h, --help                 Print this message and exit.
#
# Each option is also settable as COUNT_VENDOR_MISSES_<OPTION>, and the option
# wins over the variable.

set -u

prog=count-vendor-misses
release='count-vendor-misses 1.0'

output=${COUNT_VENDOR_MISSES_OUTPUT:--}
root=${COUNT_VENDOR_MISSES_ROOT:-}
dump=${COUNT_VENDOR_MISSES_DUMP:-}
keep=${COUNT_VENDOR_MISSES_KEEP_DUMP:-}
masq=${COUNT_VENDOR_MISSES_MASQUERADE:-x86_64-pc-linux-gnu}
cand=${COUNT_VENDOR_MISSES_CANDIDATE:-x86_64-elfsysvnt-linux-gnu}
ctrl=${COUNT_VENDOR_MISSES_CONTROL:-x86_64-pc-elfsysvnt}
top=${COUNT_VENDOR_MISSES_TOP:-15}
terse=${COUNT_VENDOR_MISSES_TERSE:-0}
quiet=${COUNT_VENDOR_MISSES_QUIET:-0}
verbose=${COUNT_VENDOR_MISSES_VERBOSE:-0}
debug=${COUNT_VENDOR_MISSES_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)       usage; exit 0 ;;
		-V|--version)    printf '%s\n' "$release"; exit 0 ;;
		-o|--output)     output=${2:-}; shift 2 ;;
		--output=*)      output=${1#*=}; shift ;;
		-r|--root)       root=${2:-}; shift 2 ;;
		--root=*)        root=${1#*=}; shift ;;
		-f|--dump)       dump=${2:-}; shift 2 ;;
		--dump=*)        dump=${1#*=}; shift ;;
		-k|--keep-dump)  keep=${2:-}; shift 2 ;;
		--keep-dump=*)   keep=${1#*=}; shift ;;
		-m|--masquerade) masq=${2:-}; shift 2 ;;
		--masquerade=*)  masq=${1#*=}; shift ;;
		-c|--candidate)  cand=${2:-}; shift 2 ;;
		--candidate=*)   cand=${1#*=}; shift ;;
		-x|--control)    ctrl=${2:-}; shift 2 ;;
		--control=*)     ctrl=${1#*=}; shift ;;
		-n|--top)        top=${2:-}; shift 2 ;;
		--top=*)         top=${1#*=}; shift ;;
		-t|--terse)      terse=1; shift ;;
		-q|--quiet)      quiet=1; shift ;;
		-v|--verbose)    verbose=$((verbose + 1)); shift ;;
		-d|--debug)      debug=1; verbose=$((verbose + 1)); shift ;;
		--)              shift; break ;;
		-?*)             printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)               break ;;
	esac
done

[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

case $top in
	'' | *[!0-9]*) printf '%s: --top wants a number, got %s\n' "$prog" "$top" >&2; exit 2 ;;
esac
[ "$top" -gt 0 ] || { printf '%s: --top wants a positive number\n' "$prog" >&2; exit 2; }

[ -n "$root" ] || [ -n "$dump" ] || {
	printf '%s: nothing to read. Give --root DIR or --dump FILE.\n' "$prog" >&2
	exit 2
}
[ -z "$root" ] || [ -z "$dump" ] || die '--root and --dump are alternatives, not a pair'

[ "$debug" = 1 ] && set -x

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM

# config.sub and config.guess carry -pc- and -unknown- by their nature; they
# are the machinery, not a package's opinion about it, so the literal grep
# steps over them. config.log is a build artifact and says nothing about
# intent either.
lit_re='-(pc|unknown)-linux-gnu'
lit_skip='--exclude=config.sub --exclude=config.guess --exclude=config.log'

probe_one_sub() {
	pkg=$1 sub=$2
	for pair in "masquerade $masq" "candidate $cand" "control $ctrl"; do
		set -- $pair
		out=$(sh "$sub" "$2" 2>/dev/null)
		st=$?
		[ -n "$out" ] || out='(none)'
		printf 'SUB\t%s\t%s\t%s\t%d\t%s\n' "$pkg" "$1" "$2" "$st" "$out"
	done
}

walk() {
	command -v sh >/dev/null 2>&1 || die 'no sh on PATH'
	[ -d "$root" ] || die "no such source tree: $root"
	note "walking $root"
	find "$root" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' |
		LC_ALL=C sort |
		while read -r pkg; do
			printf 'PKG\t%s\n' "$pkg"
			find "$root/$pkg" -name config.sub -type f 2>/dev/null |
				LC_ALL=C sort |
				while read -r sub; do
					probe_one_sub "$pkg" "$sub"
				done
			# shellcheck disable=SC2086
			grep -RIlE $lit_skip -e "$lit_re" "$root/$pkg" 2>/dev/null |
				LC_ALL=C sort |
				while read -r hit; do
					printf 'LIT\t%s\t%s\n' "$pkg" "${hit#$root/$pkg/}"
				done
		done
}

if [ -n "$dump" ]; then
	[ -r "$dump" ] || die "cannot read the dump: $dump"
	source_of_truth="dump $dump"
	cp "$dump" "$work/dump" || die "cannot copy $dump"
else
	source_of_truth="source tree $root"
	walk > "$work/dump" || die 'the walk failed'
	if [ -n "$keep" ]; then
		cat "$work/dump" > "$keep" || die "cannot write $keep"
		note "raw probe kept in $keep"
	fi
fi
[ -s "$work/dump" ] || die 'nothing to classify'

note 'classifying'
awk -v sumfile="$work/summary" -v offfile="$work/offenders" \
	-v litfile="$work/literals" '
BEGIN { FS = "\t" }

$1 == "PKG" { pkg = $2; npkg++; next }

# A verdict per config.sub, per triple. Three shapes: refused outright,
# accepted but rewritten into something else, accepted unchanged. Only the
# candidate row decides anything; the masquerade is the baseline the counts
# are read against and the control exists to price a rejection that is
# expected rather than feared.
$1 == "SUB" {
	label = $3; triple = $4; status = $5 + 0; canon = $6
	if (label == "masquerade") { subs[pkg] = 1; nsub++ }
	if (status != 0)          verdict = "rejected"
	else if (canon != triple) verdict = "rewritten"
	else                      verdict = "accepted"
	tally[label, verdict]++
	if (label == "candidate" && verdict != "accepted") {
		bad[pkg] = verdict
		if (canon != "" && verdict == "rewritten")
			rewrite[canon]++
	}
	next
}

$1 == "LIT" { lit[pkg] = lit[pkg] + 1; litfiles[$3]++; next }

function pct(a, b) { return b ? sprintf("%.1f%%", 100 * a / b) : "n/a" }

function row(label,    r, w, a) {
	r = tally[label, "rejected"] + 0
	w = tally[label, "rewritten"] + 0
	a = tally[label, "accepted"] + 0
	printf "    %-11s %9d %9d %9d %9d %9s\n", label, r + w + a, a, w, r,
		pct(r + w, r + w + a)
}

END {
	for (p in subs) nsubpkg++
	for (p in bad) nbad++
	for (p in lit) { nlit++; if (p in bad) noverlap++ }
	for (p in bad) affected[p] = 1
	for (p in lit) affected[p] = 1
	for (p in affected) naff++

	printf "== config.sub verdicts, one row per file probed\n\n"
	printf "    %-11s %9s %9s %9s %9s %9s\n", "", "probed", "accepted", "rewritten", "rejected", "miss"
	row("masquerade")
	row("candidate")
	row("control")
	printf "\n    Accepted means the triple came back byte for byte. Rewritten means\n"
	printf "    config.sub canonicalized it into something else, which is a silent\n"
	printf "    miss and the shape that matters. The control is expected to be\n"
	printf "    rejected outright; a row that is not tells you the os field is\n"
	printf "    cheaper than assumed.\n"

	printf "\n== packages\n\n"
	printf "    seen                        %6d\n", npkg
	printf "    carrying a config.sub       %6d   %s of seen\n", nsubpkg + 0, pct(nsubpkg + 0, npkg)
	printf "    candidate not accepted      %6d   %s of those carrying one\n", nbad + 0, pct(nbad + 0, nsubpkg + 0)
	printf "    literal vendor in a source  %6d   %s of seen\n", nlit + 0, pct(nlit + 0, npkg)
	printf "    both                        %6d\n", noverlap + 0
	printf "    affected either way         %6d   %s of seen\n", naff + 0, pct(naff + 0, npkg)
	printf "\n    The literal count is an upper bound. The pattern matches a comment\n"
	printf "    and a live host test alike, so a nonzero figure wants reading\n"
	printf "    before it is quoted.\n"

	for (p in bad) print bad[p], p > offfile
	for (f in litfiles) print litfiles[f], f > litfile

	print "packages_seen=" npkg + 0 > sumfile
	print "packages_with_config_sub=" nsubpkg + 0 > sumfile
	print "config_sub_probed=" nsub + 0 > sumfile
	print "masquerade_accepted=" tally["masquerade", "accepted"] + 0 > sumfile
	print "masquerade_rewritten=" tally["masquerade", "rewritten"] + 0 > sumfile
	print "masquerade_rejected=" tally["masquerade", "rejected"] + 0 > sumfile
	print "candidate_accepted=" tally["candidate", "accepted"] + 0 > sumfile
	print "candidate_rewritten=" tally["candidate", "rewritten"] + 0 > sumfile
	print "candidate_rejected=" tally["candidate", "rejected"] + 0 > sumfile
	print "control_accepted=" tally["control", "accepted"] + 0 > sumfile
	print "control_rewritten=" tally["control", "rewritten"] + 0 > sumfile
	print "control_rejected=" tally["control", "rejected"] + 0 > sumfile
	print "packages_candidate_miss=" nbad + 0 > sumfile
	print "packages_literal_vendor=" nlit + 0 > sumfile
	print "packages_affected=" naff + 0 > sumfile
	print "affected_share=" pct(naff + 0, npkg) > sumfile
}
' "$work/dump" > "$work/body" || die 'the classifier failed'

[ -s "$work/summary" ] || die 'the classifier produced no summary'

ranking() {
	if [ ! -s "$1" ]; then
		printf '    nothing counted\n'
		return
	fi
	LC_ALL=C sort -k1,1rn -k2,2 "$1" | head -n "$top" |
		awk '{ printf "    %-40s %8s\n", $2, $1 }'
}

# Offenders sort by verdict then name rather than by a count, because there is
# no count: a package either accepted the candidate or it did not.
offenders() {
	if [ ! -s "$1" ]; then
		printf '    none\n'
		return
	fi
	LC_ALL=C sort -k1,1 -k2,2 "$1" |
		awk '{ printf "    %-40s %s\n", $2, $1 }'
}

if [ "$terse" = 1 ]; then
	cat "$work/summary" > "$work/report"
else
	{
		printf 'el8 sources against a nonstandard vendor field\n\n'
		printf 'host        %s\n' "$(hostname 2>/dev/null)"
		printf 'kernel      %s\n' "$(uname -srm)"
		printf 'source      %s\n' "$source_of_truth"
		printf 'masquerade  %s\n' "$masq"
		printf 'candidate   %s\n' "$cand"
		printf 'control     %s\n' "$ctrl"
		printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf 'script      %s\n\n' "$release"
		cat "$work/body"
		if [ "$verbose" -gt 0 ]; then
			printf '\n== packages whose config.sub did not accept the candidate\n\n'
			offenders "$work/offenders"
			printf '\n== files carrying a literal vendor, by hits\n\n'
			ranking "$work/literals"
		fi
		printf '\n== summary\n\n'
		sed -e 's/^/    /' "$work/summary"
	} > "$work/report"
fi

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi
