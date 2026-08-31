#!/usr/bin/env bash
#
# WP-56 -- the per-slice differential. A slice is done when its symbols pass a
# differential against a real el8 userland in the WP-T2 environment; this is
# that comparison, one slice at a time. Each case under diff/<slice>/*.c is a
# freestanding C program that prints the observable behaviour of some of the
# slice's symbols to stdout, one fact per line. The reference side compiles
# and runs the case on the pinned el8 image (glibc 2.28, over WSL, the same
# environment WP-T2 uses); the candidate side runs it through the wired
# veneer. A slice passes when every case prints the same lines on both sides.
#
# Usage: diff-slice.sh [-d DISTRO] [-r CMD] [-c CC] SLICE
#   -d DISTRO  WSL image supplying the reference userland [default: rocky8]
#   -r CMD     candidate runner: CMD <binary> executes a candidate build
#              [default: $ESN_RUN, else direct execution]
#   -c CC      candidate compiler [default: x86_64-elfsysvnt-linux-gnu-gcc]
# Exit: 0 every case matched, 1 a divergence, 2 usage or no cases,
#       77 the reference image was unavailable so nothing was compared.
#
# The runner and compiler are injectable so t/run-tests.sh can exercise the
# comparison machinery itself host-only (both sides host gcc, no WSL): the
# harness must be trustworthy before any slice is judged by it.

set -u
prog=diff-slice
here=$(cd "$(dirname "$0")" && pwd)

distro=${LINUX_REF_DISTRO:-rocky8}
runner=${ESN_RUN:-}
cc=${ESN_CC:-x86_64-elfsysvnt-linux-gnu-gcc}
refcc=${ESN_REF_CC:-}   # set to a host cc to bypass WSL (harness self-test)

while [ $# -gt 0 ]; do
	case $1 in
		-d) shift; distro=${1:?-d wants a distro} ;;
		-r) shift; runner=${1:?-r wants a command} ;;
		-c) shift; cc=${1:?-c wants a compiler} ;;
		-h|--help) sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-*) echo "$prog: unknown option $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done
slice=${1:?usage: diff-slice.sh [-d DISTRO] [-r CMD] [-c CC] SLICE}

cases=$(ls "$here/diff/$slice"/*.c 2>/dev/null) || true
[ -n "$cases" ] || { echo "$prog: no cases under diff/$slice" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# ref_out CASE OUT -- the case's observable lines from the reference userland.
# When the image carries gcc the case is compiled and run there; the pinned
# rocky8 image carries none, so the fallback compiles with the candidate
# compiler -- which targets el8's glibc -- and runs the binary on the image,
# where the real ld.so and the real libc supply the behaviour under test.
ref_out() {
	if [ -n "$refcc" ]; then
		"$refcc" -O0 -o "$work/ref.bin" "$1" && "$work/ref.bin" > "$2"
		return
	fi
	wsl.exe -d "$distro" -- true 2>/dev/null || return 77
	if wsl.exe -d "$distro" -- bash -lc \
		'command -v gcc >/dev/null' 2>/dev/null; then
		local w
		w=$(wsl.exe -d "$distro" -- bash -lc 'mktemp -d' 2>/dev/null | tr -d '\r')
		[ -n "$w" ] || return 77
		tr -d '\r' < "$1" | wsl.exe -d "$distro" -- bash -lc "cat > $w/case.c"
		wsl.exe -d "$distro" -- bash -lc \
			"gcc -O0 -o $w/case $w/case.c && $w/case; s=\$?; rm -rf $w; exit \$s" \
			2>/dev/null | tr -d '\r' > "$2"
		return
	fi
	"$cc" -O0 -o "$work/ref.bin" "$1" || return 1
	local wbin
	wbin=$(wsl.exe -d "$distro" -- wslpath "$(cygpath -m "$work/ref.bin")" \
		2>/dev/null | tr -d '\r')
	[ -n "$wbin" ] || return 77
	wsl.exe -d "$distro" -- bash -c "\"$wbin\"" 2>/dev/null | tr -d '\r' > "$2"
}

# cand_out CASE OUT -- the same lines from the candidate build.
cand_out() {
	"$cc" -O0 -o "$work/cand.bin" "$1" || return 1
	if [ -n "$runner" ]; then $runner "$work/cand.bin" > "$2"
	else "$work/cand.bin" > "$2"; fi
}

rc=0
n=0
for c in $cases; do
	n=$((n + 1))
	name=$(basename "$c" .c)
	if ! ref_out "$c" "$work/ref.txt"; then
		s=$?
		if [ "$s" = 77 ]; then
			echo "$prog: $slice: reference $distro unavailable; nothing compared"
			exit 77
		fi
		echo "$prog: $slice/$name: reference side failed" >&2; rc=1; continue
	fi
	if ! cand_out "$c" "$work/cand.txt"; then
		echo "$prog: $slice/$name: candidate side failed" >&2; rc=1; continue
	fi
	if diff -u "$work/ref.txt" "$work/cand.txt" > "$work/d.txt"; then
		echo "ok $slice/$name"
	else
		echo "DIVERGED $slice/$name"; sed 's/^/   /' "$work/d.txt"; rc=1
	fi
done

if [ "$rc" = 0 ]; then echo "$prog: $slice: $n case(s), all match"
else echo "$prog: $slice: divergence against $distro" >&2; fi
exit $rc
