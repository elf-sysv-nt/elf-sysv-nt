#!/usr/bin/env bash
#
# Build the Phase 1 core (lk-host) and the static test ELF, run the ELF under
# the core, and check it against the bar: stdout exactly "hello" and exit 0,
# validated against the same logical program on the Rocky 8 oracle.  The core is
# built with the mingw cross compiler, linking the certified substrate N; the
# test ELF is built with the elfsysvnt cross toolchain.  Exit 0 iff the bar is
# met.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -q, --quiet        Only the verdict and any failure detail; build is silent
#                      unless it breaks.
#       --no-quiet     Undo a quiet set by the environment.
#   -k, --keep         Keep the build directory instead of removing it on exit.
#       --no-keep      Undo a keep set by the environment.
#       --oracle       Run the Rocky 8 comparison. [default: on]
#       --no-oracle    Skip it (build and core only).
#       --cc=CC        The host compiler for the core.
#                      [default: x86_64-w64-mingw32-gcc]
#       --cross=CC     The cross compiler for the test ELF.
#                      [default: $ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-gcc]
#   -V, --version      Print the version and exit.
#   -h, --help         Print this message and exit.
#
# Every option also reads from an environment variable, and the option wins:
#   LK_RUN_QUIET, LK_RUN_KEEP, LK_RUN_ORACLE, LK_RUN_CC, LK_RUN_CROSS.
#
# Exit: 0 the bar is met, 1 the bar is missed or a build breaks, 2 a usage
#       error, 77 a required toolchain is absent so nothing was built.

set -u

prog=run.sh
version='run.sh (elf-sysv-nt core Phase 1) 1.0'
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
. "$root/bin/roots.sh"

truthy() { case ${1:-} in 1|true|TRUE|yes|YES|on|ON) return 0 ;; *) return 1 ;; esac; }

quiet=0
keep=0
oracle=1
cc=x86_64-w64-mingw32-gcc
cross=$ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-gcc
truthy "${LK_RUN_QUIET:-}" && quiet=1
truthy "${LK_RUN_KEEP:-}" && keep=1
[ "${LK_RUN_ORACLE:-}" = 0 ] && oracle=0
cc=${LK_RUN_CC:-$cc}
cross=${LK_RUN_CROSS:-$cross}

usage() { sed -n '/^# Usage:/,/^# *error, 77/p' "$0" | sed 's/^# \{0,1\}//'; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 2; }
say() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*"; }

set_short() {
	local c
	while [ -n "$1" ]; do
		c=${1:0:1}; set -- "${1:1}"
		case $c in
			q) quiet=1 ;;
			k) keep=1 ;;
			V) echo "$version"; exit 0 ;;
			h) usage; exit 0 ;;
			*) die "unknown option -$c" ;;
		esac
	done
}

while [ $# -gt 0 ]; do
	case $1 in
		--) shift; break ;;
		--quiet) quiet=1 ;;
		--no-quiet) quiet=0 ;;
		--keep) keep=1 ;;
		--no-keep) keep=0 ;;
		--oracle) oracle=1 ;;
		--no-oracle) oracle=0 ;;
		--cc) shift; [ $# -gt 0 ] || die "--cc wants a value"; cc=$1 ;;
		--cc=*) cc=${1#--cc=} ;;
		--cross) shift; [ $# -gt 0 ] || die "--cross wants a value"; cross=$1 ;;
		--cross=*) cross=${1#--cross=} ;;
		--version) echo "$version"; exit 0 ;;
		--help) usage; exit 0 ;;
		--*) die "unknown option $1" ;;
		-?*) set_short "${1#-}" ;;
		*) die "unexpected argument $1" ;;
	esac
	shift
done
[ $# -eq 0 ] || die "unexpected argument $1"

if ! command -v "$cc" >/dev/null 2>&1; then
	printf '%s: %s not found; nothing built\n' "$prog" "$cc" >&2
	exit 77
fi
if ! command -v "$cross" >/dev/null 2>&1; then
	printf '%s: %s not found; nothing built\n' "$prog" "$cross" >&2
	exit 77
fi

builddir=$here/.build
mkdir -p "$builddir"
cleanup() { [ "$keep" = 1 ] || rm -rf "$builddir"; }
trap cleanup EXIT

exe=$builddir/lk-host.exe
elf=$builddir/hello.elf

# --- build the core ----------------------------------------------------------
say "building the core with $cc"
core_srcs="$here/main.c $here/gate.c $here/gate.S $here/syscall.c \
	$here/binfmt_elf.c $here/exec.c $here/host.c \
	$root/substrate/substrate_n.c"
if ! "$cc" -O2 -Wall -Wextra -std=gnu11 \
	-I"$here" -I"$root/substrate" \
	-o "$exe" $core_srcs 2> "$builddir/core.log"; then
	printf '%s: core build failed\n' "$prog" >&2
	cat "$builddir/core.log" >&2
	exit 1
fi
[ -s "$builddir/core.log" ] && [ "$quiet" != 1 ] && cat "$builddir/core.log" >&2

# --- build the test ELF ------------------------------------------------------
say "building the test ELF with $cross"
if ! "$cross" -nostdlib -static -no-pie -o "$elf" \
	"$root/test/core/hello.S" 2> "$builddir/elf.log"; then
	printf '%s: test ELF build failed\n' "$prog" >&2
	cat "$builddir/elf.log" >&2
	exit 1
fi

# --- run the ELF under the core ----------------------------------------------
say "running the ELF under the core"
# lk-host is a native Windows binary, so it opens the path its own runtime
# understands, not the POSIX one this shell writes. Convert where cygpath
# exists and leave the path alone where it does not, so the same script works
# from a plain Linux shell once the core builds there.
elf_arg=$elf
command -v cygpath >/dev/null 2>&1 && elf_arg=$(cygpath -w "$elf")
core_out=$("$exe" "$elf_arg"); core_rc=$?
core_norm=$(printf '%s' "$core_out" | tr -d '\r')

printf '%s: core stdout=[%s] exit=%d\n' "$prog" "$core_norm" "$core_rc"

pass=1
[ "$core_norm" = "hello" ] || { pass=0; printf '%s: FAIL stdout is not "hello"\n' "$prog" >&2; }
[ "$core_rc" = 0 ] || { pass=0; printf '%s: FAIL exit code is not 0\n' "$prog" >&2; }

# --- oracle comparison -------------------------------------------------------
if [ "$oracle" = 1 ]; then
	say "comparing against the Rocky 8 oracle"
	orc_out=$(cmd /c "wsl -d rocky8 -- /bin/echo hello" 2>/dev/null); orc_rc=$?
	orc_norm=$(printf '%s' "$orc_out" | tr -d '\r\n')
	printf '%s: oracle stdout=[%s] exit=%d\n' "$prog" "$orc_norm" "$orc_rc"
	[ "$orc_norm" = "hello" ] || { pass=0; printf '%s: FAIL oracle stdout differs\n' "$prog" >&2; }
	[ "$orc_rc" = 0 ] || { pass=0; printf '%s: FAIL oracle exit code is not 0\n' "$prog" >&2; }
	[ "$core_norm" = "$orc_norm" ] || { pass=0; printf '%s: FAIL core and oracle stdout differ\n' "$prog" >&2; }
fi

[ "$keep" = 1 ] && say "build kept in $builddir"
if [ "$pass" = 1 ]; then
	say "PASS: hello, exit 0, oracle matched"
	exit 0
fi
printf '%s: the bar was not met\n' "$prog" >&2
exit 1
