#!/usr/bin/env bash
#
# Build the mock substrate and the conformance suite with the mingw cross
# compiler, run the suite, and report.  The suite certifies a substrate against
# doc/design/Substrate-Interface.md; here it runs against the mock, which every
# group must pass before N or H is written.  Exit 0 iff every group passes.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -q, --quiet        Suite output is failures and the summary only; the build
#                      is silent unless it breaks.
#       --no-quiet     Undo a quiet set by the environment.
#   -k, --keep         Keep the build directory instead of removing it on exit.
#       --no-keep      Undo a keep set by the environment.
#   -s, --substrate=NAME
#                      Which substrate to build with conformance.c and certify:
#                      "mock" or "n". [default: mock]
#       --cc=CC        The C compiler to build with.
#                      [default: x86_64-w64-mingw32-gcc]
#   -V, --version      Print the version and exit.
#   -h, --help         Print this message and exit.
#
# Every option also reads from an environment variable, and the option wins:
#   SUBSTRATE_CONFORMANCE_QUIET, SUBSTRATE_CONFORMANCE_KEEP,
#   SUBSTRATE_CONFORMANCE_SUBSTRATE, SUBSTRATE_CONFORMANCE_CC.
#
# Exit: 0 every group passes, 1 a group fails or the build breaks, 2 a usage
#       error, 77 the compiler is absent so nothing was built (not-checked, the
#       convention test/t3-regen.sh draws for a missing toolchain).

set -u

prog=run.sh
version='run.sh (substrate-conformance) 1.0'
here=$(cd "$(dirname "$0")" && pwd)

# --- defaults, then the environment layer ------------------------------------
truthy() { case ${1:-} in 1|true|TRUE|yes|YES|on|ON) return 0 ;; *) return 1 ;; esac; }

quiet=0
keep=0
substrate=mock
cc=x86_64-w64-mingw32-gcc
truthy "${SUBSTRATE_CONFORMANCE_QUIET:-}" && quiet=1
truthy "${SUBSTRATE_CONFORMANCE_KEEP:-}" && keep=1
substrate=${SUBSTRATE_CONFORMANCE_SUBSTRATE:-$substrate}
cc=${SUBSTRATE_CONFORMANCE_CC:-$cc}

usage() { sed -n '/^# Usage:/,/^# *Exit:/p' "$0" | sed 's/^# \{0,1\}//'; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 2; }

# --- the command line, which outranks the environment ------------------------
# Short booleans bundle (-qk); a long value takes --cc=VALUE or --cc VALUE.
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
		--substrate) shift; [ $# -gt 0 ] || die "--substrate wants a value"; substrate=$1 ;;
		--substrate=*) substrate=${1#--substrate=} ;;
		--cc) shift; [ $# -gt 0 ] || die "--cc wants a value"; cc=$1 ;;
		--cc=*) cc=${1#--cc=} ;;
		--version) echo "$version"; exit 0 ;;
		--help) usage; exit 0 ;;
		--*) die "unknown option $1" ;;
		-s) shift; [ $# -gt 0 ] || die "-s wants a value"; substrate=$1 ;;
		-s=*) substrate=${1#-s=} ;;
		-s?*) substrate=${1#-s} ;;
		-?*) set_short "${1#-}" ;;
		*) die "unexpected argument $1" ;;
	esac
	shift
done
[ $# -eq 0 ] || die "unexpected argument $1"

# Which substrate source links with conformance.c.  Both must build and certify.
case $substrate in
	mock) src=$here/mock_substrate.c ;;
	n)    src=$here/substrate_n.c ;;
	*)    die "unknown substrate $substrate (want mock or n)" ;;
esac

# --- build -------------------------------------------------------------------
if ! command -v "$cc" >/dev/null 2>&1; then
	printf '%s: %s not found; nothing built (set --cc or SUBSTRATE_CONFORMANCE_CC)\n' \
		"$prog" "$cc" >&2
	exit 77
fi

builddir=$here/.build
mkdir -p "$builddir"
cleanup() { [ "$keep" = 1 ] || rm -rf "$builddir"; }
trap cleanup EXIT

exe=$builddir/conformance.exe
cflags="-O2 -Wall -Wextra -std=gnu11"

[ "$quiet" = 1 ] || printf '%s: building substrate %s with %s\n' "$prog" "$substrate" "$cc"
if ! "$cc" $cflags -I"$here" -o "$exe" \
	"$here/conformance.c" "$src" 2> "$builddir/build.log"; then
	printf '%s: build failed\n' "$prog" >&2
	cat "$builddir/build.log" >&2
	exit 1
fi
[ -s "$builddir/build.log" ] && [ "$quiet" != 1 ] && cat "$builddir/build.log" >&2

# --- run ---------------------------------------------------------------------
qflag=
[ "$quiet" = 1 ] && qflag=-q
"$exe" $qflag
rc=$?

if [ "$rc" = 0 ]; then
	[ "$quiet" = 1 ] || printf '%s: every conformance group passes\n' "$prog"
else
	printf '%s: a conformance group failed (exit %d)\n' "$prog" "$rc" >&2
fi
[ "$keep" = 1 ] && printf '%s: build kept in %s\n' "$prog" "$builddir"
exit $rc
