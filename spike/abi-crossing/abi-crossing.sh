#!/usr/bin/env bash
#
# Can one entry point present a System V face over an MS-ABI core, survive a
# signal delivered mid-call, and leave the red zone intact?
#
# Builds the probe, asks the compiler one question the probe cannot ask
# itself, runs the eleven cases, and writes the transcript. Nothing is
# installed and no privilege is wanted; everything happens inside one process
# and one temporary directory.
#
# Six of the cases have to pass, two are controls that have to behave as
# controls, and the rest are measurements with no pass or fail to give. A
# spike that only runs the cases it expects to pass has measured its own
# optimism, so the controls come first in the order and the verdict turns on
# them too.
#
# Usage:
#   abi-crossing.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -w DIR, --work=DIR      Keep the build here.
#   -c CASE, --case=CASE    Run one case by name rather than all eleven.
#   -r N, --rounds=N        Passes for the quiet and preempt cases.
#                           [default: 200000]
#   -e N, --events=N        Provocations for the hijack, signal and vectored
#                           cases. [default: 2000]
#   -z N, --depth=N         Bytes below %rsp to watch. [default: 1024]
#   -T N, --timeout=N       Seconds the probe may take. [default: 600]
#   -k, --keep              Do not delete the working directory.
#   -t, --terse             The summary block alone, one key=value per line.
#   -q, --quiet             Errors only. Only useful with --output.
#   -v, --verbose           Name each case on stderr as it starts.
#   -d, --debug             Trace execution; implies --verbose.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as ABI_CROSSING_<OPTION>, and the option wins
# over the variable.

set -u

prog=abi-crossing
release='abi-crossing 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${ABI_CROSSING_OUTPUT:--}
work=${ABI_CROSSING_WORK:-}
only=${ABI_CROSSING_CASE:-}
rounds=${ABI_CROSSING_ROUNDS:-200000}
events=${ABI_CROSSING_EVENTS:-2000}
depth=${ABI_CROSSING_DEPTH:-1024}
timeout_s=${ABI_CROSSING_TIMEOUT:-600}
keep=${ABI_CROSSING_KEEP:-0}
terse=${ABI_CROSSING_TERSE:-0}
quiet=${ABI_CROSSING_QUIET:-0}
verbose=${ABI_CROSSING_VERBOSE:-0}
debug=${ABI_CROSSING_DEBUG:-0}

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
		-r|--rounds)  rounds=${2:-}; shift 2 ;;
		--rounds=*)   rounds=${1#*=}; shift ;;
		-e|--events)  events=${2:-}; shift 2 ;;
		--events=*)   events=${1#*=}; shift ;;
		-z|--depth)   depth=${2:-}; shift 2 ;;
		--depth=*)    depth=${1#*=}; shift ;;
		-T|--timeout) timeout_s=${2:-}; shift 2 ;;
		--timeout=*)  timeout_s=${1#*=}; shift ;;
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

for pair in "timeout $timeout_s" "rounds $rounds" "events $events" "depth $depth"; do
	name=${pair%% *} value=${pair#* }
	case $value in
		'' | *[!0-9]*)
			printf '%s: --%s wants a number, got %s\n' "$prog" "$name" "$value" >&2
			exit 2 ;;
	esac
	[ "$value" -gt 0 ] || {
		printf '%s: --%s wants a positive number\n' "$prog" "$name" >&2
		exit 2
	}
done
[ $((depth % 8)) -eq 0 ] || {
	printf '%s: --depth wants a multiple of 8, got %s\n' "$prog" "$depth" >&2
	exit 2
}

[ "$debug" = 1 ] && set -x

for tool in gcc; do
	command -v "$tool" >/dev/null 2>&1 || die "no $tool on PATH"
done

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

probe=$work/crossing.exe

note 'building the probe'
gcc -O1 -g -Wall -Wextra -o "$probe" "$here/crossing.c" "$here/crossing.S" \
	> "$work/build.log" 2>&1 || {
	cat "$work/build.log" >&2
	die 'the probe did not build'
}
[ -s "$work/build.log" ] && cat "$work/build.log" >&2

# The one question the probe cannot ask itself, because by the time it is
# running the answer is already compiled into it: does gcc give a sysv_abi
# function a red zone on a Microsoft-ABI target? If it does, -mno-red-zone is
# a policy with a cost and the DLL has to carry the flag everywhere. If it
# does not, the flag is already the behavior and the policy costs nothing,
# which is worth knowing before it is written into a build system.
#
# The specimen is a leaf with locals, which is the only shape where the
# distinction shows: a leaf entitled to the red zone keeps its locals below
# %rsp and never adjusts the stack pointer at all.
note 'asking the compiler about the red zone'
cat > "$work/codegen.c" <<'EOF'
__attribute__((sysv_abi, noinline))
long leaf(long a, long b)
{
	volatile long t[4];

	t[0] = a;
	t[1] = b;
	t[2] = a ^ b;
	t[3] = a + b;
	return t[0] + t[1] + t[2] + t[3];
}
EOF
gcc -O2 -S -o "$work/with.s" "$work/codegen.c" 2>>"$work/build.log"
gcc -O2 -S -mno-red-zone -o "$work/without.s" "$work/codegen.c" 2>>"$work/build.log"
if [ -s "$work/with.s" ] && [ -s "$work/without.s" ]; then
	if cmp -s "$work/with.s" "$work/without.s"; then
		codegen_same=yes
	else
		codegen_same=no
	fi
	if grep -q -e 'subq.*%rsp' "$work/with.s"; then
		codegen_frame=yes
	else
		codegen_frame=no
	fi
else
	codegen_same=unavailable
	codegen_frame=unavailable
fi

note 'running the probe'
set -- --rounds "$rounds" --events "$events" --depth "$depth"
[ -n "$only" ] && set -- "$@" --case "$only"
[ "$verbose" -gt 0 ] && set -- "$@" --debug
[ "$terse" = 1 ] && set -- "$@" --terse

timeout "$timeout_s" "$probe" "$@" > "$work/body" 2> "$work/probe.err"
st=$?
[ -s "$work/probe.err" ] && cat "$work/probe.err" >&2
if [ $st -eq 124 ]; then
	die "the probe did not finish inside $timeout_s seconds"
fi
if [ $st -eq 2 ]; then
	die 'the probe refused its arguments'
fi
[ -s "$work/body" ] || die 'the probe wrote nothing'

verdict=$(sed -n 's/^ *verdict=//p' "$work/body")
[ -n "$verdict" ] || die 'the probe reported no verdict'

# The observation lines are appended to the probe's own summary rather than
# printed beside it, so that --terse stays what it claims to be: one block of
# key=value that a document can quote whole.
observations=$work/observations
case $codegen_frame in
	no)  leaf_red_zone=yes ;;	# no stack adjustment, so the locals are below %rsp
	yes) leaf_red_zone=no ;;
	*)   leaf_red_zone=$codegen_frame ;;
esac
case $codegen_same in
	yes) flag_changes=no ;;
	no)  flag_changes=yes ;;
	*)   flag_changes=$codegen_same ;;
esac
{
	printf 'sysv_leaf_uses_red_zone=%s\n' "$leaf_red_zone"
	printf 'mno_red_zone_changes_codegen=%s\n' "$flag_changes"
} > "$observations"

if [ "$terse" = 1 ]; then
	cat "$work/body" > "$work/report"
	cat "$observations" >> "$work/report"
else
	{
		printf 'one entry point, System V outward and Microsoft inward\n\n'
		printf 'host        %s\n' "$(hostname 2>/dev/null)"
		printf 'kernel      %s\n' "$(uname -srm)"
		printf 'processors  %s\n' "$(getconf _NPROCESSORS_ONLN 2>/dev/null)"
		printf 'compiler    %s\n' "$(gcc --version | head -1)"
		printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf 'script      %s\n' "$release"
		printf 'probe       %s\n' "$("$probe" --version)"
		printf 'rounds      %s passes, %s events, %s bytes watched\n\n' \
			"$rounds" "$events" "$depth"
		cat "$work/body"
		sed -e 's/^/    /' "$observations"
		printf '\n    The last two lines are the compiler'"'"'s answer rather than\n'
		printf '    Windows'"'"'. They say whether gcc puts a sysv_abi leaf'"'"'s locals\n'
		printf '    below %%rsp on this target, and so whether -mno-red-zone is a\n'
		printf '    flag that changes something or a restatement of the default.\n'
	} > "$work/report"
fi

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi

[ "$verdict" = yes ] && [ $st -eq 0 ]
