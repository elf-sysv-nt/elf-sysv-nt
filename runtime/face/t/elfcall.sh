#!/usr/bin/env bash
# WP-27's last done-when clause: a static ELF through WP-41's branch calls a
# real export and returns.
#
# The parent is WP-41's own front end, elfsysv-exec, built from the certified
# sources unchanged: it classifies the file, starts the stub suspended, and
# reserves the low window into it. The stub is stub.c built NATIVE (mingw),
# so the faced DLL it loads is its process's sole Cygwin runtime, per the
# sole-runtime-crossing proposal; the POSIX memory calls beneath the certified
# mapper come from t/shim. The stub loads the runtime its --runtime option
# names and hands its base to the image through AT_BASE; the specimen walks the
# auxv to the base, resolves real exports out of the PE export directory, and
# calls them System V, straight at the export -- the NOSIGFE leaves
# t/crossing.c chose, through the generic int face and the typed fp thunks.
# Seven checks, one bit each, so 127 is the only pass.
#
# Controls: a runtime that does not exist must refuse before entry, and the
# specimen run with no runtime at all must come back missing exactly the
# runtime bits, proving the seven bits are the runtime's and not accidents.
#
# The faced DLL is a build product and is not committed, so this reports SKIP
# when no build exists, as t/cores.sh and t/crossing.sh do.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
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
  say "SKIP: no faced DLL at $dll; run build.sh first"
  say "verdict: yes"
  exit 0
fi

loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c $loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

# WP-41's front end, the branch itself, from the certified sources unchanged.
gcc $cflags -o "$tmp/elfsysv-exec" "$exec_dir/exec_main.c" \
  "$exec_dir/dispatch.c" "$exec_dir/binfmt.c" "$exec_dir/reserve.c" \
  || { bad "the front end does not build"; }

# The stub, native, over the shim. The stack reserve is DR-0028's, not a
# tuning choice: the default puts the initial stack where the ELF world goes.
$native $cflags -I"$here/shim" -Wl,--stack,0x100000 -o "$tmp/elfsysv-stub.exe" \
  "$exec_dir/stub.c" "$exec_dir/enter.S" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" "$exec_dir/dyn_init.c" "$here/shim/mman.c" $loader_srcs \
  || { bad "the native stub does not build"; }

# The specimen, cross-built exactly as WP-41's own.
$cross-gcc -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
  -O2 -Wall -Wextra -Wl,-z,max-page-size=0x10000 \
  -o "$tmp/elfcall" "$here/elfcall-start.S" "$here/elfcall.c" \
  || { bad "the specimen does not build"; }

if [ "$fail" != 0 ]; then say "verdict: no"; exit 1; fi

export ELFSYSV_STUB=$tmp/elfsysv-stub.exe

# The specimen's path crosses into a native process, so it goes in as the
# Windows spelling, which the Cygwin front end reads just as well.
specimen=$(cygpath -w "$tmp/elfcall")

# The certification: through the branch, against the real runtime. The
# runtime rides to the stub on its own command line through the front end's
# stub-options seam.
ELFSYSV_STUB_OPTIONS="--runtime=$(cygpath -w "$dll")"
export ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/call.out" 2>&1 || got=$?
if [ "$got" = 127 ]; then
  say "ok: a static ELF through the branch called real exports and returned (status 127)"
else
  bad "the elfcall specimen returned $got, wanted 127:"
  sed 's/^/     /' "$tmp/call.out"
fi

# Control: a runtime that does not exist refuses before entry.
export ELFSYSV_STUB_OPTIONS='--runtime=C:\no-such-elfsysv-runtime.dll'
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/refuse.out" 2>&1 || got=$?
if [ "$got" != 127 ] && [ "$got" != 0 ] &&
   grep -q "cannot load the runtime" "$tmp/refuse.out"; then
  say "ok: a missing runtime is refused before entry (status $got)"
else
  bad "a missing runtime was not refused by name (status $got):"
  sed 's/^/     /' "$tmp/refuse.out"
fi

# Control: no runtime at all. AT_BASE reads 0, so exactly the runtime bits go
# missing and the specimen still returns -- the bits are the runtime's.
unset ELFSYSV_STUB_OPTIONS
got=0
"$tmp/elfsysv-exec" "$specimen" > "$tmp/none.out" 2>&1 || got=$?
if [ "$got" = 0 ]; then
  say "ok: with no runtime the specimen returns with no runtime bits (status 0)"
else
  bad "with no runtime the specimen returned $got, wanted 0"
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
