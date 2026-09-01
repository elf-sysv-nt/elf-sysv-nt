#!/usr/bin/env bash
# WP-56's fifteenth live crossing, crossed by its bind alone: the bind loop
# (wire.c) resolves the memory slice's real table against a real elfsysv1.dll,
# and no memory body is called.
#
# The parent is WP-41's own front end, elfsysv-exec, and the stub, native over
# t/shim's Win32 mman -- the same sole-runtime shape runtime/face/t/elfcall.sh
# certifies WP-27's last done-when clause with, unchanged from live-math.sh
# through live-filesystem.sh.
#
# The specimen (live-memory.c) is freestanding and cross-built exactly as
# elfcall.c: it walks the auxv to AT_BASE, hands a PE-export resolver of
# wire.h's shape to __esn_wire_bind over the real wire-memory.gen.c table, then
# reads the table the bind filled and the DLL's own PE header -- never a libc
# datum, and never a generated thunk.
#
# memory is 21 rows, all forwards and no shim: the allocator and the mman calls.
# It is the first slice to cross with an empty unresolved set. Every memory
# export glibc names, Cygwin exports under the same name, including the whole
# malloc introspection family. glibc's mmap64 is the one bare name Cygwin does
# not export -- being LP64 it has no separate *64 symbol -- but the mmap64 row
# already carries export_name "mmap", a forward-alias, so the row binds and only
# the unused bare name is missing. So the slice needs no shim, and the finding
# is the absence of one: contrast filesystem, whose stat family left eleven rows
# for a shim to synthesise.
#
# Crossed by its bind alone, not by call: no memory row is stateless. malloc and
# free stand on the allocator's arena and locks, the mman calls are syscalls; a
# freestanding harness brings none of that up. See live-memory.c's header.
#
# Five checks, one bit each, so 31 is the only pass:
#   0x01 the bind left no row unresolved -- the finding as a check, no shim
#   0x02 every filled slot lands inside the DLL's mapped image span
#   0x04 the resolver discriminates and the mmap64 alias holds: malloc resolves,
#        the bare name mmap64 and a junk name do not, yet the mmap64 row binds
#        through its "mmap" export_name
#   0x08 malloc, free and mmap reach three distinct bodies
#   0x10 a second bind is idempotent (no row null, every slot a fresh resolve)
#
# Controls, the same shape as live-filesystem.sh's: a runtime that does not
# exist must refuse before entry, and a run with no runtime at all must come
# back with none of the bits set, proving the bits are the runtime's and not
# accidents.
#
# The faced DLL is a build product and is not committed, so this reports SKIP
# when no build exists, as elfcall.sh does.
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

# The specimen: the bind loop, the real memory table and its generated thunks
# (linked for parity though none is called), and this file's resolver and
# checks -- cross-built exactly as elfcall.
$cross-gcc -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
  -O2 -Wall -Wextra -Wl,-z,max-page-size=0x10000 \
  -o "$tmp/live-memory" \
  "$here/live-memory-start.S" "$here/live-memory.c" \
  "$wiring_dir/wire.c" "$wiring_dir/wire-memory.gen.c" \
  "$wiring_dir/wire-memory.gen.s" \
  || { bad "the specimen does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

export ELFSYSV_STUB=$tmp/elfsysv-stub.exe
specimen=$(cygpath -w "$tmp/live-memory")

# The certification: through the branch, against the real DLL. The runtime
# rides to the stub on its own command line through the front end's
# stub-options seam.
ELFSYSV_STUB_OPTIONS="--runtime=$(cygpath -w "$dll")"
export ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/live.out" 2>&1 || got=$?
if [ "$got" = 31 ]; then
  say "ok: the bind left no row unresolved (memory needs no shim, the whole slice forwards), every filled slot lands inside the DLL's mapped image, the resolver discriminates and the mmap64 alias holds (malloc resolves, the bare name mmap64 does not, yet the mmap64 row binds through its \"mmap\" export_name), malloc/free/mmap reach distinct bodies, and a second bind is idempotent -- the bind-only crossing holds and no memory body is entered, since the slice offers a freestanding harness none it may safely call (status 31)"
else
  bad "the live-memory specimen returned $got, wanted 31:"
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
