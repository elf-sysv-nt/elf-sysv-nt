#!/usr/bin/env bash
# WP-27 milestone 6: DllMain and the PE TLS callback fired by the host's own
# loader.
#
# Builds hostload.c as a plain PE process and a control DLL whose DllMain
# returns FALSE, then runs the process against the faced DLL. The process
# proves DllMain's firing through LoadLibrary's verdict (with the control
# showing the verdict can go the other way) and reads the TLS callback's
# firings out of tlsdir.c's observation seam. Also checks the image itself:
# the faced DLL must carry a nonzero PE TLS data directory, or the loader
# had nothing to walk.
#
# The faced DLL is a build product and is not committed, so this reports
# SKIP when no build exists, as t/cores.sh and t/crossing.sh do.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
dll=/c/-/repo/elf-sysv-nt/a/build/wp27-face/elfsysv1.dll
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cflags="-std=gnu11 -O1 -g -Wall -Wextra"

# Built native (mingw), not against cygwin1.dll: hostload loads the faced DLL as
# the sole Cygwin runtime of its process, the shape the product ships. As a
# Cygwin program it held a second runtime, and the faced DLL's init then ran the
# fork-style cygheap copy against the first one and logged
# "child_copy: cygheap read copy failed ... Win32 error 299". hostload.c uses no
# Cygwin service, so native is a clean move. (sole-runtime-crossing proposal.)
cc=x86_64-w64-mingw32-gcc
if ! command -v "$cc" >/dev/null 2>&1; then
  say "SKIP: no $cc; the host-loader test not built"
  say "verdict: yes"
  exit 0
fi

# the control DLL: a DllMain that declines
cat > "$tmp/control.c" <<'EOF'
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
	(void)h; (void)reason; (void)reserved;
	return FALSE;
}
EOF
"$cc" $cflags -shared -o "$tmp/control.dll" "$tmp/control.c" \
  || { bad "control DLL does not build"; }

"$cc" $cflags -o "$tmp/hostload.exe" "$here/hostload.c" \
  || { bad "hostload.c does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

if [ ! -f "$dll" ]; then
  say "SKIP: no faced DLL at $dll; run build.sh first"
  say "verdict: yes"
  exit 0
fi

# the image carries a TLS directory
tlsdir=$(objdump -p "$dll" | awk '/Entry 9/ {print $3}')
if [ -n "$tlsdir" ] && [ "$tlsdir" != "00000000" ]; then
  say "ok: the faced DLL carries a PE TLS data directory (rva $tlsdir)"
else
  bad "the faced DLL has no PE TLS data directory"
fi

# the loader fires both. Launched through cmd per the standing practice, with
# the observation variable cleared in the environment cmd inherits, so the
# process comes up with a parent that is not itself Cygwin.
if env -u ELFSYSV_TLS_OBSERVED cmd /c "$(cygpath -w "$tmp/hostload.exe")" \
     "$(cygpath -w "$dll")" "$(cygpath -w "$tmp/control.dll")" \
     > "$tmp/hostload.out" 2>&1 && grep -q '0 failures' "$tmp/hostload.out"; then
  say "ok: DllMain and the TLS callback fire from the host's loader"
  tail -1 "$tmp/hostload.out" | sed 's/^/     /'
else
  bad "hostload failed:"
  sed 's/^/     /' "$tmp/hostload.out"
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
