#!/usr/bin/env bash
#
# The certification bar for core/hostfs_nt.c: build core/t/hostfs-test.c with
# the mingw cross compiler and run it against a scratch directory on a local
# NTFS volume.  Exit 0 iff every check passes.
#
# Usage:
#   hostfs-test.sh [options]
#
# Options:
#   -d DIR, --dir=DIR   The scratch directory (host spelling). [default: %TEMP%\lk-hostfs-test]
#   -q, --quiet         Only the verdict.
#       --cc=CC         The host compiler. [default: x86_64-w64-mingw32-gcc]
#   -V, --version       Print the version and exit.
#   -h, --help          Print this message and exit.
#
# Exit: 0 pass, 1 fail or a build broke, 2 usage, 77 no compiler.

set -u
prog=hostfs-test.sh
version='hostfs-test.sh (elf-sysv-nt core) 1.0'
here=$(cd "$(dirname "$0")" && pwd)
dir=${LK_HOSTFS_TEST_DIR:-}
quiet=0
cc=${LK_HOSTFS_TEST_CC:-x86_64-w64-mingw32-gcc}
usage() { sed -n '/^# Usage:/,/^# Exit:/p' "$0" | sed 's/^# \{0,1\}//'; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$version"; exit 0 ;;
		-d|--dir) dir=${2:-}; shift 2 ;;
		--dir=*) dir=${1#*=}; shift ;;
		--cc=*) cc=${1#*=}; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; usage >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments\n' "$prog" >&2; exit 2; }
command -v "$cc" >/dev/null 2>&1 || { printf '%s: %s not found; nothing built\n' "$prog" "$cc" >&2; exit 77; }
if [ -z "$dir" ]; then
	tmp=${TEMP:-${TMP:-/tmp}}
	command -v cygpath >/dev/null 2>&1 && tmp=$(cygpath -u "$tmp")
	dir=$tmp/lk-hostfs-test
	command -v cygpath >/dev/null 2>&1 && dir=$(cygpath -w "$dir")
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/lk-hostfs.XXXXXX") || exit 1
trap 'rm -rf "$work"' EXIT
if ! "$cc" -std=gnu11 -O1 -Wall -Wextra -I"$here/.." -o "$work/hostfs-test.exe" \
	"$here/hostfs-test.c" "$here/../hostfs_nt.c" 2> "$work/build.log"; then
	printf '%s: build failed\n' "$prog" >&2
	cat "$work/build.log" >&2
	exit 1
fi
if [ "$quiet" = 1 ]; then
	"$work/hostfs-test.exe" -q "$dir" | tr -d '\r' | tail -1
else
	"$work/hostfs-test.exe" "$dir" | tr -d '\r'
fi
exit "${PIPESTATUS[0]}"
