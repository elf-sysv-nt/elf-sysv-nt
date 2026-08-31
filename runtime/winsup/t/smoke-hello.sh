#!/bin/bash
# WP-26 smoke: a hello built against the freshly built elfsysv1.dll runs.
#
# The parent matters. A Cygwin parent (bash on cygwin1.dll) passes its
# child-info handshake to the spawned process; the re-badged DLL reads it,
# tries to copy the parent's cygheap, and dies ("couldn't create signal
# pipe"). Launched from cmd.exe the DLL initializes as its own island and
# hello prints. Separating the handshake is re-face work, not WP-26's.
set -euo pipefail

repo=/c/-/repo/elf-sysv-nt
cygdir=$repo/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

printf '#include <stdio.h>\nint main(void){puts("hello from elfsysv1");return 0;}\n' > "$d/hello.c"
gcc "$d/hello.c" -o "$d/hello.exe" -L"$cygdir"
objdump -p "$d/hello.exe" | grep -q 'DLL Name: elfsysv1.dll' || {
  echo "FAIL: hello does not import elfsysv1.dll" >&2; exit 1; }
cp "$cygdir/new-cygwin1.dll" "$d/elfsysv1.dll"

win=$(cygpath -w "$d")
out=$("$(cygpath -W)/System32/cmd.exe" /c "cd $win && hello.exe" | tr -d '\r')
[ "$out" = "hello from elfsysv1" ] || {
  echo "FAIL: got '$out'" >&2; exit 1; }
echo "PASS: hello runs against elfsysv1.dll"
