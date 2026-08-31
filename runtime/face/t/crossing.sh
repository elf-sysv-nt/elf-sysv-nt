#!/usr/bin/env bash
# WP-27: rerun the crossing certifications against the real DLL.
#
# Three parts. WP-22's certification (runtime/core/t/run.sh) and WP-23's
# (runtime/core/t/callback-run.sh) rerun unchanged, which is the Done-when's
# own words: the instruments that certified the crossing at stand-in width
# still pass on this host. Then crossing.c points the same instruments at
# the faced DLL itself: it loads a/build/wp27-face/elfsysv1.dll and calls
# real exports System V, checking values and the callee-saved set, with
# probe.S's leaky control proving the register check can fail.
#
# The faced DLL is a build product and is not committed, so the DLL half
# reports SKIP when no build exists, as t/cores.sh does.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
core_t=$here/../../core/t
dll=/c/-/repo/elf-sysv-nt/a/build/wp27-face/elfsysv1.dll
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. WP-22's certification, unchanged
if bash "$core_t/run.sh" -q > "$tmp/wp22.out" 2>&1; then
  say "ok: WP-22's certification reruns and passes"
else
  bad "WP-22's certification no longer passes: $(tail -1 "$tmp/wp22.out")"
fi

# 2. WP-23's certification, unchanged
if bash "$core_t/callback-run.sh" -q > "$tmp/wp23.out" 2>&1; then
  say "ok: WP-23's certification reruns and passes"
else
  bad "WP-23's certification no longer passes: $(tail -1 "$tmp/wp23.out")"
fi

# 3. the same instruments against the faced DLL, exercised as a process's SOLE
# Cygwin runtime. The harness is built native (mingw), not against cygwin1.dll,
# so loading elfsysv1.dll adds no second runtime to the process -- the shape the
# product ships and the one Cygwin actually supports. Launched through cmd per
# the standing practice, so the process's parent is not itself Cygwin. The old
# harness was a Cygwin program that LoadLibrary'd the faced DLL beside its own
# cygwin1.dll, two runtimes in one process, which passed on a quiet machine but
# flaked under load. (sole-runtime-crossing proposal.)
cc=x86_64-w64-mingw32-gcc
cflags="-std=gnu11 -O1 -g -mno-red-zone -Wall -Wextra"
if ! command -v "$cc" >/dev/null 2>&1; then
  say "SKIP: no $cc; the sole-runtime crossing half not built"
elif ! "$cc" $cflags -o "$tmp/crossing.exe" \
     "$here/crossing.c" "$core_t/probe.S" 2> "$tmp/cc.err"; then
  bad "crossing.c does not build native: $(head -1 "$tmp/cc.err")"
elif [ ! -f "$dll" ]; then
  say "SKIP: no faced DLL at $dll; the against-the-DLL half not run"
elif cmd /c "$(cygpath -w "$tmp/crossing.exe")" "$(cygpath -w "$dll")" \
       > "$tmp/cross.out" 2>&1 && grep -q '^verdict=yes' "$tmp/cross.out"; then
  say "ok: the crossing holds against the real DLL as its sole runtime"
  grep -E '^(checks|failures)=' "$tmp/cross.out" | sed 's/^/     /'
else
  bad "the crossing against the real DLL failed:"
  sed 's/^/     /' "$tmp/cross.out"
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
