#!/usr/bin/env bash
#
# WP-56: the stub crosses a dynamic image into its runtime, then enters it.
#
# The exec-kind branch (exec-kind-stub.sh) got the stub as far as recognising a
# dynamic image and refusing it, because entering it raw would fault on its
# first cross-object call. This certifies the next step: the stub now runs the
# dynamic crossing (dyn_exec_link, DR-0058) between the map and the entry, so
# the image's GOT and PLT resolve against an ELF runtime's exports, and only
# then enters. The check is end to end and observable:
#
#   dynamic-crosses   an interp-bearing ET_EXEC whose entry calls one function
#                     imported from an ELF runtime and leaves with what the
#                     call returned. Entered raw it would fault; run through
#                     the stub with the runtime supplied, it exits 42 — the
#                     runtime function's sentinel — which is reached only when
#                     the crossing relocated the PLT against the runtime.
#   no-runtime        the same image with no --elf-runtime is refused, not
#                     entered into the fault, and the refusal says so.
#   static-untouched  the WP-41 127-check specimen still enters and leaves 127,
#                     so the crossing branch did not disturb the static path.
#
# The image runs through the front end exactly as the WP-41 exec-elf check
# does, so the low window is reserved into the suspended stub the one way it
# can be, and --elf-runtime rides the ELFSYSV_STUB_OPTIONS seam the front end
# already forwards (the WP-27 elfcall certification hands --runtime the same
# way). The runtime here is a bare specimen, not the WP-53 libc.so.6 veneer;
# standing the branch up against a specimen is this leaf, and the veneer and
# bzip2 are the steps it carries.
#
# Usage:  dyn-cross-stub.sh [-k] [-q]
# Exit:   0 all passed, 1 a build or check failed, 2 usage.
set -u

prog=dyn-cross-stub
here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..
loader=$here/../..

keep=0
quiet=0
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  sed -n '32,33p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp56dxc.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

# The stub now composes the dynamic crossing, so its link takes the driver and
# the relocation engine beside the WP-41 sources.
loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c \
$loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

# ---- build the stub (now with the crossing) and the front end -----------
say "$prog: build the stub and front end with $cc"
$cc $cflags $stubldflags -o "$bin/elfsysv-stub" \
	"$exec_dir/stub.c" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" \
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

# the dynamic main: a no-PIE ET_EXEC with a PT_INTERP that imports greet from
# the runtime. max-page-size keeps its segments off a shared 0x10000 granule,
# the alignment elf_map requires and a real el8 image has.
$xg $xf -no-pie -Wl,-z,max-page-size=0x10000 -Wl,-e,entry \
	-Wl,--dynamic-linker,"$interp" -L"$bin" -Wl,-rpath,"$bin" \
	-o "$bin/dyn" "$here/dyn-cross-spec.S" "$here/dyn-cross-body.c" -lgreet \
	|| fail "dynamic specimen build failed"
"$xre" -l "$bin/dyn" 2>/dev/null | grep -q INTERP \
	|| fail "the dynamic specimen carries no PT_INTERP; the gate is untested"

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
grep_check() {	# grep_check NAME FILE PATTERN
	if grep -q -- "$3" "$2"; then say "    ok        $1"
	else say "    FAILED    $1: refusal did not mention '$3'"; rc=1; fi
}

say ""
# the crossing: entered with the runtime supplied, the image calls across the
# boundary and leaves 42.
ELFSYSV_STUB_OPTIONS="--elf-runtime $bin/libgreet.so" \
	"$run" "$bin/dyn" > /dev/null 2>"$bin/dyn.err"
check "dynamic-crosses" "$?" 42
[ "$rc" = 0 ] || { say "      stub said:"; sed 's/^/        /' "$bin/dyn.err"; }

# no runtime: the same image is refused rather than entered into the fault.
"$run" "$bin/dyn" > /dev/null 2>"$bin/nort.err"
check "no-runtime-refused" "$?" 1
grep_check "no-runtime-named" "$bin/nort.err" "needs an ELF runtime"

# the static path is undisturbed: still enters and leaves 127.
"$run" "$bin/stat" one two > /dev/null 2>&1
check "static-untouched" "$?" 127

say ""
if [ "$rc" = 0 ]; then say "$prog: ok"; else fail "a check failed"; fi
