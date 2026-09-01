#!/usr/bin/env bash
# WP-56's twenty-third live crossing, crossed by its bind alone: the bind
# loop (wire.c) resolves the io slice's real table against a real
# elfsysv1.dll, and no I/O body is called.
#
# The parent is WP-41's own front end, elfsysv-exec, and the stub, native over
# t/shim's Win32 mman -- the same sole-runtime shape runtime/face/t/elfcall.sh
# certifies WP-27's last done-when clause with, unchanged from live-math.sh
# through live-sysv-ipc.sh.
#
# The specimen (live-io.c) is freestanding and cross-built exactly as
# elfcall.c: it walks the auxv to AT_BASE, hands a PE-export resolver of
# wire.h's shape to __esn_wire_bind over the real wire-io.gen.c table, then
# reads the table the bind filled and the DLL's own PE header -- never a libc
# datum, and never a generated thunk.
#
# io is 2 rows, both forwards and no shim: libc's scatter-gather I/O
# primitives readv and writev, each a forward-same row at GLIBC_2.2.5 with no
# rename and no compat pair. It crosses with an empty unresolved set, the
# shape memory's, io-mux's, threads', regex's, syslog's and sysv-ipc's
# crossings had.
#
# The one thing new is how narrow the wired table is against the slice: the
# demand ranking placed thirty-one names in this slice, the table wires two.
# The sixteen aio_*/lio_listio names glibc ships in librt.so.1, not libc, so
# they never enter libc's forward map; the thirteen preadv/pwritev/process_vm
# /sendfile/aio_init names the classification marks stubs. Neither is a shim
# shortfall -- real-map.sh's partition owns the residue, and the wired two are
# exactly libc's vectored read and write. The finding is those two binding
# whole.
#
# Crossed by its bind alone, not by call: neither io row is safely callable
# here. readv and writev move bytes across file descriptors through Cygwin's
# subsystem, which is SIGFE and reaches the fhandler layer; a freestanding
# harness never brought it up. See live-io.c's header. The bodies are left to
# diff-slice.sh's readv-writev.c over a pipe.
#
# Five checks, one bit each, so 31 is the only pass:
#   0x01 the bind left no row unresolved -- the finding as a check, no shim
#   0x02 every filled slot lands inside the DLL's mapped image span
#   0x04 the resolver discriminates and the plain-forward finding holds: readv
#        resolves, a junk name does not, and the single readv row bound to
#        exactly that one export
#   0x08 readv and writev reach two distinct bodies
#   0x10 a second bind is idempotent (no row null, every slot a fresh resolve)
#
# Controls, the same shape as live-sysv-ipc.sh's: a runtime that does not
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
$loader/elf/elf_parse.c $loader/process/process_image.c $loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

# WP-41's front end, the branch itself, from the certified sources unchanged.
gcc $cflags -o "$tmp/elfsysv-exec" "$exec_dir/exec_main.c" \
  "$exec_dir/dispatch.c" "$exec_dir/binfmt.c" "$exec_dir/reserve.c" \
  || { bad "the front end does not build"; }

# The stub, native, over the shim -- unchanged from elfcall.sh.
$native $cflags -I"$face_t/shim" -Wl,--stack,0x100000 \
  -o "$tmp/elfsysv-stub.exe" \
  "$exec_dir/stub.c" "$exec_dir/enter.S" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" "$exec_dir/dyn_init.c" "$face_t/shim/mman.c" $loader_srcs \
  || { bad "the native stub does not build"; }

# The specimen: the bind loop, the real io table and its generated thunks
# (linked for parity though none is called), and this file's resolver and
# checks -- cross-built exactly as elfcall.
$cross-gcc -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
  -O2 -Wall -Wextra -Wl,-z,max-page-size=0x10000 \
  -o "$tmp/live-io" \
  "$here/live-io-start.S" "$here/live-io.c" \
  "$wiring_dir/wire.c" "$wiring_dir/wire-io.gen.c" \
  "$wiring_dir/wire-io.gen.s" \
  || { bad "the specimen does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

export ELFSYSV_STUB=$tmp/elfsysv-stub.exe
specimen=$(cygpath -w "$tmp/live-io")

# The certification: through the branch, against the real DLL. The runtime
# rides to the stub on its own command line through the front end's
# stub-options seam.
ELFSYSV_STUB_OPTIONS="--runtime=$(cygpath -w "$dll")"
export ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/live.out" 2>&1 || got=$?
if [ "$got" = 31 ]; then
  say "ok: the bind left no row unresolved (the two wired I/O primitives both bind, the slice needs no shim), every filled slot lands inside the DLL's mapped image, the resolver discriminates and the plain-forward finding holds (readv resolves, a junk name does not, and the single readv row bound to exactly that one export), readv/writev reach distinct bodies, and a second bind is idempotent -- the bind-only crossing holds and no I/O body is entered, since the slice offers a freestanding harness none it may safely call (status 31)"
else
  bad "the live-io specimen returned $got, wanted 31:"
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
