#!/usr/bin/env bash
# WP-56's eighth live crossing: the bind loop (wire.c) resolves the posix
# slice's real table against a real elfsysv1.dll, and one of the slice's
# generated thunks -- swab -- is called for real on NT.
#
# The parent is WP-41's own front end, elfsysv-exec, and the stub, native
# over t/shim's Win32 mman -- the same sole-runtime shape
# runtime/face/t/elfcall.sh certifies WP-27's last done-when clause with,
# unchanged from live-math.sh, live-string.sh, live-stdlib.sh,
# live-sockets.sh and live-locale.sh.
#
# The specimen (live-posix.c) is freestanding and cross-built exactly as
# elfcall.c: it walks the auxv to AT_BASE, hands a PE-export resolver of
# wire.h's shape to __esn_wire_bind over the real wire-posix.gen.c table,
# then calls one of the generated thunks (wire-posix.gen.s: w00089 swab)
# directly.
#
# posix is 108 rows, all forwards, no shim -- the largest all-forward table
# bound live so far, past sockets's 66 -- so its bind check is math's,
# stdlib's, sockets's and locale's shape: every row must resolve, missing ==
# 0, not string's exactly-one-null. What it adds is a result shape the
# earlier crossings never reached: swab returns void and writes its whole
# result through the caller's destination pointer, where ffs, abs, htonl and
# toascii each returned a scalar in a register. swab is also the slice's one
# row that stands alone -- a byte permutation between two caller buffers,
# NOSIGFE and argument-only -- where fork, close, the exec family and even
# posix's own getpid and getuid read the process or the thread pointer this
# harness never establishes. See live-posix.c's header for the full account.
#
# Five checks, one bit each, so 31 is the only pass.
#
# Controls, the same shape as live-locale.sh's: a runtime that does not
# exist must refuse before entry, and a run with no runtime at all must
# come back with none of the bits set, proving they are the runtime's and
# not accidents.
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

# The specimen: the bind loop, the real posix table, its generated thunk,
# and this file's resolver and checks -- cross-built exactly as elfcall.
$cross-gcc -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
  -O2 -Wall -Wextra -Wl,-z,max-page-size=0x10000 \
  -o "$tmp/live-posix" \
  "$here/live-posix-start.S" "$here/live-posix.c" \
  "$wiring_dir/wire.c" "$wiring_dir/wire-posix.gen.c" \
  "$wiring_dir/wire-posix.gen.s" \
  || { bad "the specimen does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

export ELFSYSV_STUB=$tmp/elfsysv-stub.exe
specimen=$(cygpath -w "$tmp/live-posix")

# The certification: through the branch, against the real DLL. The runtime
# rides to the stub on its own command line through the front end's
# stub-options seam.
ELFSYSV_STUB_OPTIONS="--runtime=$(cygpath -w "$dll")"
export ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/live.out" 2>&1 || got=$?
if [ "$got" = 31 ]; then
  say "ok: the bind loop resolved the real posix table (every forward filled, the largest all-forward table bound live so far) and swab -- the slice's one argument-only, pointer-out row -- reached the real DLL (status 31)"
else
  bad "the live-posix specimen returned $got, wanted 31:"
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
