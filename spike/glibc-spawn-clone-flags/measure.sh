#!/usr/bin/env bash
#
# What el8's glibc 2.28 actually asks the kernel for when a program calls
# posix_spawn or vfork: which clone flags, which syscall.
#
# Proposal 0011 § 6 says clone "with CLONE_VFORK but without CLONE_VM is the
# vfork glibc's posix_spawn uses", implements it as fork plus wait, and returns
# EINVAL for every other combination. Whether that sentence is true of the
# shipped libc decides whether posix_spawn works at all under substrate H,
# which runs that libc unmodified, and what the sysdeps port has to patch
# under N. This reads the answer out of the vendor binary rather than out of
# memory: it disassembles libc-2.28.so from the pinned el8 RPM and finds the
# flags __spawnix hands __clone and the number __vfork puts in %rax.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -L FILE, --libc=FILE    The libc-2.28.so to read.
#                           [default: $ELFSYSVNT_EL8/vendor-image-shape/ref/usr/lib64/libc-2.28.so]
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

libc=${MEASURE_LIBC:-$ELFSYSVNT_EL8/vendor-image-shape/ref/usr/lib64/libc-2.28.so}
output=${MEASURE_OUTPUT:--}
quiet=${MEASURE_QUIET:-0}
objdump=${MEASURE_OBJDUMP:-$ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-objdump}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)     usage; exit 0 ;;
		-V|--version)  printf '%s\n' "$release"; exit 0 ;;
		-L|--libc)     libc=${2:-}; shift 2 ;;
		--libc=*)      libc=${1#*=}; shift ;;
		-o|--output)   output=${2:-}; shift 2 ;;
		--output=*)    output=${1#*=}; shift ;;
		-q|--quiet)    quiet=1; shift ;;
		--)            shift; break ;;
		-?*)           printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)             break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

[ -r "$libc" ] || die "no libc at $libc (spike/vendor-image-shape stages it from the pinned RPM)"
command -v "$objdump" >/dev/null 2>&1 || command -v "$objdump.exe" >/dev/null 2>&1 || die "no objdump at $objdump"

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM

note "disassembling $(basename "$libc")"
"$objdump" -d --no-show-raw-insn "$libc" > "$work/libc.dis" 2>"$work/objdump.err" ||
	{ cat "$work/objdump.err" >&2; die 'objdump failed'; }

# One function's body: from its label to the next label.
body() { awk -v f="<$1>:" '$0 ~ f {p=1; next} p && /^[0-9a-f]+ <.*>:$/ {exit} p' "$work/libc.dis"; }

# q1. posix_spawn and posix_spawnp both call __spawni; __spawni is a wrapper
# over __spawnix; __spawnix calls __clone with the flags in %edx (the third
# argument). CLONE_VM is 0x100, CLONE_VFORK 0x4000, SIGCHLD 0x11.
spawn_calls_spawni=$(body 'posix_spawn@@GLIBC_2.15' | grep -c 'call.*<__spawni>')
spawnp_calls_spawni=$(body 'posix_spawnp@@GLIBC_2.15' | grep -c 'call.*<__spawni>')
spawni_calls_spawnix=$(body '__spawni' | grep -c 'call.*<__spawnix>\|jmp.*<__spawnix>')
spawnix_clone=$(body '__spawnix' | grep -c 'call.*<__clone>')
# the flags: the last mov into %edx before the call to __clone
spawnix_flags_hex=$(body '__spawnix' | awk '/mov +\$0x[0-9a-f]+,%edx/ {f=$0} /call.*<__clone>/ {print f; exit}' \
	| sed -n 's/.*\$\(0x[0-9a-f]*\),%edx.*/\1/p')
case $spawnix_flags_hex in 0x*) flags_val=$((spawnix_flags_hex)) ;; *) flags_val=0 ;; esac
has_vm=$(( (flags_val & 0x100) != 0 ))
has_vfork=$(( (flags_val & 0x4000) != 0 ))
has_sigchld=$(( (flags_val & 0xff) == 0x11 ))
child_fn=$(body '__spawnix' | grep -o '<__spawni_child>' | head -1)

# q2. __vfork: the syscall number in %eax before the syscall instruction.
vfork_nr_hex=$(body '__vfork' | awk '/mov +\$0x[0-9a-f]+,%eax/ {n=$0} /syscall/ {print n; exit}' \
	| sed -n 's/.*\$\(0x[0-9a-f]*\),%eax.*/\1/p')
case $vfork_nr_hex in 0x*) vfork_val=$((vfork_nr_hex)) ;; *) vfork_val=0 ;; esac
vfork_pops_ret=$(body '__vfork' | awk 'NR<=3' | grep -c 'pop *%rdi')
vfork_pushes_ret=$(body '__vfork' | grep -c 'push *%rdi')
vfork_exported=$(grep -c '<vfork@@GLIBC_2.2.5>:\|<__vfork>:' "$work/libc.dis")

if [ "$spawn_calls_spawni" -ge 1 ] && [ "$spawni_calls_spawnix" -ge 1 ] && [ "$spawnix_clone" -ge 1 ] \
   && [ "$has_vm" = 1 ] && [ "$has_vfork" = 1 ]; then w1=posix-spawn-clones-with-vm-and-vfork
elif [ "$spawnix_clone" -ge 1 ] && [ "$has_vfork" = 1 ]; then w1=posix-spawn-clones-vfork-without-vm
elif [ "$spawnix_clone" -ge 1 ]; then w1="posix-spawn-clones-with-${spawnix_flags_hex:-unknown}"
else w1=posix-spawn-path-not-found; fi
if [ "$vfork_val" = 58 ]; then w2=vfork-is-syscall-58; else w2="vfork-syscall-${vfork_nr_hex:-unknown}"; fi
finding="$w1,$w2"

{
	printf 'what glibc 2.28 asks the kernel for in posix_spawn and vfork\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'libc        %s\n' "$libc"
	printf 'objdump     %s\n' "$("$objdump" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n\n' "$release"

	printf 'reading, question by question\n\n'
	printf '  q1  posix_spawn -> __spawni -> __spawnix -> __clone: %s\n' "$w1"
	printf '      flags %s: CLONE_VM %s, CLONE_VFORK %s, exit signal SIGCHLD %s; child function %s\n' \
		"${spawnix_flags_hex:-none}" "$([ "$has_vm" = 1 ] && echo yes || echo no)" \
		"$([ "$has_vfork" = 1 ] && echo yes || echo no)" "$([ "$has_sigchld" = 1 ] && echo yes || echo no)" \
		"${child_fn:-none}"
	printf '  q2  __vfork: %s; return address popped before and pushed after the instruction: %s/%s\n\n' \
		"$w2" "$vfork_pops_ret" "$vfork_pushes_ret"

	printf 'raw\n\n'
	printf '    posix_spawn_calls_spawni=%s\n' "$spawn_calls_spawni"
	printf '    posix_spawnp_calls_spawni=%s\n' "$spawnp_calls_spawni"
	printf '    spawni_reaches_spawnix=%s\n' "$spawni_calls_spawnix"
	printf '    spawnix_calls_clone=%s\n' "$spawnix_clone"
	printf '    spawnix_clone_flags=%s\n' "${spawnix_flags_hex:-none}"
	printf '    spawnix_clone_vm=%s\n' "$has_vm"
	printf '    spawnix_clone_vfork=%s\n' "$has_vfork"
	printf '    spawnix_clone_sigchld=%s\n' "$has_sigchld"
	printf '    spawnix_child=%s\n' "${child_fn:-none}"
	printf '    vfork_symbols=%s\n' "$vfork_exported"
	printf '    vfork_syscall_nr=%s\n' "${vfork_nr_hex:-none}"
	printf '    vfork_pops_return_address=%s\n' "$vfork_pops_ret"
	printf '    vfork_pushes_return_address=%s\n' "$vfork_pushes_ret"
	printf '\n    __spawnix, the call and what feeds it:\n'
	body '__spawnix' | grep -B5 'call.*<__clone>' | sed 's/^/        /'
	printf '\n    __vfork, the first six instructions:\n'
	body '__vfork' | head -6 | sed 's/^/        /'

	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi
