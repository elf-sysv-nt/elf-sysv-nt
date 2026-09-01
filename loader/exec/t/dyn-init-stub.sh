#!/usr/bin/env bash
#
# WP-56: the stub runs a crossed dynamic image's DT_INIT chain before entry.
#
# The dynamic crossing (dyn-cross-stub.sh) got the stub to relocate a dynamic
# image against its runtime and enter it. But a real el8 program's constructors
# must run before its entry, and these images have no startup file to run them,
# so the loader does -- DT_INIT then DT_INIT_ARRAY, the ABI's order (WP-56,
# dyn_init). This certifies that step end to end and observably:
#
#   init-order-runs   a dynamic ET_EXEC whose DT_INIT sets g=2 (from g=0) and
#                     whose DT_INIT_ARRAY entry, seeing g==2 and greet()==42
#                     across the crossing, sets g=42; the entry carries g out.
#                     g is 0 in the image, so 42 is reached only when the loader
#                     ran DT_INIT first and DT_INIT_ARRAY second, both before the
#                     entry -- any other order or a skip poisons g off 42.
#   tags-present      the specimen actually carries DT_INIT and DT_INIT_ARRAY,
#                     so the pass is not vacuous.
#   static-untouched  the WP-41 127-check specimen still enters and leaves 127,
#                     so nothing on the static path changed.
#
# The image runs through the front end exactly as dyn-cross-stub.sh does, and
# --elf-runtime rides the ELFSYSV_STUB_OPTIONS seam the same way. The runtime is
# the bare libgreet.so specimen, not the WP-53 veneer; bzip2's make test is the
# step this leaf carries.
#
# Usage:  dyn-init-stub.sh [-k] [-q]
# Exit:   0 all passed, 1 a build or check failed, 2 usage.
set -u

prog=dyn-init-stub
here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..
loader=$here/../..

keep=0
quiet=0
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  sed -n '28,29p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-k|--keep)  keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--)         shift; break ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

export PATH="$HOME/x-elfsysvnt/bin:$PATH"
cross=x86_64-elfsysvnt-linux-gnu
xg=$cross-gcc
xre=$cross-readelf
command -v "$xg" >/dev/null 2>&1 || fail "cross gcc $xg not on PATH (add /c/-/x-elfsysvnt/bin)"

cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
stubldflags="-Wl,--stack,0x100000"
interp=/lib64/ld-linux-x86-64.so.2

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp56dxi.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c \
$loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

# ---- build the stub (with crossing and init) and the front end ----------
say "$prog: build the stub and front end with $cc"
$cc $cflags $stubldflags -o "$bin/elfsysv-stub" \
	"$exec_dir/stub.c" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" \
	"$exec_dir/dyn_init.c" \
	"$exec_dir/enter.S" $loader_srcs \
	|| fail "stub build failed"
$cc $cflags -o "$bin/elfsysv-exec" "$exec_dir/exec_main.c" \
	"$exec_dir/dispatch.c" "$exec_dir/binfmt.c" "$exec_dir/reserve.c" \
	|| fail "front end build failed"

# ---- build the specimens with the cross toolchain -----------------------
say "$prog: build the specimens with $xg"
xf="-ffreestanding -nostdlib -fcf-protection=none -O2"

# the ELF runtime: an ET_DYN exporting greet().
$xg $xf -shared -fpic -Wl,-z,max-page-size=0x10000 -Wl,-soname,libgreet.so \
	-o "$bin/libgreet.so" "$here/greet-rt.c" \
	|| fail "runtime specimen build failed"

# the dynamic main: a no-PIE ET_EXEC with a PT_INTERP, a DT_NEEDED on the
# runtime, DT_INIT set to my_dt_init by -Wl,-init, and a DT_INIT_ARRAY from the
# constructor attribute. max-page-size keeps its segments off a shared 0x10000
# granule, the alignment elf_map requires and a real el8 image has.
$xg $xf -no-pie -Wl,-z,max-page-size=0x10000 -Wl,-e,entry -Wl,-init,my_dt_init \
	-Wl,--dynamic-linker,"$interp" -L"$bin" -Wl,-rpath,"$bin" \
	-o "$bin/dyn" "$here/dyn-init-spec.S" "$here/dyn-init-body.c" -lgreet \
	|| fail "dynamic specimen build failed"
"$xre" -l "$bin/dyn" 2>/dev/null | grep -q INTERP \
	|| fail "the init specimen carries no PT_INTERP; the gate is untested"

# the static specimen: the WP-41 127-check image, an ET_EXEC with no interp.
$xg -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
	-Wl,-z,max-page-size=0x10000 -o "$bin/stat" "$here/specimen.S" \
	|| fail "static specimen build failed"

export ELFSYSV_STUB=$bin/elfsysv-stub.exe
run=$bin/elfsysv-exec
rc=0

check() {	# check NAME GOT WANT
	if [ "$2" = "$3" ]; then say "    ok        $1: $2"
	else say "    FAILED    $1: got $2, wanted $3"; rc=1; fi
}

say ""
# the specimen carries the tags the pass depends on.
dtags=$("$xre" -d "$bin/dyn" 2>/dev/null)
case $dtags in *"(INIT)"*)       say "    ok        tags-present: DT_INIT" ;;
	*) say "    FAILED    tags-present: DT_INIT missing"; rc=1 ;; esac
case $dtags in *"(INIT_ARRAY)"*) say "    ok        tags-present: DT_INIT_ARRAY" ;;
	*) say "    FAILED    tags-present: DT_INIT_ARRAY missing"; rc=1 ;; esac

# the chain: DT_INIT then DT_INIT_ARRAY run in order, before the entry, so the
# entry carries out 42.
ELFSYSV_STUB_OPTIONS="--elf-runtime $bin/libgreet.so" \
	"$run" "$bin/dyn" > /dev/null 2>"$bin/dyn.err"
check "init-order-runs" "$?" 42
[ "$rc" = 0 ] || { say "      stub said:"; sed 's/^/        /' "$bin/dyn.err"; }

# the static path is undisturbed: still enters and leaves 127.
"$run" "$bin/stat" one two > /dev/null 2>&1
check "static-untouched" "$?" 127

say ""
if [ "$rc" = 0 ]; then say "$prog: ok"; else fail "a check failed"; fi
