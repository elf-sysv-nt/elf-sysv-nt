#!/usr/bin/env bash
#
# What does the driver stop passing when a `specs` file is installed?
#
# DR-0061 makes the granule-separable link a default by installing one at
# $prefix/lib/gcc/$target/$version/specs. This asks the driver, with -###,
# what it would hand the linker in five states of that file, and counts the
# three tokens that matter: --eh-frame-hdr, -lgcc_s, and the max-page-size
# the file exists to add.
#
# The file moves for the duration and is restored under a trap; -B cannot
# displace it, because a -B prefix is searched in addition to the installed
# path rather than instead of it. See README.md if a hard kill ever leaves
# it missing.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -P DIR, --prefix=DIR    Toolchain prefix. [default: $ELFSYSVNT_PREFIX or /c/-/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE  [default: x86_64-elfsysvnt-linux-gnu]
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.

set -u

prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)

prefix=${MEASURE_PREFIX:-${ELFSYSVNT_PREFIX:-/c/-/x-elfsysvnt}}
target=${MEASURE_TARGET:-x86_64-elfsysvnt-linux-gnu}
output=${MEASURE_OUTPUT:--}
quiet=${MEASURE_QUIET:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)     usage; exit 0 ;;
		-V|--version)  printf '%s\n' "$release"; exit 0 ;;
		-o|--output)   output=${2:-}; shift 2 ;;
		--output=*)    output=${1#*=}; shift ;;
		-P|--prefix)   prefix=${2:-}; shift 2 ;;
		--prefix=*)    prefix=${1#*=}; shift ;;
		-T|--target)   target=${2:-}; shift 2 ;;
		--target=*)    target=${1#*=}; shift ;;
		-q|--quiet)    quiet=1; shift ;;
		-n|--count)    shift 2 ;;
		--)            shift; break ;;
		-?*)           printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)             break ;;
	esac
done

cxx=$prefix/bin/$target-g++
command -v "$cxx" >/dev/null 2>&1 || command -v "$cxx.exe" >/dev/null 2>&1 ||
	die "no $target-g++ under $prefix"

version=$("$cxx" -dumpversion 2>/dev/null) || die 'cannot ask the compiler its version'
specs=$prefix/lib/gcc/$target/$version/specs
committed=$here/../../toolchain/gcc/default.specs
[ -f "$committed" ] || die "no default.specs at $committed"

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
saved=$work/specs.saved
had_specs=0
[ -f "$specs" ] && { cp -p "$specs" "$saved" && had_specs=1; }

restore() {
	if [ "$had_specs" = 1 ]; then cp -p "$saved" "$specs" 2>/dev/null
	else rm -f "$specs" 2>/dev/null; fi
}
trap 'restore; rm -rf "$work"' EXIT
trap 'restore; rm -rf "$work"; exit 130' INT TERM

cat > "$work/t.cc" <<'EOF'
#include <stdexcept>
void poke (int v) { if (v > 0) throw std::runtime_error ("crossed"); }
EOF

# One state: install what is named (or nothing), ask the driver, count.
probe() {
	case $1 in
		absent) rm -f "$specs" ;;
		*)      cp -f "$2" "$specs" ;;
	esac
	"$cxx" -O2 -fPIC -shared -o /dev/null "$work/t.cc" -### > "$work/o" 2>&1
	line=$(grep collect2 "$work/o" | tr ' ' '\n')
	eh=$(printf '%s\n' "$line" | grep -c -- '--eh-frame-hdr')
	gs=$(printf '%s\n' "$line" | grep -cx -- '-lgcc_s')
	mp=$(printf '%s\n' "$line" | grep -c 'max-page-size=0x10000')
	printf '%s\teh_frame_hdr=%s\tlgcc_s=%s\tmax_page_size=%s\n' "$1" "$eh" "$gs" "$mp"
}

printf '' > "$work/rows"
note 'asking the driver in five states of the specs file'
probe absent                                     >> "$work/rows"
: > "$work/empty";                probe empty            "$work/empty" >> "$work/rows"
printf '*link:\n+ \n\n' > "$work/link0";      probe link-append-nothing      "$work/link0" >> "$work/rows"
printf '*self_spec:\n+ \n\n' > "$work/self0"; probe self-spec-append-nothing "$work/self0" >> "$work/rows"
probe committed-default-specs "$committed"       >> "$work/rows"
restore

val() { awk -F'\t' -v s="$1" -v k="$2" '$1==s { for (i=2;i<=NF;i++) { split($i,p,"="); if (p[1]==k) print p[2] } }' "$work/rows"; }

a_eh=$(val absent eh_frame_hdr); a_gs=$(val absent lgcc_s)
e_eh=$(val empty eh_frame_hdr);  e_gs=$(val empty lgcc_s)
c_eh=$(val committed-default-specs eh_frame_hdr)
c_gs=$(val committed-default-specs lgcc_s)
c_mp=$(val committed-default-specs max_page_size)

if [ "$a_eh" -ge 1 ] && [ "$a_gs" -ge 1 ] && [ "$e_eh" = 0 ] && [ "$e_gs" = 0 ] \
   && [ "$c_eh" = 0 ] && [ "$c_gs" = 0 ] && [ "$c_mp" -ge 1 ]; then
	finding=any-specs-file-drops-eh-frame-hdr-and-shared-libgcc
else
	finding="absent-${a_eh}${a_gs}-empty-${e_eh}${e_gs}-committed-${c_eh}${c_gs}${c_mp}"
fi

{
	printf 'what a specs file costs the link\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cxx" --version | head -1)"
	printf 'target      %s\n' "$target"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n\n' "$release"
	printf 'reading\n\n'
	printf '  with no specs file the driver passes --eh-frame-hdr and -lgcc_s;\n'
	printf '  an empty one at the same path costs both, so the loss follows the\n'
	printf "  file's presence rather than its contents.\n\n"
	printf 'per state, tokens on the collect2 line\n\n'
	printf '    %-30s %-14s %-9s %s\n' state --eh-frame-hdr -lgcc_s max-page-size
	awk -F'\t' '{ for (i=2;i<=NF;i++) { split($i,p,"="); v[p[1]]=p[2] }
	              printf "    %-30s %-14s %-9s %s\n", $1, v["eh_frame_hdr"], v["lgcc_s"], v["max_page_size"] }' "$work/rows"
	printf '\nraw\n\n'
	sed -e 's/^/    /' "$work/rows"
	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then cat "$work/report"
else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
