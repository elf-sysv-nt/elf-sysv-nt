#!/usr/bin/env bash
#
# DR-0072: nothing the host side allocates may land below the line.
#
# The contract is easy to state and was quietly broken for a session. rp_slurp
# read the image into a VirtualAlloc(NULL, ...) buffer; Windows satisfies that
# out of the lowest free region; the lowest free region was the guest's window;
# and the buffer holding the image occupied exactly the span the image needed.
# The refusal that followed named no owner, so it read as a runtime occupant
# and WP-56 parked at tier 8 on it.
#
# What makes this checkable is that the failure is visible from outside: place
# a non-PIE ET_EXEC through the crossing host and the placement either lands at
# ELF_WINDOW_BASE or it does not. This asserts that it does, and when it does
# not it prints who took the window, which is the diagnostic whose absence cost
# the session.
#
# The crossing host needs the WP-26 crt0.o and the WP-27 faced DLL, both
# uncommitted build products. Where they are absent this reports SKIP and exits
# 0, the same way the realproc suite's cross stage does; where they are present
# it is a hard check.
#
# Usage:  low-window-stub.sh [-k] [-q]
# Exit:   0 passed or skipped, 1 a build or check failed, 2 usage.
set -u

prog=low-window-stub
here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..
loader=$here/../..
repo=$(cd "$loader/.." && pwd)

keep=0
quiet=0
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  sed -n '23,24p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-k|--keep)  keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--)         shift; break ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
face=$main/a/build/wp27-face
cygbuild=$main/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin

if [ ! -f "$face/elfsysv1.dll" ] || [ ! -f "$cygbuild/crt0.o" ]; then
	say "$prog: SKIP  no faced DLL or WP-26 crt0.o; the crossing host cannot be built here"
	exit 0
fi

export PATH="/c/-/x-elfsysvnt/bin:$PATH"
xg=x86_64-elfsysvnt-linux-gnu-gcc
command -v "$xg" >/dev/null 2>&1 || fail "cross gcc $xg not on PATH"

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp56lw.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

say "$prog: build the crossing host"
bash "$exec_dir/crossing-host/build-host.sh" >/dev/null \
	|| fail "the crossing host did not build"

# A non-PIE ET_EXEC, which is the only image shape that makes the window's
# occupancy observable: a PIE would take the free high arena and place cleanly
# whatever was sitting at 0x400000.
say "$prog: build the fixed-address specimen"
$xg -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
	-Wl,-z,max-page-size=0x10000 -o "$bin/fixed" "$here/specimen.S" \
	|| fail "the fixed-address specimen did not build"

out=$bin/drive.out
bash "$exec_dir/crossing-host/drive.sh" "$bin/fixed" > "$out" 2>&1

rc=0
dump() { grep -v '^$' "$out" | sed 's/^/                /'; }

# The invariant, checked where it lives rather than through its symptom. A
# host-side allocation that lands below the line only collides when the guest's
# segments happen to reach it, and Windows does not place a bottom-up
# allocation deterministically, so asserting on the placement alone would pass
# most runs with the rule broken. The buffer's own address does not vary.
line=$((0x400000 + 0x3FC00000))
buf=$(sed -n 's/.*image buffer at 0x\([0-9a-f]*\) .*/\1/p' "$out" | sed -n '1p')
if [ -z "$buf" ]; then
	say "    FAILED    low-window: the host printed no image-buffer address"
	dump; rc=1
elif [ $((0x$buf)) -ge "$line" ]; then
	say "    ok        low-window: the image buffer took memory above the line (0x$buf)"
else
	say "    FAILED    low-window: the image buffer took the guest's window (0x$buf)"
	dump; rc=1
fi

# And the symptom, which is what a reader recognises. It is the weaker of the
# two checks and it is here because a passing invariant with a failing
# placement would mean something else took the window.
if grep -q 'mapped at 0x400000' "$out"; then
	say "    ok        low-window: placement landed at ELF_WINDOW_BASE"
else
	say "    FAILED    low-window: placement did not land at ELF_WINDOW_BASE"
	sed -n 's/.*is occupied by \(.*\); the host.*/                who took it: \1/p' "$out" \
		| sed -n '1p'
	dump; rc=1
fi

say ""
if [ "$rc" = 0 ]; then say "$prog: ok"; else fail "the low window was not the guest's"; fi
