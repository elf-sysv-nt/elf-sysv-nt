#!/usr/bin/env bash
#
# Criterion 2: a scripted sequence of file operations produces the same trace
# of results and errnos under the core as on the el8 reference.  Builds
# lk-host and the trace program twice (once against the gate, once against
# the syscall instruction), runs the first under the core over a fresh root
# directory and the second under the Rocky 8 oracle in a fresh directory, and
# diffs the two traces.  Exit 0 iff they are identical.
#
# Usage:
#   vfs-diff.sh [options]
#
# Options:
#   -q, --quiet        Only the verdict and the diff.
#   -k, --keep         Keep the build directory and both traces.
#       --no-oracle    Run the core only and print its trace (no verdict).
#       --cc=CC        The host compiler for the core.
#                      [default: x86_64-w64-mingw32-gcc]
#       --cross=CC     The cross compiler for the trace program.
#                      [default: $ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-gcc]
#   -V, --version      Print the version and exit.
#   -h, --help         Print this message and exit.
#
# Every option also reads from an environment variable, and the option wins:
#   LK_VFS_QUIET, LK_VFS_KEEP, LK_VFS_ORACLE, LK_VFS_CC, LK_VFS_CROSS.
#
# Exit: 0 the traces match, 1 they differ or a build broke, 2 a usage error,
#       77 a required toolchain or the oracle is absent so nothing was compared.

set -u
prog=vfs-diff.sh
version='vfs-diff.sh (elf-sysv-nt core Phase 2) 1.0'
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
. "$root/bin/roots.sh"
truthy() { case ${1:-} in 1|true|TRUE|yes|YES|on|ON) return 0 ;; *) return 1 ;; esac; }
quiet=0; keep=0; oracle=1
cc=x86_64-w64-mingw32-gcc
cross=$ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-gcc
truthy "${LK_VFS_QUIET:-}" && quiet=1
truthy "${LK_VFS_KEEP:-}" && keep=1
[ "${LK_VFS_ORACLE:-}" = 0 ] && oracle=0
cc=${LK_VFS_CC:-$cc}
cross=${LK_VFS_CROSS:-$cross}
usage() { sed -n '/^# Usage:/,/^#       77/p' "$0" | sed 's/^# \{0,1\}//'; }
say() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*"; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$version"; exit 0 ;;
		-q|--quiet) quiet=1; shift ;;
		-k|--keep) keep=1; shift ;;
		--no-oracle) oracle=0; shift ;;
		--cc=*) cc=${1#*=}; shift ;;
		--cross=*) cross=${1#*=}; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; usage >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments\n' "$prog" >&2; exit 2; }
command -v "$cross" >/dev/null 2>&1 || { printf '%s: %s not found; nothing built\n' "$prog" "$cross" >&2; exit 77; }

builddir=$here/.build-vfs
rm -rf "$builddir"
mkdir -p "$builddir/root"
cleanup() { [ "$keep" = 1 ] || rm -rf "$builddir"; }
trap cleanup EXIT

exe=$builddir/lk-host.exe
"$root/core/build.sh" -q --cc="$cc" -o "$exe" || exit $?

say "building the trace program against the gate and against syscall"
for v in gate oracle; do
	def=
	[ $v = oracle ] && def=-DLK_ORACLE
	if ! "$cross" -nostdlib -static -no-pie -O2 -Wall -Wextra -ffreestanding $def \
		-I"$root/test/core" -o "$builddir/vfs-trace-$v.elf" \
		"$root/test/core/lkcrt.S" "$root/test/core/vfs-trace.c" 2> "$builddir/elf-$v.log"; then
		printf '%s: trace program (%s) build failed\n' "$prog" "$v" >&2
		cat "$builddir/elf-$v.log" >&2
		exit 1
	fi
done

say "running under the core"
elf_arg=$builddir/vfs-trace-gate.elf
root_arg=$builddir/root
if command -v cygpath >/dev/null 2>&1; then
	elf_arg=$(cygpath -w "$elf_arg")
	root_arg=$(cygpath -w "$root_arg")
fi
"$exe" --root="$root_arg" --exe=/vfs-trace "$elf_arg" > "$builddir/core.raw" 2> "$builddir/core.err"
core_rc=$?
tr -d '\r' < "$builddir/core.raw" > "$builddir/core.trace"
[ -s "$builddir/core.err" ] && [ "$quiet" != 1 ] && cat "$builddir/core.err" >&2
say "core exit=$core_rc, $(wc -l < "$builddir/core.trace") trace lines"

if [ "$oracle" = 0 ]; then
	cat "$builddir/core.trace"
	exit 0
fi

say "running under the Rocky 8 oracle"
orc_win=$builddir/vfs-trace-oracle.elf
command -v cygpath >/dev/null 2>&1 && orc_win=$(cygpath -w "$orc_win")
orc_path=$(cmd /c "wsl -d rocky8 -- wslpath -a '$orc_win'" 2>/dev/null | tr -d '\r')
if [ -z "$orc_path" ]; then
	printf '%s: the rocky8 WSL instance is not reachable; nothing compared\n' "$prog" >&2
	exit 77
fi
# a fresh directory on the oracle's own file system, entered with --cd so no
# shell quoting has to cross cmd, wsl and sh
orc_dir=$(cmd /c "wsl -d rocky8 -- mktemp -d /tmp/vfs-trace.XXXXXX" 2>/dev/null | tr -d '\r')
[ -n "$orc_dir" ] || { printf '%s: cannot make a directory on the oracle\n' "$prog" >&2; exit 77; }
cmd /c "wsl -d rocky8 --cd $orc_dir -- $orc_path" > "$builddir/oracle.raw" 2> "$builddir/oracle.err"
orc_rc=$?
cmd /c "wsl -d rocky8 -- rm -rf $orc_dir" >/dev/null 2>&1
tr -d '\r' < "$builddir/oracle.raw" > "$builddir/oracle.trace"
say "oracle exit=$orc_rc, $(wc -l < "$builddir/oracle.trace") trace lines"

if diff -u "$builddir/oracle.trace" "$builddir/core.trace" > "$builddir/trace.diff" && [ "$core_rc" = "$orc_rc" ]; then
	say "PASS: the traces match ($(wc -l < "$builddir/core.trace") lines), exit codes $core_rc"
	[ "$keep" = 1 ] && say "kept in $builddir"
	exit 0
fi
printf '%s: FAIL: the traces differ (oracle on the left, core on the right)\n' "$prog" >&2
cat "$builddir/trace.diff" >&2
[ "$core_rc" = "$orc_rc" ] || printf '%s: exit codes differ: core %s, oracle %s\n' "$prog" "$core_rc" "$orc_rc" >&2
[ "$keep" = 1 ] && say "kept in $builddir"
exit 1
