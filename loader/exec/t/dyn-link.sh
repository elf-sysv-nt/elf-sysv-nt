#!/usr/bin/env bash
#
# WP-56 certification for the dynamic crossing driver. Build the driver, the
# loader packages it composes, and the harness with the host compiler; build a
# libgreet "runtime" and an interp-bearing "hello" main with the cross
# toolchain; and hold dyn_exec_link() to its contract over the pair. The main
# is the reloc harness's own hello specimen, relinked with a PT_INTERP so the
# classifier reads it as dynamic, and libgreet is the runtime it imports from.
#
# The bar: the guard refuses a null request, a non-dynamic image, and a dynamic
# image with no runtime; the real pair links through the driver into a scope
# with the main as root and the runtime behind it; and the entered image makes
# its cross-object call and reads its imported datum -- the crossing the driver
# composed runs.
#
# Usage: dyn-link.sh [-k] [-q]
#   -k, --keep   keep the built binaries
#   -q, --quiet  errors only
# Exit: 0 passed, 1 a build or check failed, 2 usage.

set -u
prog=dyn-link
here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..
elf=$here/../../elf
map=$here/../../map
reloc=$here/../../reloc

keep=0
quiet=0
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  sed -n '18,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-k|--keep)  keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--)         shift; break ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

export PATH="$HOME/x-elfsysvnt/bin:$PATH"
xg=x86_64-elfsysvnt-linux-gnu-gcc
command -v "$xg" >/dev/null 2>&1 || fail "cross gcc $xg not on PATH"

cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
xf="-ffreestanding -nostdlib -fcf-protection=none -O2"
xlf="-Wl,-z,max-page-size=0x10000"
interp=/lib64/ld-linux-x86-64.so.2

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp56dyn.XXXXXX"); fi
test_bin=$bin/dyn_exec_test
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

# ---- build the driver, the packages it composes, and the harness --------
say "building the driver and harness with $cc"
$cc $cflags -c "$exec_dir/dyn_exec.c"   -o "$bin/dyn_exec.o"    || fail "dyn_exec.c"
$cc $cflags -c "$exec_dir/exec_kind.c"  -o "$bin/exec_kind.o"   || fail "exec_kind.c"
$cc $cflags -c "$reloc/elf_reloc.c"     -o "$bin/elf_reloc.o"   || fail "elf_reloc.c"
$cc $cflags -c "$reloc/reloc_resolve.S" -o "$bin/reloc_resolve.o" || fail "reloc_resolve.S"
$cc $cflags -c "$elf/elf_parse.c"       -o "$bin/elf_parse.o"   || fail "elf_parse.c"
$cc $cflags -c "$map/elf_map.c"         -o "$bin/elf_map.o"     || fail "elf_map.c"
$cc $cflags -c "$map/host_mem.c"        -o "$bin/host_mem.o"    || fail "host_mem.c"
$cc $cflags -c "$here/dyn_exec_test.c"  -o "$bin/dyn_exec_test.o" || fail "dyn_exec_test.c"
$cc $cflags -c "$reloc/t/enter.S"       -o "$bin/enter.o"       || fail "enter.S"
$cc -o "$test_bin" \
	"$bin/dyn_exec_test.o" "$bin/dyn_exec.o" "$bin/exec_kind.o" \
	"$bin/elf_reloc.o" "$bin/reloc_resolve.o" "$bin/elf_parse.o" \
	"$bin/elf_map.o" "$bin/host_mem.o" "$bin/enter.o" || fail "link dyn_exec_test"

# ---- build the specimens with the cross toolchain -----------------------
say "building the runtime and the interp-bearing main with the cross toolchain"
$xg $xf $xlf -fpic -shared -Wl,-soname,libgreet.so \
	-o "$bin/libgreet.so" "$reloc/t/greetlib.c" || fail "libgreet.so"

# The main is the reloc harness's hello, linked lazy against the runtime and
# given a PT_INTERP so exec_kind_of() reads it as dynamic. That interp is never
# consulted here -- the test enters the image directly -- it only flips the
# classifier's verdict, which is the driver's gate.
$xg $xf $xlf -fpie -pie -Wl,-z,lazy -Wl,-e,entry -Wl,--dynamic-linker,"$interp" \
	-o "$bin/dmain" "$reloc/t/hello.c" \
	-L"$bin" -lgreet -Wl,-rpath,"$bin" || fail "dmain"

# The interp must actually be present, or the classifier's gate is untested.
x86_64-elfsysvnt-linux-gnu-readelf -l "$bin/dmain" 2>/dev/null \
	| grep -q INTERP || fail "the main carries no PT_INTERP"

# ---- run ----------------------------------------------------------------
say ""
"$test_bin" "$bin/dmain" "$bin/libgreet.so"
rc=$?
say ""
[ "$rc" = 0 ] && say "the driver passed" || say "the driver failed"
exit $rc
