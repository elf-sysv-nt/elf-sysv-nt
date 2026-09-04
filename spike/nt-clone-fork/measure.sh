#!/usr/bin/env bash
#
# Does NtCreateProcessEx with a null section clone this process, the way fork
# would need? Proposal 0011's open question 2 asks it and nothing in the
# project has measured it. This builds the native probe and the Cygwin fork
# timer beside it, runs both, and writes a dated transcript with a verdict.
#
# The probe is native (mingw), because the design's host process has no Cygwin
# under it; the fork timer is Cygwin, because Cygwin's fork is the thing it
# prices. The two binaries are built by two compilers on purpose.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --iterations=N    Clones and forks to time. [default: 200]
#   -k, --keep              Keep the built binaries beside the sources.
#   -q, --quiet             Errors only.
#   -v, --verbose           Pass --verbose to the native probe.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.

set -u
set -o pipefail

prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${MEASURE_OUTPUT:--}
iterations=${MEASURE_ITERATIONS:-200}
keep=${MEASURE_KEEP:-0}
quiet=${MEASURE_QUIET:-0}
verbose=${MEASURE_VERBOSE:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)        usage; exit 0 ;;
		-V|--version)     printf '%s\n' "$release"; exit 0 ;;
		-o|--output)      output=${2:-}; shift 2 ;;
		--output=*)       output=${1#*=}; shift ;;
		-n|--iterations)  iterations=${2:-}; shift 2 ;;
		--iterations=*)   iterations=${1#*=}; shift ;;
		-k|--keep)        keep=1; shift ;;
		-q|--quiet)       quiet=1; shift ;;
		-v|--verbose)     verbose=1; shift ;;
		--)               shift; break ;;
		-?*)              printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)                break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

case $iterations in
	''|*[!0-9]*) die "iterations must be a positive integer, got '$iterations'" ;;
esac
[ "$iterations" -ge 1 ] || die 'iterations must be at least 1'

cross=x86_64-w64-mingw32-gcc
command -v "$cross" >/dev/null 2>&1 || die "no $cross on PATH"
command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then
	probe=$here/clone-probe.exe
	timer=$here/fork-timer.exe
else
	probe=$work/clone-probe.exe
	timer=$work/fork-timer.exe
fi

note 'building the native clone probe'
"$cross" -std=gnu11 -O1 -Wall -Wextra -o "$probe" \
	"$here/clone-probe.c" > "$work/build-probe.log" 2>&1 ||
	{ cat "$work/build-probe.log" >&2; die 'the clone probe did not build'; }

note 'building the Cygwin fork timer'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$timer" \
	"$here/fork-timer.c" > "$work/build-timer.log" 2>&1 ||
	{ cat "$work/build-timer.log" >&2; die 'the fork timer did not build'; }

probe_args="-n $iterations"
[ "$verbose" = 1 ] && probe_args="$probe_args --verbose"

note 'running the clone probe'
# The native binary writes stdout in text mode, so it emits CRLF; strip the
# carriage returns before anything reads the keys or the raw block quotes them.
# shellcheck disable=SC2086
"$probe" $probe_args 2>"$work/probe.err" | tr -d '\r' > "$work/probe.out" ||
	{ cat "$work/probe.err" >&2; die 'the clone probe did not run'; }
[ -s "$work/probe.out" ] || { cat "$work/probe.err" >&2; die 'the clone probe produced no output'; }

note 'running the fork timer'
"$timer" -n "$iterations" 2>"$work/timer.err" | tr -d '\r' > "$work/timer.out" ||
	{ cat "$work/timer.err" >&2; die 'the fork timer did not run'; }

val()  { sed -n "s/^$1=//p" "$work/probe.out"; }
tval() { sed -n "s/^$1=//p" "$work/timer.out"; }

q1_created=$(val q1_clone_created)
q1_status=$(val q1_status)
q2_cloned=$(val q2_address_space_cloned)
q2_isolated=$(val q2_post_clone_write_isolated)
q3_inherited=$(val q3_inherited_handles)
q3_absent=$(val q3_noninherited_absent)
q4_present=$(val q4_shared_section_view)
q4_p2c=$(val q4_parent_to_child)
q4_c2p=$(val q4_child_to_parent)
q5_ran=$(val q5_thread_after_clone)
q5_created=$(val q5_thread_created)
q5_status=$(val q5_plain_status)
q5_control=$(val q5_control_self_thread)
q6_pcfg=$(val q6_parent_cfg); q6_ccfg=$(val q6_child_cfg)
q6_pcet=$(val q6_parent_cet); q6_ccet=$(val q6_child_cet)

# The finding, stated as what the clone is worth as a fork primitive. The clone
# either refuses outright, or is created and clones the address space but
# cannot host a thread -- the csrss break -- or clones and runs a thread, which
# would be the whole primitive. The ladder reads the facts in that order and
# never reads a timing.
finding=inconclusive
if [ "$q1_created" != 1 ]; then
	finding=clone-refused
elif [ "$q2_cloned" = 1 ] && [ "$q5_ran" = 1 ]; then
	finding=clone-works
elif [ "$q2_cloned" = 1 ] && [ "$q5_ran" = 0 ]; then
	finding=clone-without-threads
else
	finding=clone-partial
fi

flag()  { [ "$1" = on ] && printf 'enabled' || { [ "$1" = off ] && printf 'disabled' || printf 'unavailable'; }; }
yn()    { [ "$1" = 1 ] && printf 'yes' || { [ "$1" = 0 ] && printf 'no' || printf 'n/a'; }; }

{
	printf 'NtCreateProcessEx null-section clone as a fork primitive\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | grep -o '[0-9][0-9.]*' | head -1)"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cross" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$probe" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  NtCreateProcessEx(NULL section) clones: %s (status %s)\n' \
		"$([ "$q1_created" = 1 ] && printf 'yes' || printf 'NO')" "$q1_status"
	printf '  q2  private pre-clone data readable in the child at the same address: %s%s\n' \
		"$(yn "$q2_cloned")" \
		"$([ "$q2_isolated" = 1 ] && printf ', post-clone write isolated' || printf '')"
	printf '      method: NtReadVirtualMemory from the parent\n'
	printf '  q3  an inheritable handle valid in the child at the same value: %s%s\n' \
		"$(yn "$q3_inherited")" \
		"$([ "$q3_absent" = 1 ] && printf ', the non-inheritable one absent' || printf '')"
	printf '  q4  a shared ViewShare section view present and coherent both ways: %s\n' \
		"$([ "$q4_present" = 1 ] && [ "$q4_p2c" = 1 ] && [ "$q4_c2p" = 1 ] && printf 'yes' || printf 'no')"
	printf '  q5  a thread created in the child after the clone runs: %s (status %s; control thread in this process: %s)\n' \
		"$(yn "$q5_ran")" "$q5_status" "$(yn "$q5_control")"
	printf '  q6  mitigations, parent / child: CFG %s / %s, CET %s / %s\n' \
		"$(flag "$q6_pcfg")" "$(flag "$q6_ccfg")" "$(flag "$q6_pcet")" "$(flag "$q6_ccet")"
	printf '  q7  median clone %s us vs Cygwin fork %s us over %s each (context, not a finding)\n\n' \
		"$(val q7_clone_median_us)" "$(tval q7_cygwin_fork_median_us)" "$iterations"

	printf 'raw\n\n'
	sed -e 's/^/    /' "$work/probe.out"
	sed -e 's/^/    /' "$work/timer.out"

	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi
