#!/usr/bin/env bash
#
# WP-20: the elfsysv1.dll export inventory.
#
# Reads winsup/cygwin/cygwin.din and writes one tab-separated row per export:
# the name, whether it is data or a function, the signal-frame class a function
# carries (SIGFE, NOSIGFE, SIGFE_MAYBE), and the target it aliases when it is an
# alias. This one list is what WP-21's down-call wrappers and WP-51's version map
# both read, so it is generated rather than hand-kept, and the generation is
# deterministic -- source order, no timestamp -- so a rerun reproduces it byte
# for byte. The reproduce test in t/ is what holds that guarantee.
#
# Source of record is cygwin.din at the ref DR-0007 names (Cygwin 3.6.10,
# newlib-cygwin b11613e47). The default --din points at that checkout; the
# reproduce test pins the ref so a moved checkout fails loudly rather than
# regenerating a different list.
#
# Columns, tab-separated, no header row (the README carries the legend):
#   name   kind(data|func)   sigfe(SIGFE|NOSIGFE|SIGFE_MAYBE|none|-)   alias(target|-)
# sigfe is '-' for data and 'none' for a function the .din leaves unannotated
# (glob_pattern_p is the one such today); an alias may be attached ('n= t') or
# spaced ('n = t').
#
# Usage:
#   extract-exports.sh [options]
#
# Options:
#   -d FILE, --din=FILE     cygwin.din to read.
#                           [default: /c/-/repo/newlib-cygwin/winsup/cygwin/cygwin.din]
#   -o FILE, --output=FILE  Destination; - is stdout. [default: -]
#   -t, --terse             Print the counts alone, one key=value per line.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as EXPORT_INV_<OPTION>, and the option wins.

set -u

prog=extract-exports
release='extract-exports 1.0'

din=${EXPORT_INV_DIN:-/c/-/repo/newlib-cygwin/winsup/cygwin/cygwin.din}
output=${EXPORT_INV_OUTPUT:--}
terse=${EXPORT_INV_TERSE:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-d|--din)     din=${2:-}; shift 2 ;;
		--din=*)      din=${1#*=}; shift ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-t|--terse)   terse=1; shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
[ -f "$din" ] || die "no cygwin.din at $din"

# The parse. Everything before EXPORTS is the LIBRARY line and blanks; after it,
# each non-blank, non-comment line is one export. A trailing '# ...' comment is
# stripped first, so the annotation is the last remaining token when there is
# one. An alias is 'name = target ANN' (the = may be attached to the name); a
# plain export is 'name ANN'; a bare 'name' is a function the .din left
# unannotated. Any other last token is a format we did not expect, and the run
# fails on it rather than guessing, because a silently dropped export is a symbol
# that links and then is absent at run time.
rows=$(awk '
	/^EXPORTS[ \t]*$/ { seen = 1; next }
	!seen { next }
	{ sub(/[ \t]*#.*$/, "") }          # strip trailing comment
	{ gsub(/[ \t]+$/, "") }            # and trailing space
	/^[ \t]*$/ { next }                # blank or comment-only line
	{
		line = $0
		gsub(/=/, " = ", line)     # normalise an attached alias =
		n = split(line, a, " ")
		name = a[1]
		if (a[2] == "=") { alias = a[3]; ann = (n >= 4) ? a[4] : "" }
		else             { alias = "-";  ann = (n >= 2) ? a[2] : "" }
		if (ann == "DATA")                                            { kind = "data"; sig = "-" }
		else if (ann == "SIGFE" || ann == "NOSIGFE" || ann == "SIGFE_MAYBE") { kind = "func"; sig = ann }
		else if (ann == "")                                          { kind = "func"; sig = "none" }
		else {
			printf "%s: line %d: unexpected annotation %s in: %s\n", "'"$prog"'", NR, ann, $0 > "/dev/stderr"
			bad = 1
			next
		}
		printf "%s\t%s\t%s\t%s\n", name, kind, sig, alias
	}
	END { if (bad) exit 3 }
' "$din") || die "cygwin.din carried an export this parser did not recognize"

emit() {
	if [ "$output" = - ]; then printf '%s\n' "$rows"
	else printf '%s\n' "$rows" > "$output" || die "cannot write $output"
	fi
}

if [ "$terse" = 1 ]; then
	printf '%s\n' "$rows" | awk -F'\t' '
		{ total++ }
		$2 == "data" { d++ }
		$2 == "func" { fn++ }
		$3 == "SIGFE" { sf++ }
		$3 == "NOSIGFE" { nf++ }
		$3 == "SIGFE_MAYBE" { mb++ }
		$3 == "none" { nn++ }
		$4 != "-" { al++ }
		END {
			printf "total=%d\n", total
			printf "data=%d\n", d
			printf "func=%d\n", fn
			printf "sigfe=%d\n", sf
			printf "nosigfe=%d\n", nf
			printf "sigfe_maybe=%d\n", mb
			printf "unannotated=%d\n", nn
			printf "aliases=%d\n", al
		}'
else
	emit
fi
