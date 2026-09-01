#!/usr/bin/env bash
#
# reent-face-bringup live run -- item 3's terminal witness, across enter.S.
#
# dyn-cross-stub.sh (loader/exec/t) certified that the stub crosses a dynamic
# image into an ELF runtime and enters it, on a bare libgreet.so whose one
# export is a sentinel. This carries that exact machinery one rung further, to
# the reent: the runtime is the WP-53 libc.so.6 veneer, the specimen imports a
# reent-consuming body's ingredients (strtol, __errno) from it, and the veneer's
# own thunk resolves each into the elfsysv1.dll face the loader maps and names
# through AT_BASE. Two loader options compose the two-stage crossing the veneer
# needs, and the stub already carries both (stub.c):
#
#   --elf-runtime libc.so.6   map the veneer and link the specimen's PLT against
#                             its exported strtol/__errno thunks (DR-0058).
#   --runtime elfsysv1.dll    LoadLibraryA the PE face and hand its base to the
#                             image through AT_BASE, which the veneer's
#                             DT_INIT_ARRAY constructor (resolver.c) reads to
#                             resolve each thunk into the face at run time.
#
# The witness is the specimen's exit status: reent-body.c returns 42 only when
# strtol overflowed to LONG_MAX AND __errno came back ERANGE across the crossing,
# and a distinguishing 10/11/12 or a fault code otherwise. This script prints
# that status and one verdict line; measure.sh reads them.
#
# It builds only into a tmp dir and patches nothing committed. It needs the
# cross toolchain, the veneer's build inputs, and the built elfsysv1.dll face;
# measure.sh gates on those and SKIPs to verdict=staged when any is absent, so
# this script assumes they are present.
#
# Usage:  live-run.sh [-k]      (-k keeps the build dir)
# Prints: enter_status=<n>, then reent_live_run=<pass|no|faulted>.
set -u

here=$(cd "$(dirname "$0")" && pwd)
# Source (the loader, the veneer's build-libc) comes from the checkout this
# spike lives in, so a session worktree builds its own edited tree, not the
# main checkout's. Build products under a/ are gitignored, so they resolve
# against the shared git common dir instead -- the same split the sibling
# reent spikes use.
repo=$(cd "$here/../.." && pwd)
main=$(cd "$(git -C "$here" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo

keep=0
[ "${1:-}" = "-k" ] && keep=1

export PATH="$HOME/x-elfsysvnt/bin:$PATH"
cross=x86_64-elfsysvnt-linux-gnu
xg=$cross-gcc
xre=$cross-readelf
cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
stubldflags="-Wl,--stack,0x100000"
interp=/lib64/ld-linux-x86-64.so.2
face=$main/a/build/wp27-face/elfsysv1.dll

loader=$repo/loader
exec_dir=$loader/exec

if [ "$keep" = 1 ]; then bin=$here/.live-build; mkdir -p "$bin"; else bin=$(mktemp -d "${TMPDIR:-/tmp}/reent-face-live.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

fail() { echo "reent_live_run=error  ($*)"; exit 1; }

# ---- the loader stub (with the crossing) and the front end --------------
loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c \
$loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

$cc $cflags $stubldflags -o "$bin/elfsysv-stub" \
	"$exec_dir/stub.c" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" \
	"$exec_dir/dyn_init.c" "$exec_dir/enter.S" $loader_srcs \
	>"$bin/stub-build.out" 2>&1 || { sed 's/^/    stub: /' "$bin/stub-build.out"; fail "stub build failed"; }
$cc $cflags -o "$bin/elfsysv-exec" "$exec_dir/exec_main.c" \
	"$exec_dir/dispatch.c" "$exec_dir/binfmt.c" "$exec_dir/reserve.c" \
	>"$bin/fe-build.out" 2>&1 || { sed 's/^/    fe: /' "$bin/fe-build.out"; fail "front end build failed"; }

# ---- the WP-53 libc.so.6 veneer: the ELF runtime the specimen links to ---
"$repo/veneer/libc/build-libc" -B "$bin/veneer" -q >"$bin/veneer-build.out" 2>&1 \
	|| { sed 's/^/    veneer: /' "$bin/veneer-build.out"; fail "veneer build failed"; }
vlibc=$bin/veneer/libc.so.6
[ -f "$vlibc" ] || fail "veneer produced no libc.so.6"

# ---- the reent-consuming specimen ---------------------------------------
# A no-PIE ET_EXEC with a PT_INTERP that imports strtol and __errno from the
# veneer. Freestanding and -nostdlib: the crossing under test is the loader's,
# and our own `entry` is the start. max-page-size keeps its segments off a
# shared 0x10000 granule, the alignment elf_map requires and a real el8 image
# has. It links against the veneer by explicit path so its DT_NEEDED is the
# veneer's soname (libc.so.6).
xf="-ffreestanding -nostdlib -fcf-protection=none -O2"
$xg $xf -no-pie -Wl,-z,max-page-size=0x10000 -Wl,-e,entry \
	-Wl,--dynamic-linker,"$interp" -Wl,--no-as-needed -Wl,-rpath,"$bin/veneer" \
	-o "$bin/reent" "$here/reent-spec.S" "$here/reent-body.c" "$vlibc" \
	>"$bin/spec-build.out" 2>&1 \
	|| { sed 's/^/    spec: /' "$bin/spec-build.out"; fail "specimen build failed"; }
"$xre" -l "$bin/reent" 2>/dev/null | grep -q INTERP \
	|| fail "the specimen carries no PT_INTERP; the crossing gate is untested"
"$xre" -d "$bin/reent" 2>/dev/null | grep -q 'NEEDED.*libc\.so\.6' \
	|| fail "the specimen does not NEED libc.so.6; it did not bind to the veneer"

# ---- run it through the loader crossing ---------------------------------
# The front end reserves the low window into the suspended stub the one way it
# can, and ELFSYSV_STUB_OPTIONS rides the seam the front end forwards. The faced
# runtime's console wedges on a host pty, so each run is detached via cmd with
# stdin from NUL, as the sibling reent spikes do. Everything runs from beside
# the faced DLL so its own imports resolve.
work=$main/a/build/wp27-face
cp "$bin/reent" "$work/reent-live.exe" 2>/dev/null
cp "$bin/veneer/libc.so.6" "$work/libc.so.6" 2>/dev/null
cp "$bin/elfsysv-stub.exe" "$work/elfsysv-stub.exe" 2>/dev/null || cp "$bin/elfsysv-stub" "$work/elfsysv-stub.exe" 2>/dev/null
cp "$bin/elfsysv-exec.exe" "$work/elfsysv-exec.exe" 2>/dev/null || cp "$bin/elfsysv-exec" "$work/elfsysv-exec.exe" 2>/dev/null

# probe: run the front end with the given ELFSYSV_STUB_OPTIONS; return the
# specimen's exit status in $probe_st and the stub's own stderr in $probe_out.
probe() {
	( cd "$work" \
	  && ELFSYSV_STUB="$work/elfsysv-stub.exe" \
	     ELFSYSV_STUB_OPTIONS="$1" \
	     timeout 60 cmd /c "elfsysv-exec.exe reent-live.exe > reent-live.out 2>&1 < NUL" )
	probe_st=$?
	probe_out=$(tr -d '\r' < "$work/reent-live.out" 2>/dev/null)
}

# 7. The ELF half alone: map the veneer and enter, with no face base supplied.
# A loader diagnostic ("elfsysv-stub:") means the veneer did not map or the
# image did not enter; no diagnostic with a non-zero status means the crossing
# entered and the veneer's own strtol thunk ran and faulted on the null resolve
# (resolver.c's honest failure for an export with no face base) -- so the ELF
# crossing itself is not the obstacle.
probe "--elf-runtime libc.so.6"
if printf '%s' "$probe_out" | grep -q 'elf_map_err_granule'; then
	echo "veneer_maps_as_elf_runtime=no  (elf_map_err_granule; not linked granule-separable)"
	echo "crossing_enters=no  (veneer refused before entry)"
elif printf '%s' "$probe_out" | grep -q 'elfsysv-stub:'; then
	echo "veneer_maps_as_elf_runtime=yes"
	echo "crossing_enters=no  ($(printf '%s' "$probe_out" | sed -n 's/.*elfsysv-stub: //p' | head -1))"
else
	echo "veneer_maps_as_elf_runtime=yes"
	echo "crossing_enters=yes  (entered; the veneer thunk ran and null-faulted with no face base, status $probe_st)"
fi

# 8. The face base half: LoadLibraryA the faced DLL so its base reaches the
# veneer's resolver through AT_BASE. This is the cygload shape reent-bringup
# found wedges, and it does: error 1114 / heap-at-wrong-address.
probe "--runtime elfsysv1.dll"
if printf '%s' "$probe_out" | grep -q 'cannot load the runtime'; then
	echo "face_base_via_runtime=no  ($(printf '%s' "$probe_out" | sed -n 's/.*elfsysv-stub: //p' | grep 'cannot load' | head -1))"
else
	echo "face_base_via_runtime=yes"
fi

# 9. The terminal witness: both halves together. 42 only when strtol crossed
# the veneer into the face and returned LONG_MAX.
probe "--elf-runtime libc.so.6 --runtime elfsysv1.dll"
st=$probe_st
printf '%s\n' "$probe_out" | sed 's/^/    stub: /'
rm -f "$work/reent-live.exe" "$work/reent-live.out" "$work/libc.so.6" \
      "$work/elfsysv-stub.exe" "$work/elfsysv-exec.exe" 2>/dev/null

echo "enter_status=$st"
if [ "$st" = 42 ]; then
	echo "reent_live_run=pass  (strtol crossed the veneer into the face and returned LONG_MAX)"
elif [ "$st" = 12 ]; then
	echo "reent_live_run=no  (crossed and returned, but not the body's LONG_MAX; see reent-body.c)"
else
	echo "reent_live_run=faulted  (exit $st: the crossing did not return the body's byte)"
fi
