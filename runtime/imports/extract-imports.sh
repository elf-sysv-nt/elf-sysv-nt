#!/usr/bin/env bash
#
# WP-21: the elfsysv1.dll down-call import inventory.
#
# Reads the import table of a built cygwin1.dll and writes one tab-separated
# row per imported Windows function: the DLL it comes from, the symbol name,
# and whether the function is variadic. This is the list of every call the
# runtime makes DOWN into Windows; WP-21's wrapper generator turns each row
# into one ms_abi forwarding thunk, so the runtime never names an import
# directly. The generation is deterministic -- import-table order, no timestamp
# -- so a rerun reproduces it byte for byte, and the reproduce test in t/ holds
# that guarantee.
#
# Source of record is the built cygwin1.dll of the runtime base DR-0007 names
# (Cygwin 3.6.10). The import table is read with binutils objdump -p; its
# interpreted .idata contents carry the DLL name and the imported member for
# every entry. The reproduce test pins the DLL by SHA-256 so a swapped or
# rebuilt binary fails loudly rather than regenerating a different list.
#
# Why the built DLL and not cygwin.din: cygwin.din is the outward surface (the
# exports, WP-20's list). The imports are the other face and do not appear in
# the .din at all; they are the Windows functions winsup calls, resolved at
# link and recorded in the DLL's import directory. The binary is the only place
# the full, resolved set exists.
#
# Columns, tab-separated, no header row (the README carries the legend):
#   dll   symbol   variadic(yes|no)
# variadic marks a function whose Windows prototype is variadic; such an import
# cannot be forwarded by a generic thunk (a System V va_list is twenty-four
# bytes and Microsoft's is eight) and is WP-24's to handle, so the generator
# refuses to emit a plain wrapper for it. See the classifier note below.
#
# Usage:
#   extract-imports.sh [options]
#
# Options:
#   -d FILE, --dll=FILE     cygwin1.dll to read.
#                           [default: /c/-/cygwin/root/bin/cygwin1.dll]
#   -o FILE, --output=FILE  Destination; - is stdout. [default: -]
#   -t, --terse             Print the counts alone, one key=value per line.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as IMPORT_INV_<OPTION>, and the option wins.

set -u

prog=extract-imports
release='extract-imports 1.0'

dll=${IMPORT_INV_DLL:-/c/-/cygwin/root/bin/cygwin1.dll}
output=${IMPORT_INV_OUTPUT:--}
terse=${IMPORT_INV_TERSE:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-d|--dll)     dll=${2:-}; shift 2 ;;
		--dll=*)      dll=${1#*=}; shift ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-t|--terse)   terse=1; shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
[ -f "$dll" ] || die "no cygwin1.dll at $dll"
command -v objdump >/dev/null 2>&1 || die "objdump (binutils) not on PATH"

# The variadic classifier. An import's calling convention is not recorded in
# the import table, so a variadic function is recognised by name against the
# documented variadic entry points of the two DLLs cygwin1.dll imports from.
# KERNEL32.dll exports none; ntdll.dll exports the DbgPrint family and a small
# set of CRT-style formatters. The list is deliberately over-broad: a name on
# it that is not imported costs nothing, while a variadic import that slipped
# through would be forwarded as if fixed-arity and corrupt its stack. Today the
# intersection with the import set is empty, which is the finding the README
# records; the branch exists so a future base that imports one is caught at
# generation rather than at run time.
variadic_set=' DbgPrint DbgPrintEx DbgPrintReturnControlC vDbgPrintEx vDbgPrintExWithPrefix RtlCliDisplayString sprintf swprintf _snprintf _snwprintf _scprintf _scwprintf wsprintfA wsprintfW '

is_variadic() {
	case $variadic_set in
		*" $1 "*) return 0 ;;
		*)        return 1 ;;
	esac
}

# The parse. objdump -p prints, for the interpreted .idata section, a "DLL
# Name:" header per imported library and then one member line per import,
# "<vma>\t <hint>  <name>". The import directory is bracketed by "The Import
# Tables" and the export section that follows it, so parsing is confined to
# that window; the member regex matches nowhere else, but the window makes the
# intent explicit. A member line the regex cannot read is a format this parser
# did not expect, and the run fails on it rather than dropping an import, since
# a missing wrapper is a direct down-call at run time.
rows=$(objdump -p "$dll" 2>/dev/null | awk '
	/^The Import Tables/          { imp = 1; next }
	/^There is an export table/   { imp = 0 }
	/^The Export Tables/          { imp = 0 }
	!imp { next }
	/DLL Name:/ { dll = $3; next }
	/^\t[0-9a-f]+\t[ \t]*[0-9]+  / {
		name = $0
		sub(/^\t[0-9a-f]+\t[ \t]*[0-9]+  /, "", name)
		sub(/[ \t]+$/, "", name)
		if (name == "" || dll == "") {
			printf "'"$prog"': line %d: unreadable import member: %s\n", NR, $0 > "/dev/stderr"
			bad = 1; next
		}
		printf "%s\t%s\n", dll, name
	}
	END { if (bad) exit 3 }
') || die "cygwin1.dll carried an import member this parser did not recognize"

[ -n "$rows" ] || die "no imports parsed from $dll (is it a PE with an import table?)"

# Attach the variadic column in a second pass so the classifier lives in the
# shell rather than being duplicated into awk.
rows=$(printf '%s\n' "$rows" | while IFS=$'\t' read -r d n; do
	if is_variadic "$n"; then printf '%s\t%s\tyes\n' "$d" "$n"
	else                      printf '%s\t%s\tno\n'  "$d" "$n"; fi
done)

emit() {
	if [ "$output" = - ]; then printf '%s\n' "$rows"
	else printf '%s\n' "$rows" > "$output" || die "cannot write $output"
	fi
}

if [ "$terse" = 1 ]; then
	printf '%s\n' "$rows" | awk -F'\t' '
		{ total++; per[$1]++ }
		$3 == "yes" { v++ }
		END {
			printf "total=%d\n", total
			for (k in per) printf "dll[%s]=%d\n", k, per[k]
			printf "variadic=%d\n", v
		}' | sort
else
	emit
fi
