#!/usr/bin/env bash
# reent-bringup 1.0 -- which host shape makes a reent-consuming body work.
#
# Builds three probes against the faced elfsysv1.dll and prints reproducible
# findings as `key=word`. The cygload probes run as a foreign PE; the
# real-process probe is a real process of the faced runtime (WP-26 crt0 +
# -lcygwin), run detached via cmd because the faced runtime's console wedges
# on a host pty. Provenance is a header block, apart from the findings.
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

native=x86_64-w64-mingw32-gcc
cflags="-std=gnu11 -O2 -Wall -Wextra"

emit "script  reent-bringup 1.0"
emit ""
emit "host        $(hostname)"
emit "compiler    $($native --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ]; then
	emit "skip=no faced DLL or WP-26 build tree; build.sh first"
	emit "verdict=yes"
	exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# --- cygload shape: the reent slot with no bring-up ----------------------
$native $cflags -o "$tmp/cygload.exe" "$here/cygload-probe.c" \
	|| { emit "cygload_build=failed"; emit "verdict=no"; exit 1; }
timeout 40 "$tmp/cygload.exe" "$(cygpath -w "$dll")" 2>&1 \
	| grep -E '^[a-z].*=' | grep -v '^verdict=' | emit_lines

# --- cygload shape: the bring-up call that wedges ------------------------
$native $cflags -o "$tmp/dllinit.exe" "$here/dllinit-probe.c" \
	|| { emit "dllinit_build=failed"; emit "verdict=no"; exit 1; }
timeout 25 "$tmp/dllinit.exe" "$(cygpath -w "$dll")" >"$tmp/dllinit.out" 2>&1
rc=$?
if [ $rc -eq 124 ] && ! grep -q '^returned=' "$tmp/dllinit.out"; then
	emit "cygload_dll_init=hangs"
elif grep -q '^returned=' "$tmp/dllinit.out"; then
	emit "cygload_dll_init=returns"
else
	emit "cygload_dll_init=error"
fi

# --- real process of the faced runtime -----------------------------------
gcc -std=gnu11 -O1 -g -Wall -Wextra -mno-red-zone -fno-stack-protector \
	-nostdlib -o "$out/reent-realproc.exe" "$here/realproc-probe.c" \
	"$build/crt0.o" -L"$build" -lcygwin -lkernel32 \
	|| { emit "realproc_build=failed"; emit "verdict=no"; exit 1; }
( cd "$out" && rm -f reent-realproc.out \
	&& timeout 60 cmd /c "reent-realproc.exe > reent-realproc.out 2>&1 < NUL" )
grep -E '^realproc_' "$out/reent-realproc.out" 2>/dev/null | emit_lines

emit "verdict=yes"
