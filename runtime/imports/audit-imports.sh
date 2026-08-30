#!/usr/bin/env bash
#
# WP-21: the link-map audit -- no System V-side path calls an import directly.
#
# The done-condition for the down-call wrappers is that the raw Windows imports
# are named in exactly one place, the generated wrappers unit, and nowhere else
# in the runtime. This checks that against real object files. For each object
# given, it reads the undefined symbols the object still needs from elsewhere
# and intersects them with the import inventory, counting both the bare name
# (NtClose) and the import slot (__imp_NtClose). Any object other than the
# wrappers unit that needs an import symbol is a direct down-call and fails the
# audit; the wrappers unit is expected to need every slot and is reported, not
# faulted.
#
# Run it over the runtime's objects once WP-22 has built them:
#   audit-imports.sh build/*.o
# It also runs standalone over just the wrappers object, which is the check
# available before the rest of the runtime exists: it confirms the wrappers are
# the sole namer of the imports, which is the property everything else must
# then preserve.
#
# For a final linked image the same rule reads off the link map: the only
# input object contributing references to the import slots is the wrappers
# unit. This script works on the objects because nm gives the per-object
# attribution a stripped image has lost; the README documents the map form.
#
# Usage:
#   audit-imports.sh [options] OBJECT...
#
# Options:
#   -i FILE, --inventory=FILE   import inventory TSV. [default: cygwin-imports.tsv]
#   -w NAME, --wrappers=NAME    basename of the sanctioned wrappers object.
#                               [default: wrappers.gen.o]
#   -q, --quiet                 Report violations only.
#   -h, --help                  Print this message and exit.
#
# Exit: 0 clean, 1 a direct down-call was found, 2 usage.

set -u

prog=audit-imports
here=$(cd "$(dirname "$0")" && pwd)
inventory=$here/cygwin-imports.tsv
wrappers=wrappers.gen.o
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
die()  { printf '%s: %s\n' "$prog" "$*" >&2; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)      usage; exit 0 ;;
		-i|--inventory) inventory=${2:-}; shift 2 ;;
		--inventory=*)  inventory=${1#*=}; shift ;;
		-w|--wrappers)  wrappers=${2:-}; shift 2 ;;
		--wrappers=*)   wrappers=${1#*=}; shift ;;
		-q|--quiet)     quiet=1; shift ;;
		--)             shift; break ;;
		-?*)            die "unknown option $1" ;;
		*)              break ;;
	esac
done
[ $# -ge 1 ] || die "no object files given"
[ -f "$inventory" ] || die "no inventory at $inventory"
command -v nm >/dev/null 2>&1 || die "nm (binutils) not on PATH"

# The import symbol set, one name per line, both spellings. Held in a temp file
# so grep -F -f can match against it without a shell round trip per symbol.
imps=$(mktemp "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'no temp file'
trap 'rm -f "$imps"' EXIT
awk -F'\t' '{ print $2; print "__imp_" $2 }' "$inventory" | sort -u > "$imps"

violations=0
sanctioned=0
for obj in "$@"; do
	[ -f "$obj" ] || { say "skip: no such object $obj"; continue; }
	# Undefined symbols this object still needs from elsewhere. nm marks them
	# U; strip the address column and the type letter to the bare name.
	undef=$(nm -u "$obj" 2>/dev/null | awk '{ print $NF }' | sort -u)
	hits=$(printf '%s\n' "$undef" | grep -Fxf "$imps" 2>/dev/null)
	[ -n "$hits" ] || { say "clean: $(basename "$obj") names no import"; continue; }
	n=$(printf '%s\n' "$hits" | grep -c .)
	if [ "$(basename "$obj")" = "$wrappers" ]; then
		sanctioned=$((sanctioned + n))
		say "sanctioned: $(basename "$obj") names $n import slots (the boundary)"
	else
		violations=$((violations + n))
		printf '%s: VIOLATION: %s calls %d import(s) directly:\n' "$prog" "$(basename "$obj")" "$n" >&2
		printf '%s\n' "$hits" | sed 's/^/    /' >&2
	fi
done

if [ "$violations" -gt 0 ]; then
	printf '%s: %d direct down-call(s) outside %s\n' "$prog" "$violations" "$wrappers" >&2
	exit 1
fi
say "audit clean: imports named only by $wrappers ($sanctioned slots)"
exit 0
