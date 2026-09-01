#!/usr/bin/env bash
# reent-stub-libc-crossing 1.0 -- does a host->faced libc call cross when reached
# through an explicit System V thunk?
#
# spike/reent-stub-realproc-window measured the ordinary Microsoft-ABI host call
# into the faced System V libc and found it does not cross
# (ms_abi_libc_call_crosses=no), while a cygwin_internal reached through a
# sysv_abi bridge does. acceptance/reent/stub-realproc.md leaves open the
# "bounded choice": whether the stub can reach the faced libc through a per-call
# System V thunk at all, and for which calls. This spike measures that with two
# exemplars reached through the bridge shape:
#
#   sysv_thunk_reentfree_call_crosses -- strlen("abcd") returns 4 across the
#     thunk. A reent-free leaf: if it crosses, the sysv_abi thunk itself carries
#     a host->faced libc call and the ABI is not the obstacle.
#
#   sysv_thunk_stdio_call_crosses -- puts emits its line across the thunk. A
#     reent-consuming stdio body: if it does not cross while strlen does, the
#     remaining obstacle is reent/stdio bring-up (item 3 of
#     acceptance/reent/README.md), not the thunk.
#
# The findings are verdict words, reproducible; the addresses and the image base
# the window spike reports are not repeated here -- only the crossing verdicts.
#
# The faced runtime wedges on a host pty, so each probe is run detached via cmd
# with stdin from NUL, as spike/reent-stub-realproc-window and
# spike/reent-bringup do.
#
# SKIPs (verdict yes, exit 0) when the faced DLL or the WP-26 build tree are
# absent, both being uncommitted build products.
set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }
emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
[ -n "$dest" ] && : >"$dest"

# Build products under a/ are gitignored -- resolve them against the shared git
# common dir so the spike runs from a session worktree as well as the checkout.
main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
out=$main/a/build/wp27-face
dll=$out/elfsysv1.dll
build=$main/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin

emit "script  reent-stub-libc-crossing 1.0"
emit ""
emit "host        $(hostname)"
emit "compiler    $(gcc --version 2>/dev/null | head -1)"
emit "binutils    $(ld --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ]; then
	emit "skip=no faced DLL or WP-26 build tree; build them first"
	emit "verdict=yes"
	exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-libc-crossing.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

CF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib"
LIBS="$build/crt0.o -L$build -lcygwin -lkernel32 -lgcc"

if ! gcc $CF -o "$tmp/probe.exe" "$here/libc-crossing-probe.c" $LIBS 2>"$tmp/build.err"; then
	emit "probe_links=no"
	sed 's/^/    ld: /' "$tmp/build.err" | emit
	emit "verdict=no"; exit 1
fi

run_probe() { # $1 exe -> prints the newline-joined markers seen
	local base=$(basename "$1")
	cp "$1" "$out/$base"
	( cd "$out" && rm -f "$base.out" \
	  && timeout 40 cmd /c "$base > $base.out 2>&1 < NUL" ) 2>/dev/null
	tr -d '\r' < "$out/$base.out" 2>/dev/null
	rm -f "$out/$base" "$out/$base.out"
}

o=$(run_probe "$tmp/probe.exe")

# Startup must have reached main for either call to mean anything.
if ! printf '%s' "$o" | grep -q '^A:reached-main'; then
	emit "startup_reached_main=no"
	emit "sysv_thunk_reentfree_call_crosses=faulted"
	emit "sysv_thunk_stdio_call_crosses=faulted"
	emit "verdict=yes"; exit 0
fi
emit "startup_reached_main=yes"

# reent-free leaf: strlen returned 4 and control survived (C after B).
if printf '%s' "$o" | grep -q '^S:strlen-crossed:4' \
   && printf '%s' "$o" | grep -q '^C:after-strlen'; then
	emit "sysv_thunk_reentfree_call_crosses=yes"
elif printf '%s' "$o" | grep -q '^C:after-strlen'; then
	emit "sysv_thunk_reentfree_call_crosses=no"
else
	emit "sysv_thunk_reentfree_call_crosses=faulted"
fi

# reent-consuming stdio body: puts emitted its line and control survived (E).
if printf '%s' "$o" | grep -q '^P:puts-ran' \
   && printf '%s' "$o" | grep -q '^E:after-puts'; then
	emit "sysv_thunk_stdio_call_crosses=yes"
elif printf '%s' "$o" | grep -q '^E:after-puts'; then
	emit "sysv_thunk_stdio_call_crosses=no"
else
	emit "sysv_thunk_stdio_call_crosses=faulted"
fi

emit "verdict=yes"
