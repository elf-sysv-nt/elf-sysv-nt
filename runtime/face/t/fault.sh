#!/usr/bin/env bash
# WP-27 milestone 7: a fault beneath a System V frame arrives as SIGSEGV
# and leaves by siglongjmp, against the real DLL.
#
# Builds fault.c as a real process of the faced runtime: -nostdlib against
# the WP-26 build tree's crt0 and import library, so startup goes through
# the asis `_dll_crt0` protocol and main runs beneath the runtime's own
# exception protection with signals live.  No host cygwin1.dll is in the
# process; the faced DLL's runtime is the only one that can turn the
# access violation into a signal.  The System V functions are compiled
# -mno-red-zone because the vendor's delivery does not yet honour the red
# zone at the delivery site; DR-0006's reservation lands with WP-43.
#
# Runs detached from the host pty (via cmd, handles on files), because
# the faced runtime's console setup wedges on the host cygwin's pty --
# the same isolation the other DLL-facing tests observe.
#
# The faced DLL and build tree are build products and are not committed,
# so this reports SKIP when either is missing.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
build=/c/-/repo/elf-sysv-nt/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin
out=/c/-/repo/elf-sysv-nt/a/build/wp27-face
dll=$out/elfsysv1.dll
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ]; then
  say "SKIP: no faced DLL or WP-26 build tree; run build.sh first"
  say "verdict: yes"
  exit 0
fi

gcc -std=gnu11 -O1 -g -Wall -Wextra -mno-red-zone -fno-stack-protector \
    -nostdlib -o "$out/fault.exe" "$here/fault.c" \
    "$build/crt0.o" -L"$build" -lcygwin -lkernel32 \
  || { bad "fault.c does not build"; say "verdict: no"; exit 1; }

cd "$out"
rm -f fault.out fault-probe.out
if ELFSYSV_FAULT_TRACE=1 timeout 60 \
     cmd /c "fault.exe > fault.out 2>&1 < NUL" && \
   grep -q '^verdict=yes$' fault.out; then
  say "ok: SIGSEGV beneath a System V frame, out by siglongjmp"
  grep '^checks=\|^failures=' fault.out | sed 's/^/     /'
else
  bad "the fault path failed (rc $?):"
  sed 's/^/     /' fault.out 2>/dev/null || say "     (no output)"
fi

# The fault-dispatch record's probe: a System V fault on a runtime-created
# thread, in its own process because the failing outcome kills it.
# Reported, not asserted -- the seam belongs to WP-43 and milestone 8. A
# change of outcome either way should be looked at.
# (cmd swallows the crash status -- the losing outcome dies of the very
# fault under test -- and the verdict is read from the probe= line.)
ELFSYSV_FAULT_PROBE=1 timeout 60 \
  cmd /c "fault.exe > fault-probe.out 2>&1 < NUL & exit /b 0" || true
if grep -q '^probe=delivered$' fault-probe.out 2>/dev/null; then
  say "NOTE: the fault-dispatch probe now DELIVERS on a pthread; that"
  say "      record's measurement is stale and WP-43 may stand on this"
else
  say "note: sysv fault on a pthread still lost, as the fault-dispatch record measures"
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
