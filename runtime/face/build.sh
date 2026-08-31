#!/bin/bash
# WP-27: put the System V face on elfsysv1.dll.
#
# Compiles the generated faces (int, typed), the core bindings, and WP-24's
# variadic entries -- generated veneer and hand-written nonformat alike --
# onto the __face_ prefix so the seam names one convention; then relinks the
# WP-26 DLL with face.din as the export table and the face objects on the
# link line, through the DIN_FILE / FACE_OFILES seam the vendor tree now
# carries.
#
# Needs the WP-26 build tree under a/build/wp26 (runtime/winsup/build.sh).
# Logs to a/build-logs/27-sysv-face.log. Neither tree nor log is committed.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=/c/-/repo/elf-sysv-nt
build=$repo/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin
out=$repo/a/build/wp27-face
log=$repo/a/build-logs/27-sysv-face.log

mkdir -p "$out" "$repo/a/build-logs"
say() { printf '%s\n' "$*" | tee -a "$log"; }

[ -f "$build/Makefile" ] || {
  say "no WP-26 build tree at $build; run runtime/winsup/build.sh first"
  exit 1; }

cflags="-g -O2 -mno-red-zone -Wall -Werror"

say "== face objects =="
gcc -I "$here" -c "$here/int-faces.gen.S" -o "$out/int-faces.o"
gcc -I "$here" -c "$here/ctx-faces.gen.S" -o "$out/ctx-faces.o"
gcc $cflags -c "$here/typed-faces.gen.c" -o "$out/typed-faces.o"
gcc $cflags -c "$here/cores.c" -o "$out/cores.o"
gcc $cflags -c "$here/nonformat-cores.c" -o "$out/nonformat-cores.o"
gcc $cflags -c "$here/../varargs/sv2ms.c" -o "$out/sv2ms.o"
gcc $cflags -c "$here/tlsdir.c" -o "$out/tlsdir.o"

say "== nonformat entries compiled onto the __face_ prefix =="
# the same rename the generated veneer gets, driven by the enumeration's
# PROTOTYPE rows so the list cannot drift from what nonformat.c defines
nfdefs=$(awk -F'\t' '$3=="PROTOTYPE" {printf " -D%s=__face_%s", $1, $1}' \
  "$here/../varargs/variadic-exports.tsv")
gcc $cflags -fno-builtin $nfdefs -I "$here/../varargs" \
  -c "$here/../varargs/nonformat.c" -o "$out/nonformat-faced.o"

say "== veneer entries generated onto the __face_ prefix =="
bash "$here/../varargs/gen-veneer.sh" --prefix __face_ \
  --c "$out/veneer-faced.c" --h "$out/veneer-faced.h"
gcc $cflags -I "$here/../varargs" -I "$out" \
  -c "$out/veneer-faced.c" -o "$out/veneer-faced.o"
# nothing in the faced object may define an export-table name outright
for o in veneer-faced nonformat-faced; do
  if nm "$out/$o.o" | awk '$2~/[TD]/{print $3}' \
     | grep -qxF -f <(cut -f1 "$here/face.tsv"); then
    say "$o.o still defines an export name"; exit 1
  fi
done

face_ofiles="$out/int-faces.o $out/ctx-faces.o $out/typed-faces.o $out/cores.o"
face_ofiles="$face_ofiles $out/nonformat-cores.o $out/nonformat-faced.o"
face_ofiles="$face_ofiles $out/veneer-faced.o $out/sv2ms.o $out/tlsdir.o"

say "== relink: face.din on the DLL =="
cd "$build"
rm -f cygwin.def sigfe.s cygwin.sc new-cygwin1.dll
make DIN_FILE="$here/face.din" FACE_OFILES="$face_ofiles" \
     new-cygwin1.dll 2>&1 | tee -a "$log"

cp new-cygwin1.dll "$out/elfsysv1.dll"
say "== faced DLL at $out/elfsysv1.dll =="
