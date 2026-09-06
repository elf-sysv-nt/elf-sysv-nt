#!/usr/bin/env bash
#
# Which el8 packages carry a raw syscall instruction, or a Go runtime, in
# their shipped text: the size of substrate N's userland as a list.
#
# The census itself is a long, resumable job over every x86_64 package in
# the Rocky 8.10 repositories (run-census.sh, detached, into the untracked
# annex); this script renders the transcript from what that job collected,
# confirming every byte-scan hit by disassembly with the cross objdump.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -r DIR, --root=DIR      The census work root. [default: $ELFSYSVNT_ROOT/a/census-work/syscall-fs]
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.

set -u
prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../../bin/roots.sh"
root=${MEASURE_ROOT:-$ELFSYSVNT_ROOT/a/census-work/syscall-fs}
output=${MEASURE_OUTPUT:--}
quiet=${MEASURE_QUIET:-0}
objdump=${MEASURE_OBJDUMP:-$ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-objdump}
usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-r|--root) root=${2:-}; shift 2 ;;
		--root=*) root=${1#*=}; shift ;;
		-o|--output) output=${2:-}; shift 2 ;;
		--output=*) output=${1#*=}; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
[ -d "$root/frag" ] || die "no census under $root; run run-census.sh first"
command -v "$objdump" >/dev/null 2>&1 || command -v "$objdump.exe" >/dev/null 2>&1 || die "no objdump at $objdump"

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM

note 'rendering the report, confirming hits by disassembly'
python3 "$here/census.py" report --root "$root" --objdump "$objdump" > "$work/report.out" 2>"$work/report.err" ||
	{ cat "$work/report.err" >&2; die 'the report did not render'; }
val() { sed -n "s/^$1=//p" "$work/report.out"; }

scanned=$(val packages_scanned); elf=$(val elf_files_scanned); errs=$(val packages_errored)
bytes=$(val packages_with_0f05_bytes); exact=$(val packages_confirmed_syscall); outside=$(val packages_confirmed_syscall_outside_glibc)
go=$(val packages_go_runtime); both=$(val packages_n_cannot_rebuild_as_shipped)
total=$(grep -c . "$root/worklist.tsv" 2>/dev/null || echo 0)

# The finding is the shape, not the count: whether the packages N cannot
# take as shipped are a handful of runtimes or a swathe of the distribution.
if [ "${scanned:-0}" -lt 100 ]; then finding=census-incomplete
elif [ "${both:-0}" -le $(( scanned / 20 )) ]; then finding=raw-syscall-confined-to-runtimes
else finding=raw-syscall-widespread; fi
finding="$finding,glibc-carries-its-own"

{
	printf 'raw syscall instructions and Go runtimes in the shipped el8 text\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'objdump     %s\n' "$("$objdump" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n\n' "$release"
	printf 'reading\n\n'
	printf '  %s packages scanned of %s in the worklist (%s errored), %s ELF files; %s packages carry an 0f 05 byte pair in text,\n' \
		"${scanned:-?}" "$total" "${errs:-?}" "${elf:-?}" "${bytes:-?}"
	printf '  %s a syscall instruction objdump confirms, %s of those outside glibc; %s carry a Go runtime;\n' \
		"${exact:-?}" "${outside:-?}" "${go:-?}"
	printf '  %s packages substrate N cannot take as shipped and the post-link check would refuse after a rebuild.\n\n' "${both:-?}"
	printf 'raw\n\n'; sed -e 's/^/    /' "$work/report.out"
	printf '\nverdict\n\n    finding=%s\n' "$finding"
} > "$work/report"
if [ "$output" = - ]; then cat "$work/report"; else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
