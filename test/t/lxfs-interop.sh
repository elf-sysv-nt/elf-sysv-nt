#!/usr/bin/env bash
#
# Criterion 3, as DR-0102 amended it: a tree written by the kernel is read by
# WSL with identical modes, owners, symlink targets and special files, and
# the reverse holds for a tree written by WSL.  Builds lk-host and the tree
# program twice (gate and syscall), has the kernel write a tree under one
# root and WSL write one under another, reads each back with both, and
# diffs the manifests.  WSL sees the roots through a DrvFs mount made with
# the metadata option, which is what lets it carry the LX attributes.
#
# Usage:
#   lxfs-interop.sh [options]
#
# Options:
#   -q, --quiet        Only the verdict and the diffs.
#   -k, --keep         Keep the build directory, the trees and the manifests.
#       --cc=CC        The host compiler for the core.
#                      [default: x86_64-w64-mingw32-gcc]
#       --cross=CC     The cross compiler for the tree program.
#                      [default: $ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-gcc]
#                      (the triple is doc/design/target-definition.md's)
#   -V, --version      Print the version and exit.
#   -h, --help         Print this message and exit.
#
# Every option also reads from an environment variable, and the option wins:
#   LK_LXFS_QUIET, LK_LXFS_KEEP, LK_LXFS_CC, LK_LXFS_CROSS.
#
# Exit: 0 both directions agree, 1 a manifest differs or a build broke, 2 a
#       usage error, 77 a toolchain or the oracle is absent so nothing was compared.

set -u
prog=lxfs-interop.sh
version='lxfs-interop.sh (elf-sysv-nt core Phase 2) 1.0'
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
. "$root/bin/roots.sh"
truthy() { case ${1:-} in 1|true|TRUE|yes|YES|on|ON) return 0 ;; *) return 1 ;; esac; }
quiet=0; keep=0
cc=x86_64-w64-mingw32-gcc
cross=$ELFSYSVNT_PREFIX/bin/x86_64-elfsysvnt-linux-gnu-gcc
truthy "${LK_LXFS_QUIET:-}" && quiet=1
truthy "${LK_LXFS_KEEP:-}" && keep=1
cc=${LK_LXFS_CC:-$cc}
cross=${LK_LXFS_CROSS:-$cross}
usage() { sed -n '/^# Usage:/,/^#       usage error/p' "$0" | sed 's/^# \{0,1\}//'; }
say() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*"; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$version"; exit 0 ;;
		-q|--quiet) quiet=1; shift ;;
		-k|--keep) keep=1; shift ;;
		--cc=*) cc=${1#*=}; shift ;;
		--cross=*) cross=${1#*=}; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; usage >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments\n' "$prog" >&2; exit 2; }
command -v "$cross" >/dev/null 2>&1 || { printf '%s: %s not found; nothing built\n' "$prog" "$cross" >&2; exit 77; }
command -v cygpath >/dev/null 2>&1 || { printf '%s: needs a Cygwin shell to reach WSL\n' "$prog" >&2; exit 77; }

builddir=$here/.build-lxfs
# the trees hold LX reparse points (fifo, device nodes) that Cygwin's rm
# refuses; cmd's rmdir removes them
wipe() { [ -d "$1" ] || return 0; cmd /c "rmdir /s /q $(cygpath -w "$1")" >/dev/null 2>&1; rm -rf "$1" 2>/dev/null; }
wipe "$builddir"
mkdir -p "$builddir/r1" "$builddir/r2"
cleanup() { [ "$keep" = 1 ] || wipe "$builddir"; }
trap cleanup EXIT

exe=$builddir/lk-host.exe
"$root/core/build.sh" -q --cc="$cc" -o "$exe" || exit $?
say "building the tree program against the gate and against syscall"
for v in gate oracle; do
	def=
	[ $v = oracle ] && def=-DLK_ORACLE
	if ! "$cross" -nostdlib -static -no-pie -O2 -Wall -Wextra -ffreestanding $def \
		-I"$root/test/core" -o "$builddir/lxfs-tree-$v.elf" \
		"$root/test/core/lkcrt.S" "$root/test/core/lxfs-tree.c" 2> "$builddir/elf-$v.log"; then
		printf '%s: tree program (%s) build failed\n' "$prog" "$v" >&2
		cat "$builddir/elf-$v.log" >&2
		exit 1
	fi
done

# WSL sees a Windows directory through a DrvFs mount it makes itself; the
# script that does it is written to a file so no quoting crosses cmd, wsl
# and sh.  $1 is what to run inside.
wslpath_of() {
	local m; m=$(cygpath -m "$1")
	local drive; drive=$(printf '%s' "${m%%:*}" | tr 'A-Z' 'a-z')
	printf '/mnt/%s%s' "$drive" "${m#*:}"
}
orc_elf=$(wslpath_of "$builddir/lxfs-tree-oracle.elf")
wsl_run() {	# wsl_run <host-dir> <mode> <output-file>
	local win; win=$(cygpath -w "$1")
	local script=$builddir/wsl-$2-$$.sh
	printf 'mkdir -p /mnt/lk-interop || exit 70\n' > "$script"
	printf 'mount -t drvfs "%s" /mnt/lk-interop -o metadata,uid=0,gid=0,case=dir || exit 71\n' "$win" >> "$script"
	printf 'cd /mnt/lk-interop || exit 72\n%s %s; rc=$?\ncd /\numount /mnt/lk-interop\nexit $rc\n' "$orc_elf" "$2" >> "$script"
	cmd /c "wsl -d rocky8 -- sh $(wslpath_of "$script")" 2> "$3.err" | tr -d '\r' > "$3"
	return "${PIPESTATUS[0]}"
}
if ! cmd /c "wsl -d rocky8 -- true" >/dev/null 2>&1; then
	printf '%s: the rocky8 WSL instance is not reachable; nothing compared\n' "$prog" >&2
	exit 77
fi

kernel_run() {	# kernel_run <host-dir> <mode> <output-file>
	"$exe" --root="$(cygpath -w "$1")" --exe=/lxfs-tree "$(cygpath -w "$builddir/lxfs-tree-gate.elf")" "$2" 2> "$3.err" | tr -d '\r' > "$3"
	return "${PIPESTATUS[0]}"
}

pass=1
say "A: the kernel writes a tree, both read it back"
kernel_run "$builddir/r1" write "$builddir/a-write.out" || { printf '%s: the kernel could not write the tree\n' "$prog" >&2; cat "$builddir/a-write.out" "$builddir/a-write.out.err" >&2; exit 1; }
kernel_run "$builddir/r1" read "$builddir/a-kernel.txt" || pass=0
wsl_run "$builddir/r1" read "$builddir/a-wsl.txt" || { printf '%s: WSL could not read the tree (see a-wsl.txt.err)\n' "$prog" >&2; cat "$builddir/a-wsl.txt.err" >&2; pass=0; }
if diff -u "$builddir/a-kernel.txt" "$builddir/a-wsl.txt" > "$builddir/a.diff"; then
	say "A: identical, $(wc -l < "$builddir/a-kernel.txt") objects"
else
	printf '%s: A: the manifests differ (kernel on the left, WSL on the right)\n' "$prog" >&2
	cat "$builddir/a.diff" >&2
	pass=0
fi

say "B: WSL writes a tree, both read it back"
wsl_run "$builddir/r2" write "$builddir/b-write.out" || { printf '%s: WSL could not write the tree\n' "$prog" >&2; cat "$builddir/b-write.out" "$builddir/b-write.out.err" >&2; exit 1; }
wsl_run "$builddir/r2" read "$builddir/b-wsl.txt" || pass=0
kernel_run "$builddir/r2" read "$builddir/b-kernel.txt" || pass=0
if diff -u "$builddir/b-wsl.txt" "$builddir/b-kernel.txt" > "$builddir/b.diff"; then
	say "B: identical, $(wc -l < "$builddir/b-kernel.txt") objects"
else
	printf '%s: B: the manifests differ (WSL on the left, kernel on the right)\n' "$prog" >&2
	cat "$builddir/b.diff" >&2
	pass=0
fi

if diff -u "$builddir/a-kernel.txt" "$builddir/b-kernel.txt" > "$builddir/ab.diff"; then
	say "the two trees read the same whichever side wrote them"
else
	say "note: the kernel-written and WSL-written trees differ as the kernel reads them:"
	[ "$quiet" = 1 ] || cat "$builddir/ab.diff"
fi

if [ "$pass" = 1 ]; then
	say "PASS: both directions agree"
	[ "$keep" = 1 ] && say "kept in $builddir"
	exit 0
fi
printf '%s: FAIL\n' "$prog" >&2
[ "$keep" = 1 ] && say "kept in $builddir"
exit 1
