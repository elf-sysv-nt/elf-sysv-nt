#!/usr/bin/env bash
# WP-56's second live crossing: the bind loop (wire.c) resolves the
# runtime slice's real table against a real elfsysv1.dll, and two of the
# slice's generated thunks are called for real on NT.
#
# Same parent as live-math.sh: WP-41's own front end, elfsysv-exec, and the
# stub, native over t/shim's Win32 mman -- the sole-runtime shape
# runtime/face/t/elfcall.sh certifies WP-27's last done-when clause with.
# The specimen here (live-runtime.c) is freestanding and cross-built
# exactly as live-math.c: it walks the auxv to AT_BASE, hands a PE-export
# resolver of wire.h's shape to `__esn_wire_bind` over the real
# wire-runtime.gen.c table (10 rows, all NOSIGFE, so no full Cygwin
# process bring-up is needed), then calls two of the generated thunks
# (wire-runtime.gen.s) directly -- getcontext, swapcontext; see
# live-runtime.c's file comment for why the other three thunks (__assert,
# makecontext, setcontext) and the five jmp_buf shims are out of scope for
# this specimen, and for the finding writing it turned up: the real DLL's
# swapcontext does not perform the actual context switch in this
# freestanding harness, so both checks here observe a save-side effect
# only, never a resumed control transfer. Three checks, one bit each, so 7
# is the only pass.
#
# Controls, the same shape as elfcall.sh's and live-math.sh's: a runtime
# that does not exist must refuse before entry, and a run with no runtime
# at all must come back with none of the bits set, proving they are the
# runtime's and not accidents.
#
# The faced DLL is a build product and is not committed, so this reports
# SKIP when no build exists, as live-math.sh does.
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

# The stub, native, over the shim -- unchanged from live-math.sh.
$native $cflags -I"$face_t/shim" -Wl,--stack,0x100000 \
  -o "$tmp/elfsysv-stub.exe" \
  "$exec_dir/stub.c" "$exec_dir/enter.S" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" "$exec_dir/dyn_init.c" "$face_t/shim/mman.c" $loader_srcs \
  || { bad "the native stub does not build"; }

# The specimen: the bind loop, the real runtime table, its generated
# thunks, and this file's resolver and checks -- cross-built exactly as
# live-math.
$cross-gcc -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
  -O2 -Wall -Wextra -Wl,-z,max-page-size=0x10000 \
  -o "$tmp/live-runtime" \
  "$here/live-runtime-start.S" "$here/live-runtime.c" \
  "$wiring_dir/wire.c" "$wiring_dir/wire-runtime.gen.c" \
  "$wiring_dir/wire-runtime.gen.s" \
  || { bad "the specimen does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

export ELFSYSV_STUB=$tmp/elfsysv-stub.exe
specimen=$(cygpath -w "$tmp/live-runtime")

# The certification: through the branch, against the real DLL. The
# runtime rides to the stub on its own command line through the front
# end's stub-options seam.
ELFSYSV_STUB_OPTIONS="--runtime=$(cygpath -w "$dll")"
export ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/live.out" 2>&1 || got=$?
if [ "$got" = 7 ]; then
  say "ok: the bind loop resolved the real runtime table and the wired thunks reached the real DLL (status 7)"
else
  bad "the live-runtime specimen returned $got, wanted 7:"
  sed 's/^/     /' "$tmp/live.out"
fi

# Control: a runtime that does not exist refuses before entry.
export ELFSYSV_STUB_OPTIONS='--runtime=C:\no-such-elfsysv-runtime.dll'
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/refuse.out" 2>&1 || got=$?
if [ "$got" != 7 ] && [ "$got" != 0 ] &&
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
