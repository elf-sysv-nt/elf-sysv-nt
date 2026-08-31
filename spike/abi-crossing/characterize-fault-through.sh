#!/usr/bin/env bash
#
# Why does a fault beneath a System V frame stop crossing back?
#
# spike/abi-crossing's fault-through case passed on Cygwin 3.0.7 and fails on
# 3.6.10, and WP-22, WP-43 and WP-61 fail with it. The issue reports read that
# as 3.6.10 delivering the fault differently. The case cannot support that
# reading, because it reports the same failure for three different things: a
# fault that was never raised, a fault that was raised and lost, and a fault
# that came back wrong. This separates them.
#
# Every case runs in its own process and answers with its exit status, because
# a case that loses the fault takes the process with it.
#
# It builds and runs in whatever Cygwin root invokes it and labels the
# transcript with that root's version, because a binary built against one
# root's cygwin1.dll hangs when run from another root's shell. Run it once pe
# root and keep both transcripts; the comparison is the point.
#
# Usage:
#   characterize-fault-through.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -O LEVEL, --opt=LEVEL   Optimization levels to sweep. [default: 0 1 2]
#   -r N, --repeat=N        Times to run each case. [default: 5]
#   -k, --keep              Keep the built binaries beside the sources.
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as CHARACTERIZE_FAULT_<OPTION>.

set -u

prog=characterize-fault-through
release='characterize-fault-through 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${CHARACTERIZE_FAULT_OUTPUT:--}
opts=${CHARACTERIZE_FAULT_OPT:-0 1 2}
repeat=${CHARACTERIZE_FAULT_REPEAT:-5}
keep=${CHARACTERIZE_FAULT_KEEP:-0}
quiet=${CHARACTERIZE_FAULT_QUIET:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-O|--opt)     opts=${2:-}; shift 2 ;;
		--opt=*)      opts=${1#*=}; shift ;;
		-r|--repeat)  repeat=${2:-}; shift 2 ;;
		--repeat=*)   repeat=${1#*=}; shift ;;
		-k|--keep)    keep=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

case $repeat in
	'' | *[!0-9]*) printf '%s: --repeat wants a number, got %s\n' "$prog" "$repeat" >&2; exit 2 ;;
esac
[ "$repeat" -gt 0 ] || { printf '%s: --repeat wants a positive number\n' "$prog" >&2; exit 2; }
for o in $opts; do
	case $o in
		[0-9s]) ;;
		*) printf '%s: --opt wants levels like 0 1 2, got %s\n' "$prog" "$o" >&2; exit 2 ;;
	esac
done

command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
cleanup() { rm -rf "$work"; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

# One run of one case. The exit status is the answer and the third outcome is
# the one the original case could not distinguish from the second.
#   ok       the fault came back as a signal and the handler returned
#   nofault  nothing faulted, so the store is not in the binary
#   lost     the process did not survive to answe
one() {
	"$1" "$2" > "$work/case.out" 2>&1
	case $? in
		0) printf ok ;;
		1) printf nofault ;;
		2) printf usage ;;
		*) printf lost ;;
	esac
}

# Runs a case --repeat times and prints one word, or the counts when the
# answers disagree. A shape that answers differently between two runs of the
# same binary is a finding in itself and must not be flattened to its first
# answer.
tally() {
	bin=$1 case_=$2
	ok=0 nofault=0 lost=0 other=0
	i=0
	while [ "$i" -lt "$repeat" ]; do
		case $(one "$bin" "$case_") in
			ok) ok=$((ok + 1)) ;;
			nofault) nofault=$((nofault + 1)) ;;
			lost) lost=$((lost + 1)) ;;
			*) other=$((other + 1)) ;;
		esac
		i=$((i + 1))
	done
	if [ "$ok" = "$repeat" ]; then printf ok
	elif [ "$nofault" = "$repeat" ]; then printf nofault
	elif [ "$lost" = "$repeat" ]; then printf lost
	elif [ "$other" = "$repeat" ]; then printf usage
	else printf 'mixed(ok=%d,nofault=%d,lost=%d)' "$ok" "$nofault" "$lost"
	fi
}

note 'building the probe'
for o in $opts; do
	if [ "$keep" = 1 ]; then bin=$here/fault-probe-O$o.exe; else bin=$work/fault-probe-O$o.exe; fi
	gcc -std=gnu11 "-O$o" -g -Wall -Wextra -o "$bin" "$here/fault-probe.c" \
		>> "$work/build.log" 2>&1 || {
		cat "$work/build.log" >&2
		die "the probe did not build at -O$o"
	}
	eval "bin_$o=\$bin"
	# Whether the specimen's call survives compilation, asked of the binary
	# by symbol rather than by instruction pattern. An earlier version of
	# this grepped the assembly for the store and was wrong at two levels
	# out of three, because the store has a different shape at each one.
	# What the compiler removes here is the call, not only the store: it
	# proves the callee cannot return and drops the site that reaches it.
	if command -v objdump >/dev/null 2>&1 &&
	   objdump -d "$bin" 2>/dev/null | grep -q -e 'call.*sysv_over_ms_null'; then
		eval "call_$o=present"
	else
		eval "call_$o=elided"
	fi
done

# The observation is read from the first level built; unwind records are a
# property of what the compiler emits for the attribute, not of -O.
first=${opts%% *}
eval "firstbin=\$bin_$first"
"$firstbin" observe > "$work/observe.out" 2>&1 || true

note 'running the matrix'
: > "$work/matrix"
cases=$("$firstbin" --list | grep -v -e '^observe$')
for c in $cases; do
	row="$c"
	for o in $opts; do
		eval "bin=\$bin_$o"
		row="$row $(tally "$bin" "$c")"
	done
	printf '%s\n' "$row" >> "$work/matrix"
done

row_for() { sed -n "s/^$1 //p" "$work/matrix"; }
any() { case " $(row_for "$1") " in *" $2 "*) return 0 ;; esac; return 1; }
all() {
	got=$(row_for "$1")
	for w in $got; do [ "$w" = "$2" ] || return 1; done
	return 0
}

# The finding, and it is a classification rather than a yes or no, because the
# question the issue reports ask -- is 3.6.10's delivery different -- is
# answered by comparing two transcripts and not by either one alone. What one
# transcript can settle is which of the three failures the fault-through case
# was actually reporting.
#
#   specimen-deleted    the store is not in the binary, so nothing faulted and
#                       the case was reporting the compiler, not the host
#   crossing-holds      every shape recovered
#   crossing-partial    some shapes recovered and some lost the fault, which
#                       means the crossing depends on something the case does
#                       not control
#   crossing-lost       no shape recovered
finding=inconclusive
# The specimen's fate is read off its own row rather than out of the assembly.
# A grep for the store has to know what the store looks like at every -O level
# and in every compiler's idiom, and it was wrong at two of three the first
# time it was written. The row cannot be wrong about it: nofault means the
# probe called the function and nothing faulted, which is the store not being
# there.
null_row=$(row_for null-store)
null_kept=
i=1
for o in $opts; do
	w=$(printf '%s' "$null_row" | cut -d' ' -f$i)
	case $w in
		ok)      null_kept="$null_kept O$o=kept" ;;
		nofault) null_kept="$null_kept O$o=deleted" ;;
		*)       null_kept="$null_kept O$o=$w" ;;
	esac
	i=$((i + 1))
done
case " $null_kept " in
	*deleted*) specimen=deleted ;;
	*kept*)    specimen=kept ;;
	*)         specimen=unknown ;;
esac
call_state=
for o in $opts; do
	eval "w=\$call_$o"
	call_state="$call_state O$o=$w"
done
call_state=${call_state# }
shapes='direct-0 direct-1 direct-2 pointer-0 pointer-1 pointer-2'
n_ok=0 n_lost=0 n_other=0
for s in $shapes; do
	if all "$s" ok; then n_ok=$((n_ok + 1))
	elif all "$s" lost; then n_lost=$((n_lost + 1))
	else n_other=$((n_other + 1))
	fi
done
if [ "$n_ok" -gt 0 ] && [ "$n_lost" = 0 ] && [ "$n_other" = 0 ]; then
	finding=crossing-holds
elif [ "$n_ok" = 0 ] && [ "$n_other" = 0 ]; then
	finding=crossing-lost
else
	finding=crossing-partial
fi
# The specimen leads, when it has anything to say. A run where the store is
# gone at some level and the crossing holds at every level is not reporting a
# host that changed; it is reporting a compiler that deleted the question.
if [ "$specimen" = deleted ] && [ "$finding" = crossing-holds ]; then
	finding=specimen-deleted-crossing-holds
elif [ "$specimen" = deleted ]; then
	finding="specimen-deleted-and-$finding"
fi

{
	printf 'a fault taken beneath a System V frame\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$(gcc --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n' "$("$firstbin" --version)"
	printf 'sweep       -O levels%s, %s runs per case\n\n' \
		"$(for o in $opts; do printf ' %s' "$o"; done)" "$repeat"

	printf '== the specimen\n\n'
	printf '    does the fault happen at all:%s\n' "$null_kept"
	printf '    is the call to it in the binary: %s\n\n' "$call_state"
	printf '    This is the compiler answering, not the host. A store through a\n'
	printf '    literal null pointer is undefined behaviour, so a compiler that\n'
	printf '    can see the path may conclude the path is unreachable. What it\n'
	printf '    removes is the call site, not only the store, which is why the\n'
	printf '    second line is asked by symbol. Where the call is gone, nothing\n'
	printf '    faults and every case below it reports nofault -- which is what\n'
	printf '    the original case reported as the fault not coming back.\n\n'

	printf '== the unwind records\n\n'
	sed -e 's/^/    /' "$work/observe.out"
	printf '\n    DR-0012 measured this on gcc 7.4 and the seam depends on the\n'
	printf '    System V answer staying no. It is restated here because this runs\n'
	printf '    under a compiler that record never saw.\n\n'

	printf '== the matrix\n\n'
	printf '    %-12s' ''
	for o in $opts; do printf ' %-24s' "-O$o"; done
	printf '\n'
	while read -r line; do
		set -- $line
		name=$1
		shift
		printf '    %-12s' "$name"
		for w in "$@"; do printf ' %-24s' "$w"; done
		printf '\n'
	done < "$work/matrix"
	printf '\n    ok means the fault came back as a signal and the handler\n'
	printf '    returned. nofault means the case ran and nothing faulted, so\n'
	printf '    the fault is not in the binary. lost means the process did not\n'
	printf '    survive to answer, which is the outcome the original case could\n'
	printf '    not tell from nofault and reported as the same sentence.\n\n'
	printf '    plain, ms-only and sysv-leaf are controls: a fault in a frame the\n'
	printf '    host can walk, and a fault in a System V leaf, which the host\n'
	printf '    leaf rule describes correctly by accident. null-store is the\n'
	printf '    spike specimen. The six below it are the same fault in the same\n'
	printf '    place, differing only in how the call is reached.\n\n'

	printf '== summary\n\n'
	printf '    specimen_null_store=%s\n' "$specimen"
	printf '    specimen_by_level=%s\n' "$(printf '%s' "$null_kept" | sed -e 's/^ //' -e 's/ /,/g')"
	printf '    specimen_call_site=%s\n' "$(printf '%s' "$call_state" | tr ' ' ',')"
	sed -e 's/^/    /' "$work/observe.out"
	while read -r line; do
		set -- $line
		name=$1
		shift
		printf '    case_%s=%s\n' "$name" "$(printf '%s' "$*" | tr ' ' ',')"
	done < "$work/matrix"
	printf '    shapes_ok=%d\n' "$n_ok"
	printf '    shapes_lost=%d\n' "$n_lost"
	printf '    shapes_mixed=%d\n' "$n_other"
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi
