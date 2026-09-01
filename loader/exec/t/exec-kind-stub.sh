#!/usr/bin/env bash
#
# WP-56: the stub classifies before it enters.
#
# WP-41's stub entered every parsed image at e_entry, which is right for a
# static executable and wrong for a dynamic one -- a dynamic image's _start
# runs before its GOT is relocated, so entered that way it faults on its
# first library call. exec_kind_of() (WP-56) is the decision that tells the
# two apart, and this certifies that the stub now consults it, between the
# map and the entry, DR-0058's single branch:
#
#   static       an ET_EXEC with no PT_INTERP keeps WP-41's direct-entry
#                path -- the 127-check specimen still runs and still leaves
#                127, so the branch did not disturb the path it certifies.
#   dynamic      an interp-bearing image (bzip2's shape) is recognized as
#                owed the crossing and refused rather than entered raw, and
#                the refusal names DR-0058 -- the crossing is staged, not
#                yet wired into the stub.
#   unsupported  a bare shared object -- a dynamic section, no interpreter --
#                is refused before it is mapped: not a program on this route.
#
# The images run through the front end, exactly as the WP-41 exec-elf check
# does, so the low window is reserved into the suspended stub the one way it
# can be. All three are ELF, so the magic-byte branch hands all three to the
# stub; the exec-kind branch inside the stub is what this certifies.
#
# Usage:  exec-kind-stub.sh [-k] [-q]
# Exit:   0 all passed, 1 a build or check failed, 2 usage.
set -u

prog=exec-kind-stub
here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..
loader=$here/../..

keep=0
quiet=0
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  sed -n '27,28p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
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

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp56exk.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c \
$loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

# ---- build the stub (now with the classifier) and the front end ---------
say "$prog: build the stub and front end with $cc"
$cc $cflags $stubldflags -o "$bin/elfsysv-stub" \
	"$exec_dir/stub.c" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" \
	"$exec_dir/dyn_init.c" \
	"$exec_dir/enter.S" $loader_srcs \
	|| fail "stub build failed"
$cc $cflags -o "$bin/elfsysv-exec" "$exec_dir/exec_main.c" \
	"$exec_dir/dispatch.c" "$exec_dir/binfmt.c" "$exec_dir/reserve.c" \
	|| fail "front end build failed"

# ---- build the three specimens with the cross toolchain -----------------
say "$prog: build the specimens with $xg"
# static: the WP-41 127-check specimen, an ET_EXEC with no interp.
$xg -static -nostdlib -no-pie -ffreestanding -fcf-protection=none \
	-Wl,-z,max-page-size=0x10000 -o "$bin/stat" "$here/specimen.S" \
	|| fail "static specimen build failed"
# dynamic: bzip2's shape -- a no-pie ET_EXEC that names an interpreter and
# imports from a shared object, so the link is dynamic and the ELF carries a
# PT_INTERP while staying ET_EXEC. Its body never runs here (the stub refuses
# it before entry); only its shape matters. max-page-size keeps its RX and RW
# segments off a shared 0x10000 granule, the alignment a real el8 image has and
# the one elf_map requires, so the stub maps it and reaches the entry branch.
xf="-ffreestanding -nostdlib -fcf-protection=none -O2"
printf 'int libval=7; void greet(void){ }\n' > "$bin/libspec.c"
printf 'extern void greet(void); extern int libval; int sink;\n'\
'void entry(void){ greet(); sink=libval; for(;;){} }\n' > "$bin/main.c"
$xg $xf -shared -fpic -Wl,-soname,libspec.so -o "$bin/libspec.so" "$bin/libspec.c" \
	|| fail "shared runtime for the dynamic specimen build failed"
$xg $xf -no-pie -Wl,-z,max-page-size=0x10000 -Wl,-e,entry \
	-Wl,--dynamic-linker,"$interp" -L"$bin" -lspec -Wl,-rpath,"$bin" \
	-o "$bin/dyn" "$bin/main.c" \
	|| fail "dynamic specimen build failed"
"$xre" -l "$bin/dyn" 2>/dev/null | grep -q INTERP \
	|| fail "the dynamic specimen carries no PT_INTERP; the gate is untested"
# unsupported: a bare shared object -- a dynamic section, no interpreter.
$xg $xf -shared -fpic -o "$bin/so.so" "$bin/libspec.c" \
	|| fail "shared specimen build failed"

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
# static: unchanged -- enters and leaves 127, the specimen's only pass.
"$run" "$bin/stat" one two > /dev/null 2>&1
check "static-still-enters" "$?" 127

# dynamic: recognized, refused before entry, and the refusal names the crossing.
"$run" "$bin/dyn" > /dev/null 2>"$bin/dyn.err"
check "dynamic-refused" "$?" 1
grep_check "dynamic-names-crossing" "$bin/dyn.err" "dynamic image"
grep_check "dynamic-names-DR"       "$bin/dyn.err" "DR-0058"

# unsupported: a shared object is not a program this route runs.
"$run" "$bin/so.so" > /dev/null 2>"$bin/so.err"
check "unsupported-refused" "$?" 1
grep_check "unsupported-named" "$bin/so.err" "not a program this route runs"

say ""
if [ "$rc" = 0 ]; then say "$prog: ok"; else fail "a check failed"; fi
