#!/usr/bin/env bash
# reent-realproc-low-window 1.0 -- can a real process of the faced runtime map
# the fixed low ELF window (0x400000) through its own mmap, on the _dll_crt0
# main thread, where the foreign-child window handover was refused?
#
# The DR (this branch, faced-runtime hosting) resolved that the acceptance
# crossing hosts the faced runtime as its own process rather than handing a low
# window from a parent to a suspended cygwin child (refused, see
# spike/reent-stub-realproc-window-reconcile). It named this as the next
# measurement the resolved shape turns on. This spike drives it: builds a real
# process of the faced runtime (WP-26 crt0 + -lcygwin, the fault.c shape) and
# reports, as key=word findings, whether its own mmap places MAP_FIXED at a
# free low address and at ELF_WINDOW_BASE for bzip2's span.
#
# SKIPs (verdict yes, exit 0) when the faced DLL or the WP-26 build tree are
# absent, both being uncommitted build products.
set -u
here=$(cd "$(dirname "$0")" && pwd)
out=/c/-/repo/elf-sysv-nt/a/build/wp27-face
dll=$out/elfsysv1.dll
build=/c/-/repo/elf-sysv-nt/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }

emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
emit_lines() { while IFS= read -r ln; do emit "$ln"; done; }
[ -n "$dest" ] && : >"$dest"

emit "script  reent-realproc-low-window 1.0"
emit ""
emit "host        $(hostname)"
emit "compiler    $(gcc --version 2>/dev/null | head -1)"
emit "binutils    $(ld --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ]; then
	emit "skip=no faced DLL or WP-26 build tree; build.sh first"
	emit "verdict=yes"
	exit 0
fi

# A real process of the faced runtime: -nostdlib against the WP-26 crt0 and
# -lcygwin, so startup runs the _dll_crt0 protocol and elfsysv1.dll is the
# process's sole Cygwin runtime, its reent/cygheap up before main.
if gcc -std=gnu11 -O1 -g -Wall -Wextra -mno-red-zone -fno-stack-protector \
	-nostdlib -o "$out/lowwindow-realproc.exe" "$here/lowwindow-probe.c" \
	"$build/crt0.o" -L"$build" -lcygwin -lkernel32; then
	emit "realproc_build=ok"
else
	emit "realproc_build=failed"
	emit "verdict=no"
	exit 1
fi

# Run detached via cmd -- the faced runtime's console wedges on a host pty.
( cd "$out" && rm -f lowwindow-realproc.out \
	&& timeout 60 cmd /c "lowwindow-realproc.exe > lowwindow-realproc.out 2>&1 < NUL" )
rc=$?
if [ $rc -eq 124 ]; then
	emit "realproc_run=timeout"
	emit "verdict=no"
	exit 1
fi
grep -E '^(realproc_|  region |verdict=)' "$out/lowwindow-realproc.out" 2>/dev/null | emit_lines
grep -q '^verdict=' "$out/lowwindow-realproc.out" 2>/dev/null || emit "verdict=no"
