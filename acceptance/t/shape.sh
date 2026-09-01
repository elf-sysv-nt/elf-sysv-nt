#!/usr/bin/env bash
#
# WP-56 certification for the acceptance image-shape helper (img_shape.c).
#
# Build the helper with the host compiler over the loader packages it reads --
# WP-31's parser and WP-56's exec-kind classifier -- then build three specimens
# with the cross toolchain whose shapes are known, and hold img_shape's verdict
# to each:
#
#   dynamic      an interp-bearing image (bzip2's shape): kind=dynamic, its
#                interp the path we named, and it reads back the way accept.sh
#                gates a package before crediting it.
#   static       an ET_EXEC with no PT_INTERP: kind=static, interp -.
#   unsupported  a shared object -- a dynamic section but no interpreter:
#                kind=unsupported, loadable through the dl surface but not
#                runnable as a program here.
#
# The classifier is what the dynamic driver stands behind, so a specimen whose
# readelf shape disagrees with img_shape's verdict is a failure. The dynamic
# specimen's PT_INTERP is confirmed with readelf first, or the gate is untested.
#
# Usage: shape.sh [-q]
# Exit: 0 passed, 1 a build or check failed.

set -u
prog=shape
here=$(cd "$(dirname "$0")" && pwd)
acc=$here/..
root=$acc/..
elf=$root/loader/elf
exec_dir=$root/loader/exec

quiet=0
[ "${1:-}" = "-q" ] && quiet=1
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

export PATH="$HOME/x-elfsysvnt/bin:$PATH"
xg=x86_64-elfsysvnt-linux-gnu-gcc
xre=x86_64-elfsysvnt-linux-gnu-readelf
command -v "$xg" >/dev/null 2>&1 || fail "cross gcc $xg not on PATH (add /c/-/x-elfsysvnt/bin)"

cc=${CC:-gcc}
work=$(mktemp -d "${TMPDIR:-/tmp}/wp56shape.XXXXXX")
trap 'rm -rf "$work"' EXIT

# ---- build the helper over the loader packages --------------------------
hf="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
say "$prog: build img_shape with $cc"
$cc $hf -c "$elf/elf_parse.c"       -o "$work/elf_parse.o" || fail "elf_parse.c"
$cc $hf -c "$exec_dir/exec_kind.c"  -o "$work/exec_kind.o" || fail "exec_kind.c"
$cc $hf -Werror -c "$here/img_shape.c" -o "$work/img_shape.o" || fail "img_shape.c"
$cc -o "$work/img_shape" "$work/img_shape.o" "$work/elf_parse.o" "$work/exec_kind.o" \
|| fail "link img_shape"

# ---- build the specimens with the cross toolchain -----------------------
printf 'void entry(void){ for(;;){} }\n' > "$work/spec.c"
xf="-ffreestanding -nostdlib -fcf-protection=none -O2 -Wl,-e,entry"
interp=/lib64/ld-linux-x86-64.so.2

say "$prog: build the specimens with $xg"
$xg $xf -fpie -pie -Wl,--dynamic-linker,"$interp" -o "$work/dyn" "$work/spec.c" \
|| fail "dynamic specimen"
$xg $xf -no-pie -o "$work/stat" "$work/spec.c" || fail "static specimen"
$xg $xf -shared -fpic -o "$work/so.so" "$work/spec.c" || fail "shared specimen"

"$xre" -l "$work/dyn" 2>/dev/null | grep -q INTERP \
|| fail "the dynamic specimen carries no PT_INTERP; the gate is untested"

# ---- hold the helper to each shape --------------------------------------
fails=0
ck() { # ck LABEL FILE EXPECT_KIND EXPECT_INTERP
local label=$1 file=$2 wantk=$3 wanti=$4 out kind interp rc
out=$("$work/img_shape" "$file"); rc=$?
kind=$(printf '%s\n' "$out" | sed -n 's/^kind=//p')
interp=$(printf '%s\n' "$out" | sed -n 's/^interp=//p')
if [ "$rc" != 0 ]; then
printf '    %-40s FAILED (exit %d)\n' "$label" "$rc"; fails=$((fails+1)); return
fi
if [ "$kind" != "$wantk" ]; then
printf '    %-40s FAILED (kind=%s want %s)\n' "$label" "$kind" "$wantk"; fails=$((fails+1)); return
fi
if [ -n "$wanti" ] && [ "$interp" != "$wanti" ]; then
printf '    %-40s FAILED (interp=%s want %s)\n' "$label" "$interp" "$wanti"; fails=$((fails+1)); return
fi
printf '    %-40s ok\n' "$label"
}

say ""
ck "interp-bearing image reads dynamic" "$work/dyn"   dynamic     "$interp"
ck "static image reads static"          "$work/stat"  static      "-"
ck "shared object reads unsupported"    "$work/so.so" unsupported "-"

say ""
if [ "$fails" = 0 ]; then say "$prog: ok"; exit 0; fi
fail "$fails shape check(s) failed"
