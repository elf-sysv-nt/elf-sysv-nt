#!/usr/bin/env bash
#
# Can a PE stub map a static ELF image and transfer control to it?
#
# Builds the stub, synthesizes the specimens, runs each case, and writes the
# transcript. Nothing is installed and no privilege is wanted; everything
# happens inside one process and one temporary directory.
#
# Six cases, four of which are asked to map and run and two of which are
# controls that have to be turned away. The controls are the point of the
# shape: a spike that only runs the cases it expects to pass has measured its
# own optimism.
#
# Usage:
#   map-and-jump.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -w DIR, --work=DIR      Keep the build and the specimens here.
#   -c CASE, --case=CASE    Run one case by name rather than all six.
#   -T N, --timeout=N       Seconds a case may take. [default: 30]
#   -r N, --repeat=N        Times to repeat a mapping case that failed, to
#                           separate a standing obstacle from a stray one.
#                           0 disables it. [default: 20]
#   -k, --keep              Do not delete the working directory.
#   -t, --terse             The summary block alone, one key=value per line.
#   -q, --quiet             Errors only. Only useful with --output.
#   -v, --verbose           Pass --verbose down to the stub.
#   -d, --debug             Trace execution; implies --verbose.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MAP_AND_JUMP_<OPTION>, and the option wins
# over the variable.

set -u

prog=map-and-jump
release='map-and-jump 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${MAP_AND_JUMP_OUTPUT:--}
work=${MAP_AND_JUMP_WORK:-}
only=${MAP_AND_JUMP_CASE:-}
timeout_s=${MAP_AND_JUMP_TIMEOUT:-30}
repeat=${MAP_AND_JUMP_REPEAT:-20}
keep=${MAP_AND_JUMP_KEEP:-0}
terse=${MAP_AND_JUMP_TERSE:-0}
quiet=${MAP_AND_JUMP_QUIET:-0}
verbose=${MAP_AND_JUMP_VERBOSE:-0}
debug=${MAP_AND_JUMP_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-w|--work)    work=${2:-}; shift 2 ;;
		--work=*)     work=${1#*=}; shift ;;
		-c|--case)    only=${2:-}; shift 2 ;;
		--case=*)     only=${1#*=}; shift ;;
		-T|--timeout) timeout_s=${2:-}; shift 2 ;;
		--timeout=*)  timeout_s=${1#*=}; shift ;;
		-r|--repeat)  repeat=${2:-}; shift 2 ;;
		--repeat=*)   repeat=${1#*=}; shift ;;
		-k|--keep)    keep=1; shift ;;
		-t|--terse)   terse=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		-v|--verbose) verbose=$((verbose + 1)); shift ;;
		-d|--debug)   debug=1; verbose=$((verbose + 1)); shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done

[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

case $timeout_s in
	'' | *[!0-9]*) printf '%s: --timeout wants a number, got %s\n' "$prog" "$timeout_s" >&2; exit 2 ;;
esac
[ "$timeout_s" -gt 0 ] || { printf '%s: --timeout wants a positive number\n' "$prog" >&2; exit 2; }

case $repeat in
	'' | *[!0-9]*) printf '%s: --repeat wants a number, got %s\n' "$prog" "$repeat" >&2; exit 2 ;;
esac

[ "$debug" = 1 ] && set -x

for tool in gcc as objcopy python3; do
	command -v "$tool" >/dev/null 2>&1 || die "no $tool on PATH"
done

# The cases. Name, link base, p_align, and what the case is for: map means it
# must map and run to the end, refuse means it must be turned away. Fields are
# separated by spaces because none of them contains one.
#
#   flat        the geometry ld gives a static binary with the default
#               max-page-size on el8, page-aligned segments, base 0x400000.
#   huge        the same at 2 MB alignment, which is what ld's default
#               max-page-size leaves in a vendor binary and what WP-32's
#               arithmetic has to survive. The 2 MB alignment inflates the
#               span from 24 KB to 4 MB without adding a byte of content,
#               which is the whole reason this case is separate from flat.
#   hugehigh    the same geometry somewhere nothing else wants. It exists to
#               separate two questions that the huge case asks at once: can
#               the stub do the 2 MB arithmetic, and is the address it is
#               asked to do it at available. A pair where one passes and one
#               fails answers both; either alone answers neither.
#   offgranule  a link base that is page-aligned and not granule-aligned.
#               0x8048000 is where i386 binaries lived and is 32 KB past a
#               64 KB boundary, so it prices the rounding directly.
#   occupied    a link base inside the stub's own image. This one has to be
#               refused; a stub that maps over itself and reports success has
#               not been measuring anything.
#   nowhere     a link base at the top of the 128 TB user address space. Also
#               has to be refused, and by a different mechanism.
#
# The occupied case takes its base from the stub's own module base, which the
# stub reports and which a rebuild or a rebase can move. Hardcoding it would
# make the control quietly stop being one.
cases='
flat 0x400000 0x1000 map
huge 0x400000 0x200000 map
hugehigh 0x10000000 0x200000 map
offgranule 0x8048000 0x1000 map
occupied - 0x1000 refuse
nowhere 0x800000000000 0x1000 refuse
'

if [ -n "$work" ]; then
	mkdir -p "$work" || die "cannot create $work"
	work=$(cd "$work" && pwd)
	keep=1
else
	work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$work"; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

stub=$work/map-and-jump-stub.exe
payload=$work/payload.bin

note 'building the stub'
gcc -O1 -g -Wall -Wextra -o "$stub" "$here/stub.c" "$here/enter.S" \
	> "$work/build.log" 2>&1 || {
	cat "$work/build.log" >&2
	die 'the stub did not build'
}

note 'assembling the payload'
as --64 -o "$work/payload.o" "$here/payload.S" >> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the payload did not assemble'; }
objcopy -O binary --only-section=.text "$work/payload.o" "$payload" ||
	die 'the payload did not flatten'
[ -s "$payload" ] || die 'the flattened payload is empty'

# The occupied control wants an address inside the stub, so ask the stub.
module_base=$("$stub" --where | sed -n 's/^case_module_base=//p')
[ -n "$module_base" ] || die 'the stub would not report its own module base'

run_case() {
	name=$1 base=$2 align=$3 kind=$4
	[ "$base" = - ] && base=$module_base
	elf=$work/$name.elf
	extra=
	[ "$kind" = refuse ] && extra=--expect-refusal
	[ "$verbose" -gt 0 ] && extra="$extra --verbose"

	printf '== case %s -- base %s, p_align %s%s\n\n' "$name" "$base" "$align" \
		"$([ "$kind" = refuse ] && printf ', a control that must be refused')"
	python3 "$here/make-elf.py" --code "$payload" --output "$elf" \
		--base "$base" --align "$align" --manifest "$work/$name.manifest" \
		--quiet || { printf '    the specimen would not build\n\ncase_result=fail\n'; return 1; }
	sed -e 's/^/    specimen /' "$work/$name.manifest"
	printf '\n'
	# shellcheck disable=SC2086
	timeout "$timeout_s" "$stub" $extra "$elf" 2>&1
	st=$?
	if [ $st -eq 124 ]; then
		printf '\n    the case did not finish inside %s seconds\n' "$timeout_s"
		printf 'case_result=fail\n'
		return 1
	fi
	return $st
}

ran=0
passed=0
: > "$work/body"
: > "$work/verdicts"

# Fed by a heredoc rather than a pipe so the loop runs in this shell and the
# counts survive it.
while read -r name base align kind; do
	[ -n "$name" ] || continue
	if [ -n "$only" ] && [ "$only" != "$name" ]; then
		continue
	fi
	note "case $name"
	run_case "$name" "$base" "$align" "$kind" >> "$work/body" 2>&1
	result=$?
	# A mapping case that failed is worth asking twice. One refusal could be
	# a stray allocation that happened to land badly; twenty in twenty is an
	# obstacle that stands there, and only the second of those is a finding.
	if [ "$result" -ne 0 ] && [ "$kind" = map ] && [ "$repeat" -gt 0 ]; then
		again=0
		attempt=1
		while [ "$attempt" -le "$repeat" ]; do
			timeout "$timeout_s" "$stub" --terse --quiet "$work/$name.elf" \
				>/dev/null 2>&1 || again=$((again + 1))
			attempt=$((attempt + 1))
		done
		printf '    asked again %d times, refused %d of them\n' \
			"$repeat" "$again" >> "$work/body"
		printf 'case_repeat=%d\ncase_repeat_refused=%d\n' \
			"$repeat" "$again" >> "$work/body"
	fi
	printf '%s %s %s\n' "$name" "$kind" \
		"$([ $result -eq 0 ] && printf pass || printf fail)" >> "$work/verdicts"
	printf '\n' >> "$work/body"
done <<EOF
$cases
EOF

[ -s "$work/verdicts" ] || die "no case ran; --case named ${only:-nothing} and nothing matched"

count() { grep -c "$@" "$work/verdicts" 2>/dev/null || true; }
ran=$(($(wc -l < "$work/verdicts") + 0))
passed=$(($(count -e ' pass$') + 0))
map_run=$(($(count -e ' map ') + 0))
map_passed=$(($(count -e ' map pass$') + 0))
refuse_run=$(($(count -e ' refuse ') + 0))
refuse_passed=$(($(count -e ' refuse pass$') + 0))

# The question is whether this can be done, not whether it can be done
# everywhere. So the verdict turns on one mapping case running to the end with
# every control refused, and the map cases that failed are reported as
# constraints beside it rather than folded into it. A transcript that reported
# only cases_passed would answer a question nobody asked.
if [ "$map_passed" -ge 1 ] && [ "$refuse_run" -eq "$refuse_passed" ]; then
	verdict=yes
else
	verdict=no
fi

{
	printf 'cases_run=%d\n' "$ran"
	printf 'cases_passed=%d\n' "$passed"
	printf 'map_cases=%d\n' "$map_run"
	printf 'map_cases_passed=%d\n' "$map_passed"
	printf 'refuse_cases=%d\n' "$refuse_run"
	printf 'refuse_cases_passed=%d\n' "$refuse_passed"
	printf 'module_base=%s\n' "$module_base"
	while read -r name kind result; do
		printf 'case_%s=%s %s\n' "$name" "$kind" "$result"
	done < "$work/verdicts"
	printf 'verdict=%s\n' "$verdict"
} > "$work/summary"

if [ "$terse" = 1 ]; then
	cat "$work/summary" > "$work/report"
else
	{
		printf 'a PE stub against a static ELF image\n\n'
		printf 'host        %s\n' "$(hostname 2>/dev/null)"
		printf 'kernel      %s\n' "$(uname -srm)"
		printf 'compiler    %s\n' "$(gcc --version | head -1)"
		printf 'assembler   %s\n' "$(as --version | head -1)"
		printf 'python      %s\n' "$(python3 -V 2>&1)"
		printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf 'script      %s\n' "$release"
		printf 'stub        %s\n' "$("$stub" --version)"
		printf 'generator   %s\n\n' "$(python3 "$here/make-elf.py" --version)"
		cat "$work/body"
		printf '== summary\n\n'
		sed -e 's/^/    /' "$work/summary"
	} > "$work/report"
fi

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi

[ "$verdict" = yes ] && [ "$ran" -eq "$passed" ]
