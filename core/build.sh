#!/usr/bin/env bash
#
# Build lk-host, the core over substrate N, with the mingw cross compiler.
# One place for the source list, so the phase-1 check and the phase-2
# differential build the same binary.
#
# Usage:
#   build.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Where to put lk-host.exe. [default: ./.build/lk-host.exe]
#       --cc=CC             The host compiler. [default: x86_64-w64-mingw32-gcc]
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Every option also reads from an environment variable, and the option wins:
#   LK_BUILD_OUTPUT, LK_BUILD_CC, LK_BUILD_QUIET.
#
# Exit: 0 built, 1 the build broke, 2 a usage error, 77 no compiler.

set -u
prog=build.sh
version='build.sh (elf-sysv-nt core) 1.0'
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
output=${LK_BUILD_OUTPUT:-$here/.build/lk-host.exe}
cc=${LK_BUILD_CC:-x86_64-w64-mingw32-gcc}
quiet=${LK_BUILD_QUIET:-0}
usage() { sed -n '/^# Usage:/,/^# Exit:/p' "$0" | sed 's/^# \{0,1\}//'; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$version"; exit 0 ;;
		-o|--output) output=${2:-}; shift 2 ;;
		--output=*) output=${1#*=}; shift ;;
		--cc=*) cc=${1#*=}; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; usage >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments\n' "$prog" >&2; exit 2; }
command -v "$cc" >/dev/null 2>&1 || { printf '%s: %s not found; nothing built\n' "$prog" "$cc" >&2; exit 77; }

srcs="$here/main.c $here/gate.c $here/gate.S $here/syscall.c \
	$here/binfmt_elf.c $here/exec.c $here/host.c $here/vma.c $here/vdso.c \
	$here/hostfs_nt.c $here/file.c $here/vfs.c $here/vfs_synth.c \
	$here/task.c $here/sys_fs.c \
	$root/substrate/substrate_n.c"
mkdir -p "$(dirname "$output")"
log=$(mktemp "${TMPDIR:-/tmp}/lk-build.XXXXXX")
[ "$quiet" = 1 ] || printf '%s: building lk-host with %s\n' "$prog" "$cc"
# shellcheck disable=SC2086
if ! "$cc" -O2 -Wall -Wextra -std=gnu11 -I"$here" -I"$root/substrate" -o "$output" $srcs 2> "$log"; then
	printf '%s: core build failed\n' "$prog" >&2
	cat "$log" >&2
	rm -f "$log"
	exit 1
fi
[ -s "$log" ] && [ "$quiet" != 1 ] && cat "$log" >&2
rm -f "$log"
exit 0
