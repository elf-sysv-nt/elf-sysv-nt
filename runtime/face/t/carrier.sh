#!/usr/bin/env bash
# WP-27 milestone 8: thread creation establishes the DR-0021 carrier,
# against the real DLL.
#
# Builds carrier.c (the unit) and t/carrier.c (the harness) as a real
# process of the faced runtime -- the fault.sh shape: -nostdlib against
# the WP-26 build tree's crt0 and import library, run detached from the
# host pty via cmd.  The CW code numbers are derived from the vendor
# header here, at build time, so the test cannot drift from the enum.
#
# The faced DLL and build tree are build products and are not committed,
# so this reports SKIP when either is missing.  The DLL must carry the
# CW_ELFSYSV_CARRIER commit; an older DLL fails the reservation check.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
src=/c/-/repo/newlib-cygwin
build=/c/-/repo/elf-sysv-nt/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin
out=/c/-/repo/elf-sysv-nt/a/build/wp27-face
dll=$out/elfsysv1.dll
hdr=$src/winsup/cygwin/include/sys/cygwin.h
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ]; then
  say "SKIP: no faced DLL or WP-26 build tree; run build.sh first"
  say "verdict: yes"
  exit 0
fi

# The CW code numbers, from the enum's own order.
cw_index() {
  sed -n '/typedef enum/,/cygwin_getinfo_types;/p' "$hdr" \
    | grep -o 'CW_[A-Z_0-9]*' | awk -v n="$1" '$0==n{print NR-1; exit}'
}
cw_pad=$(cw_index CW_CYGTLS_PADSIZE)
cw_car=$(cw_index CW_ELFSYSV_CARRIER)
[ -n "$cw_pad" ] && [ -n "$cw_car" ] \
  || { bad "cannot derive the CW codes from $hdr"; say "verdict: no"; exit 1; }
say "derived: CW_CYGTLS_PADSIZE=$cw_pad CW_ELFSYSV_CARRIER=$cw_car"

gcc -std=gnu11 -O1 -g -Wall -Wextra -mno-red-zone -fno-stack-protector \
    -DELFSYSV_CW_CYGTLS_PADSIZE=$cw_pad \
    -DELFSYSV_CW_ELFSYSV_CARRIER=$cw_car \
    -nostdlib -o "$out/carrier.exe" "$here/carrier.c" "$here/../carrier.c" \
    "$build/crt0.o" -L"$build" -lcygwin -lkernel32 \
  || { bad "the carrier harness does not build"; say "verdict: no"; exit 1; }

cd "$out"
rm -f carrier.out
if timeout 60 cmd /c "carrier.exe > carrier.out 2>&1 < NUL" && \
   grep -q '^verdict=yes$' carrier.out; then
  say "ok: thread creation establishes the carrier, per thread, one offset"
  grep '^checks=\|^failures=' carrier.out | sed 's/^/     /'
else
  bad "the carrier certification failed (rc $?):"
  sed 's/^/     /' carrier.out 2>/dev/null || say "     (no output)"
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
