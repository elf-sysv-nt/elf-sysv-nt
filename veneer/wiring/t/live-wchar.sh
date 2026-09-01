#!/usr/bin/env bash
# WP-56's eleventh live crossing, and the first whose finding is negative: the
# bind loop (wire.c) resolves the wchar slice's real table against a real
# elfsysv1.dll, the slice's wide movers -- wmemcpy, wmemmove, wmempcpy --
# reach the real body, and they move a two-byte wchar_t, not the four-byte one
# the slice's forward premise assumed. The wchar rows do not cross as
# value-preserving forwards the way string's and misc's did.
#
# The parent is WP-41's own front end, elfsysv-exec, and the stub, native
# over t/shim's Win32 mman -- the same sole-runtime shape
# runtime/face/t/elfcall.sh certifies WP-27's last done-when clause with,
# unchanged from live-math.sh through live-misc.sh.
#
# The specimen (live-wchar.c) is freestanding and cross-built exactly as
# elfcall.c: it walks the auxv to AT_BASE, hands a PE-export resolver of
# wire.h's shape to __esn_wire_bind over the real wire-wchar.gen.c table,
# then calls three of the generated thunks (wire-wchar.gen.s: w00082 wmemcpy,
# w00083 wmemmove, w00084 wmempcpy) directly and reads the width the body
# moved by.
#
# wchar is 87 rows, all forwards, no shim, so its bind check is math's,
# stdlib's, posix's, time's and misc's shape: every row must resolve, missing
# == 0. That holds -- all 87 wide names are exported. What the earlier
# crossings then confirmed, and this one refutes, is that a resolved wchar row
# crosses as a plain tail jump. The el8 face presents a four-byte wchar_t, the
# System V width; Cygwin's newlib body inside elfsysv1.dll uses two bytes, the
# Windows width. veneer/wiring/README.md recorded the wide surface as
# "value-preserving end to end: wchar_t is 4 bytes on both sides", but that
# was measured face-against-glibc, both el8; face-against-the-real-DLL,
# wmemcpy(dst, src, 5) moves ten bytes, not twenty. The decision this branch
# adds records the consequence: every wchar_t-bearing row needs a
# width-translating shim, not a forward.
#
# Five checks, one bit each, so 31 is the only pass -- a pass meaning the
# negative finding holds and reproduces. See live-wchar.c's header for the
# full account.
#
# Controls, the same shape as live-misc.sh's: a runtime that does not exist
# must refuse before entry, and a run with no runtime at all must come back
# with none of the bits set, proving the bits are the runtime's and not
# accidents.
#
# The faced DLL is a build product and is not committed, so this reports
# SKIP when no build exists, as elfcall.sh does.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
wiring_dir=$here/..
face_t=$here/../../../runtime/face/t
exec_dir=$here/../../../loader/exec
loader=$here/../../../loader
dll=/c/-/repo/elf-sysv-nt/a/build/wp27-face/elfsysv1.dll
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

export PATH="$HOME/x-elfsysvnt/bin:$PATH"
cross=x86_64-elfsysvnt-linux-gnu
native=x86_64-w64-mingw32-gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"

if ! command -v "$native" >/dev/null 2>&1; then
  say "SKIP: no $native; the sole-runtime stub not built"
  say "verdict: yes"
  exit 0
fi
if ! command -v "$cross-gcc" >/dev/null 2>&1; then
  say "SKIP: no $cross-gcc; the specimen not built"
  say "verdict: yes"
  exit 0
fi
if [ ! -f "$dll" ]; then
  say "SKIP: no faced DLL at $dll; run runtime/face/build.sh first"
  say "verdict: yes"
  exit 0
fi

loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c"

# WP-41's front end, the branch itself, from the certified sources unchanged.
gcc $cflags -o "$tmp/elfsysv-exec" "$exec_dir/exec_main.c" \
  "$exec_dir/dispatch.c" "$exec_dir/binfmt.c" "$exec_dir/reserve.c" \
  || { bad "the front end does not build"; }

# The stub, native, over the shim -- unchanged from elfcall.sh.
$native $cflags -I"$face_t/shim" -Wl,--stack,0x100000 \
  -o "$tmp/elfsysv-stub.exe" \
  "$exec_dir/stub.c" "$exec_dir/enter.S" "$face_t/shim/mman.c" $loader_srcs \
  || { bad "the native stub does not build"; }

# The specimen: the bind loop, the real wchar table, its generated thunks,
# and this file's resolver and checks -- cross-built exactly as elfcall.
$cross-gcc -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
  -O2 -Wall -Wextra -Wl,-z,max-page-size=0x10000 \
  -o "$tmp/live-wchar" \
  "$here/live-wchar-start.S" "$here/live-wchar.c" \
  "$wiring_dir/wire.c" "$wiring_dir/wire-wchar.gen.c" \
  "$wiring_dir/wire-wchar.gen.s" \
  || { bad "the specimen does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

export ELFSYSV_STUB=$tmp/elfsysv-stub.exe
specimen=$(cygpath -w "$tmp/live-wchar")

# The certification: through the branch, against the real DLL. The runtime
# rides to the stub on its own command line through the front end's
# stub-options seam.
ELFSYSV_STUB_OPTIONS="--runtime=$(cygpath -w "$dll")"
export ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/live.out" 2>&1 || got=$?
if [ "$got" = 31 ]; then
  say "ok: the bind loop resolved the real wchar table (all 87 wide names exported) and wmemcpy/wmemmove/wmempcpy reached the real body -- which moved a TWO-byte wchar_t (ten bytes for five elements, not twenty), refuting the slice's four-byte forward premise; the wchar rows need a width shim, not a tail jump (status 31)"
else
  bad "the live-wchar specimen returned $got, wanted 31:"
  sed 's/^/     /' "$tmp/live.out"
fi

# Control: a runtime that does not exist refuses before entry.
export ELFSYSV_STUB_OPTIONS='--runtime=C:\no-such-elfsysv-runtime.dll'
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/refuse.out" 2>&1 || got=$?
if [ "$got" != 31 ] && [ "$got" != 0 ] &&
   grep -q "cannot load the runtime" "$tmp/refuse.out"; then
  say "ok: a missing runtime is refused before entry (status $got)"
else
  bad "a missing runtime was not refused by name (status $got):"
  sed 's/^/     /' "$tmp/refuse.out"
fi

# Control: no runtime at all. AT_BASE reads 0, so the bind never runs and
# every bit stays clear -- the bits are the runtime's, not accidents.
unset ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/none.out" 2>&1 || got=$?
if [ "$got" = 0 ]; then
  say "ok: with no runtime the specimen returns with no bits set (status 0)"
else
  bad "with no runtime the specimen returned $got, wanted 0"
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
