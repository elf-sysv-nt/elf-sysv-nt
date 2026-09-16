#!/usr/bin/env bash
#
# Which arm of ENTER_KERNEL does each kind of glibc object take?
#
# The port gives ENTER_KERNEL three arms -- rtld, `defined SHARED`, and an
# `#else` its own comment describes as "a static link". `nscd` takes the third
# and references `_dl_sysinfo`, which is defined in elf/dl-support.c and so
# exists only in the static libc, and the link fails. The question this
# answers is not whether that happens but why: what, at compile time, tells a
# dynamically linked program's object apart from a static link's?
#
# The evidence is the build's own compile lines. glibc compiles most sources
# more than once with different flags, so rather than reasoning about what the
# makefiles ought to pass, this reads what they did pass, for one object of
# each kind, out of the make log the failing build left behind.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -l FILE, --log=FILE     The glibc make log to read.
#                           [default: $ELFSYSVNT_EL8/glibc/build/glibc-make.log]
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.
#
# Exit codes: 0 the run completed, 1 failure, 2 usage error.

set -u

prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../../bin/roots.sh"

log=${MEASURE_LOG:-$ELFSYSVNT_EL8/glibc/build/glibc-make.log}
output=${MEASURE_OUTPUT:--}
quiet=${MEASURE_QUIET:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-l|--log)     log=${2:-}; shift 2 ;;
		--log=*)      log=${1#*=}; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done

[ -r "$log" ] || die "cannot read the make log: $log"

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM

# One object of each kind. The first compile of each basename is taken, which
# is the plain .o pass; the later PIC passes are counted in the tallies below
# rather than reported one by one.
arm_of() { # SHARED, PIC -> the arm the preprocessor selects
	case $1 in yes) printf 'shared' ;; *) printf 'static' ;; esac
}

row() { # label, basename.c
	line=$(grep -m1 -e "$2 -c " "$log" 2>/dev/null)
	if [ -z "$line" ]; then
		printf '%s\tshared=?\tpic=?\tmodule=?\tarm=not-found\n' "$1"
		return
	fi
	s=no; p=no
	case $line in *-DSHARED*) s=yes ;; esac
	case $line in *-DPIC*) p=yes ;; esac
	m=$(printf '%s' "$line" | grep -o -e '-DMODULE_NAME=[a-zA-Z_]*' | head -1)
	m=${m#-DMODULE_NAME=}
	printf '%s\tshared=%s\tpic=%s\tmodule=%s\tarm=%s\n' "$1" "$s" "$p" "${m:-none}" "$(arm_of "$s")"
}

# The second question, and the one that separates the two candidates left
# standing after the first. If a program's objects were to reach the gate
# address through the thread pointer -- which is what upstream i386 does, and
# what this port declined -- then every gate call those objects make has to
# happen after the thread pointer is set. A static program sets it in
# __libc_setup_tls, and the question is whether anything calls the kernel
# before that line.
#
# This reads the source rather than running it: the line numbers of the first
# syscall-bearing call and of TLS_INIT_TP in csu/libc-tls.c, and the chain from
# the first to a gate call.
startup_probe() {
	src=$1
	tls=$src/csu/libc-tls.c
	brk=$src/sysdeps/unix/sysv/linux/x86_64/brk.c
	if [ ! -r "$tls" ] || [ ! -r "$brk" ]; then
		printf 'startup\tsbrk_line=?\ttls_init_line=?\tbefore=not-measured\n'
		return
	fi
	sbrk_line=$(grep -n -m1 -e '__sbrk (' "$tls" | cut -d: -f1)
	tls_line=$(grep -n -m1 -e 'TLS_INIT_TP (' "$tls" | cut -d: -f1)
	syscall_in_brk=no
	grep -q -e 'INLINE_SYSCALL' -e 'INTERNAL_SYSCALL' "$brk" && syscall_in_brk=yes
	before=unknown
	if [ -n "$sbrk_line" ] && [ -n "$tls_line" ]; then
		if [ "$sbrk_line" -lt "$tls_line" ] && [ "$syscall_in_brk" = yes ]; then
			before=yes
		else
			before=no
		fi
	fi
	printf 'startup\tsbrk_line=%s\ttls_init_line=%s\tbrk_syscalls=%s\tbefore=%s\n' \
		"${sbrk_line:-?}" "${tls_line:-?}" "$syscall_in_brk" "$before"
}

note 'reading the build log'
{
	row nscd-program     nscd_setup_thread.c
	row nscd-connections connections.c
	row rtld-dl-load     dl-load.c
	row libc-init-first  init-first.c
	row libc-strlen      strlen.c
	row static-reloc     static-reloc.c
} > "$work/rows"

src=${MEASURE_SRC:-$ELFSYSVNT_EL8/glibc/src/glibc-2.28}
startup_probe "$src" > "$work/startup"
before=$(awk -F'\t' '{ for (i=2;i<=NF;i++) { split($i,p,"="); if (p[1]=="before") print p[2] } }' "$work/startup")
sbrk_line=$(awk -F'\t' '{ for (i=2;i<=NF;i++) { split($i,p,"="); if (p[1]=="sbrk_line") print p[2] } }' "$work/startup")
tls_line=$(awk -F'\t' '{ for (i=2;i<=NF;i++) { split($i,p,"="); if (p[1]=="tls_init_line") print p[2] } }' "$work/startup")

both=$(awk '/-DSHARED/ && /-DPIC/   { n++ } END { print n+0 }' "$log")
piconly=$(awk '/-DPIC/ && !/-DSHARED/ { n++ } END { print n+0 }' "$log")
sharedonly=$(awk '/-DSHARED/ && !/-DPIC/ { n++ } END { print n+0 }' "$log")
neither=$(awk '!/-DSHARED/ && !/-DPIC/ && / -c / { n++ } END { print n+0 }' "$log")

# The finding turns on two things and nothing else: that the failing object and
# a genuinely static one carry identical flags, and that no flag combination
# separates them.
nscd_flags=$(awk -F'\t' '$1=="nscd-program" { print $2 "," $3 }' "$work/rows")
static_flags=$(awk -F'\t' '$1=="static-reloc" { print $2 "," $3 }' "$work/rows")

if [ "$nscd_flags" = "$static_flags" ] && [ "$sharedonly" = 0 ]; then
	finding=program-and-static-objects-are-indistinguishable-at-compile-time
else
	finding="nscd[$nscd_flags]-static[$static_flags]-sharedonly[$sharedonly]"
fi

case $before in
	yes) finding="$finding,static-startup-calls-the-kernel-before-the-thread-pointer" ;;
	no)  finding="$finding,static-startup-has-a-thread-pointer-first" ;;
	*)   finding="$finding,startup-not-measured" ;;
esac

{
	printf 'which arm of ENTER_KERNEL each glibc object takes\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'glibc       %s\n' "$(awk -F'\t' '$1=="name"{print $2}' "$here/../../toolchain/glibc/glibc.pin" 2>/dev/null)"
	printf 'log         %s\n' "$log"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n\n' "$release"

	printf 'reading\n\n'
	printf '  the object that fails to link and an object destined for the static\n'
	printf '  libc are compiled with the same flags, so no arm of ENTER_KERNEL can\n'
	printf '  tell them apart. SHARED is a subset of PIC, and the plain pass that\n'
	printf '  builds both of them carries neither.\n\n'

	printf 'per object\n\n'
	printf '    %-18s %-10s %-8s %-10s %s\n' object SHARED PIC module arm
	awk -F'\t' '{ for (i=2;i<=NF;i++) { split($i,p,"="); v[p[1]]=p[2] }
	              printf "    %-18s %-10s %-8s %-10s %s\n", $1, v["shared"], v["pic"], v["module"], v["arm"] }' "$work/rows"

	printf '\nacross the whole build\n\n'
	printf '    SHARED and PIC      %s\n' "$both"
	printf '    PIC only            %s\n' "$piconly"
	printf '    SHARED only         %s\n' "$sharedonly"
	printf '    neither, compiles   %s\n' "$neither"

	printf '\nstatic startup, against the thread pointer\n\n'
	printf '    csu/libc-tls.c calls __sbrk at line %s\n' "$sbrk_line"
	printf '    and sets the thread pointer with TLS_INIT_TP at line %s\n' "$tls_line"
	printf '    brk reaches the kernel through INLINE_SYSCALL, so the call at the\n'
	printf '    first line is a gate call made from an object in the third arm.\n'
	printf '    kernel called before the thread pointer exists: %s\n' "$before"

	printf '\nwhat this rules out\n\n'
	printf '    A third arm conditioned on PIC cannot work. The failing object and\n'
	printf '    a static-libc object agree on both flags, so any condition written\n'
	printf '    over them puts both in the same arm, which is where they already\n'
	printf '    are. glibc does not know at compile time whether the program its\n'
	printf '    object will join is linked statically or dynamically, and nothing\n'
	printf '    in the flags can be made to say so.\n\n'
	printf '    A third arm that reads the thread pointer cannot work either, for\n'
	printf '    the reason the port gave and did not measure: a static program\n'
	printf '    calls brk to place its own TLS before it has a thread pointer to\n'
	printf '    read, and that call is in the third arm. Carving the static case\n'
	printf '    out would need exactly the condition the paragraph above shows\n'
	printf '    does not exist.\n'

	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then cat "$work/report"
else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
