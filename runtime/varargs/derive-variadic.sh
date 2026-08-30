#!/usr/bin/env bash
#
# WP-24: derive and certify the variadic export set.
#
# The .din the export inventory is cut from does not mark which exports are
# variadic -- it carries names and signal-frame classes, not signatures. So the
# set is derived the only place the shape lives: the C prototype. This tool
# treats variadic-exports.tsv as the maintained record of that derivation and
# certifies two things about it against the committed export inventory:
#
#   * every name it lists is really an exported function (a row in
#     cygwin-exports.tsv with kind func); a typo or a dropped export fails here
#     rather than as a missing veneer symbol at link time;
#   * the three names a loose signature scan would sweep in but that are not in
#     fact variadic -- __eprintf (four fixed arguments), shmctl and msgctl
#     (a trailing struct pointer, not an ellipsis) -- are absent, so the record
#     documents its own exclusions.
#
# With --headers it also scans the Cygwin headers at the runtime base and flags
# any exported name it finds declared with a trailing ... or a va_list that the
# record does not list. That scan misses multi-line and macro-built prototypes,
# so it is an aid to maintenance rather than the source of record; the
# maintained list is. The method, in a sentence: a function is in the set when
# its C prototype's last parameter is an ellipsis (the format and sentinel
# variadics) or a va_list (the v-forms), and the set is that predicate
# intersected with the export inventory.
#
# Usage:
#   derive-variadic.sh [options]
#
# Options:
#   -e FILE, --exports=FILE    variadic enumeration TSV. [default: variadic-exports.tsv]
#   -x FILE, --inventory=FILE  export inventory TSV. [default: ../exports/cygwin-exports.tsv]
#   --headers[=DIR]            cross-check against Cygwin headers.
#                              [default dir: /c/-/cygwin/root/usr/include]
#   -t, --terse                counts only, one key=value per line.
#   -q, --quiet                errors only.
#   -V, --version              print the version and exit.
#   -h, --help                 print this message and exit.
#
# Exit: 0 the set is consistent, 1 it is not, 2 usage.

set -u

prog=derive-variadic
release='derive-variadic 1.0'
here=$(cd "$(dirname "$0")" && pwd)

enum=$here/variadic-exports.tsv
inv=$here/../exports/cygwin-exports.tsv
headers=
headers_dir=/c/-/cygwin/root/usr/include
terse=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)     usage; exit 0 ;;
		-V|--version)  printf '%s\n' "$release"; exit 0 ;;
		-e|--exports)  enum=${2:-}; shift 2 ;;
		--exports=*)   enum=${1#*=}; shift ;;
		-x|--inventory) inv=${2:-}; shift 2 ;;
		--inventory=*) inv=${1#*=}; shift ;;
		--headers)     headers=1; shift ;;
		--headers=*)   headers=1; headers_dir=${1#*=}; shift ;;
		-t|--terse)    terse=1; shift ;;
		-q|--quiet)    quiet=1; shift ;;
		-?*)           printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)             break ;;
	esac
done
[ -f "$enum" ] || fail "no enumeration at $enum"
[ -f "$inv" ] || fail "no export inventory at $inv"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

# the enumeration, validated for shape as it is read
awk -F'\t' -v prog="$prog" '
	NF != 4 { printf "%s: line %d: want 4 fields, got %d\n", prog, NR, NF > "/dev/stderr"; bad=1; next }
	$2 != "ellipsis" && $2 != "valist" { printf "%s: line %d: bad kind %s\n", prog, NR, $2 > "/dev/stderr"; bad=1 }
	seen[$1]++ { printf "%s: duplicate export %s\n", prog, $1 > "/dev/stderr"; bad=1 }
	END { if (bad) exit 1 }
' "$enum" || fail "the enumeration is malformed"

cut -f1 "$enum" | sort -u > "$tmp/enum.txt"
# exported functions only (kind == func)
awk -F'\t' '$2 == "func" { print $1 }' "$inv" | sort -u > "$tmp/funcs.txt"

# 1. every enumerated name is an exported function
missing=$(comm -23 "$tmp/enum.txt" "$tmp/funcs.txt")
[ -z "$missing" ] || fail "enumerated names absent from the export inventory (or not functions):
$missing"
say "every enumerated export is a function in the inventory"

# 2. the documented non-variadic exclusions are absent
for n in __eprintf shmctl msgctl; do
	if grep -qxF "$n" "$tmp/enum.txt"; then
		fail "$n is not variadic (it has no ellipsis) and must not be enumerated"
	fi
done
say "the non-variadic exclusions (__eprintf, shmctl, msgctl) are absent"

# 3. optional header cross-check
if [ -n "$headers" ]; then
	[ -d "$headers_dir" ] || fail "no headers at $headers_dir"
	{
		grep -rhoE '[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\([^;{)]*\.\.\.[[:space:]]*\)' "$headers_dir" 2>/dev/null
		grep -rhoE '[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\([^;{)]*va_list[^;{)]*\)' "$headers_dir" 2>/dev/null
	} | grep -oE '^[A-Za-z_][A-Za-z0-9_]*' | sort -u > "$tmp/hdr.txt"
	# exported, header-declared-variadic, and not enumerated: names to review
	comm -12 "$tmp/hdr.txt" "$tmp/funcs.txt" > "$tmp/hdr_exported.txt"
	review=$(comm -23 "$tmp/hdr_exported.txt" "$tmp/enum.txt" | grep -vxF -e __eprintf -e shmctl -e msgctl || true)
	if [ -n "$review" ]; then
		say "header scan flags exported variadic-shaped names not enumerated (review):"
		printf '%s\n' "$review" | sed 's/^/  /' >&2
	else
		say "header cross-check adds nothing the enumeration is missing"
	fi
fi

total=$(wc -l < "$tmp/enum.txt" | tr -d ' ')
fmt=$(awk -F'\t' '$3 != "PROTOTYPE"' "$enum" | wc -l | tr -d ' ')
proto=$(awk -F'\t' '$3 == "PROTOTYPE"' "$enum" | wc -l | tr -d ' ')
ellip=$(awk -F'\t' '$2 == "ellipsis"' "$enum" | wc -l | tr -d ' ')
valist=$(awk -F'\t' '$2 == "valist"' "$enum" | wc -l | tr -d ' ')

if [ "$terse" = 1 ]; then
	printf 'total\t%s\n' "$total"
	printf 'format\t%s\n' "$fmt"
	printf 'prototype\t%s\n' "$proto"
	printf 'ellipsis\t%s\n' "$ellip"
	printf 'valist\t%s\n' "$valist"
else
	say "consistent: $total variadic exports ($fmt format-driven, $proto prototype-driven; $ellip by ellipsis, $valist by va_list)"
fi
exit 0
