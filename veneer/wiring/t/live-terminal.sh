#!/usr/bin/env bash
# WP-56's twelfth live crossing, and the second whose finding is negative: the
# bind loop (wire.c) resolves the terminal slice's real table against a real
# elfsysv1.dll, the slice's three NOSIGFE forwards -- cfgetispeed, cfgetospeed,
# cfmakeraw -- reach the real body, and the two that read a struct termios read
# it at the body's offsets, not the face's. The el8 face's NCCS is 32 and the
# body's is 18, so c_ispeed/c_ospeed sit at 52/56 in the face but 36/40 in the
# body: the cf-speed rows do not cross as value-preserving forwards the way
# string's and misc's did.
#
# The parent is WP-41's own front end, elfsysv-exec, and the stub, native over
# t/shim's Win32 mman -- the same sole-runtime shape runtime/face/t/elfcall.sh
# certifies WP-27's last done-when clause with, unchanged from live-math.sh
# through live-wchar.sh.
#
# The specimen (live-terminal.c) is freestanding and cross-built exactly as
# elfcall.c: it walks the auxv to AT_BASE, hands a PE-export resolver of
# wire.h's shape to __esn_wire_bind over the real wire-terminal.gen.c table,
# then calls three of the generated thunks (wire-terminal.gen.s: w00000
# cfgetispeed, w00001 cfgetospeed, w00002 cfmakeraw) directly and reads which
# struct-termios offsets the body used.
#
# The terminal table is 30 rows, one shim (ioctl), the rest forwards, so its
# bind check is the all-resolve shape: every row must resolve, missing == 0.
# That holds -- every terminal name, ioctl included, is exported. What the
# earlier crossings then confirmed, and this one refutes for the speed rows, is
# that a resolved terminal row crosses as a plain tail jump. cfgetispeed and
# cfgetospeed are plain field reads in the body (return t->c_ispeed, measured
# on Cygwin), so on a face-laid struct they read the body's offset 36/40, not
# the face's 52/56. cfmakeraw touches only the leading flag words the two
# layouts share, so it crosses value-preserving there -- the crossing pins both
# halves. The decision this branch adds records the consequence: every
# struct-termios-bearing row needs a layout-translating shim, not a forward.
#
# Five checks, one bit each, so 31 is the only pass -- a pass meaning the
# negative finding holds and reproduces. See live-terminal.c's header for the
# full account.
#
# Controls, the same shape as live-wchar.sh's: a runtime that does not exist
# must refuse before entry, and a run with no runtime at all must come back
# with none of the bits set, proving the bits are the runtime's and not
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

# The specimen: the bind loop, the real terminal table, its generated thunks,
# and this file's resolver and checks -- cross-built exactly as elfcall.
$cross-gcc -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
  -O2 -Wall -Wextra -Wl,-z,max-page-size=0x10000 \
  -o "$tmp/live-terminal" \
  "$here/live-terminal-start.S" "$here/live-terminal.c" \
  "$wiring_dir/wire.c" "$wiring_dir/wire-terminal.gen.c" \
  "$wiring_dir/wire-terminal.gen.s" \
  || { bad "the specimen does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

export ELFSYSV_STUB=$tmp/elfsysv-stub.exe
specimen=$(cygpath -w "$tmp/live-terminal")

# The certification: through the branch, against the real DLL. The runtime
# rides to the stub on its own command line through the front end's
# stub-options seam.
ELFSYSV_STUB_OPTIONS="--runtime=$(cygpath -w "$dll")"
export ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/live.out" 2>&1 || got=$?
if [ "$got" = 31 ]; then
  say "ok: the bind loop resolved the real terminal table (every name exported, ioctl included) and cfgetispeed/cfgetospeed reached the real body -- which read the body's c_ispeed/c_ospeed at offsets 36/40, not the face's 52/56 (NCCS 18 vs 32), while cfmakeraw crossed the shared leading flag words value-preserving; the struct-termios rows need a layout shim, not a tail jump (status 31)"
else
  bad "the live-terminal specimen returned $got, wanted 31:"
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
