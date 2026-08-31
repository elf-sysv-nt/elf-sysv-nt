#!/bin/bash
# WP-26: does the winsup build reproduce from the pinned ref, byte for byte
# in the parts the toolchain makes reproducible?
#
# A second clean build under a/build/wp26-repro, then a section-level compare
# of new-cygwin1.dll against the first build's. The PE header carries a link
# timestamp and the debug sections carry build paths, so the comparison is
# the loadable sections that should not vary: .text and .data. A differing
# .rdata is reported but does not fail the check, since it may embed paths.
set -euo pipefail

repo=/c/-/repo/elf-sysv-nt
src=/c/-/repo/newlib-cygwin
first=$repo/a/build/wp26
build=$repo/a/build/wp26-repro
log=$repo/a/build-logs/wp26-repro.log
pin=b11613e47
rel=x86_64-pc-cygwin/winsup/cygwin/new-cygwin1.dll

mkdir -p "$build" "$repo/a/build-logs"
deps=$repo/a/build/deps/lib
git -C "$src" merge-base --is-ancestor $pin HEAD

cd "$build"
if [ ! -f Makefile ]; then
  "$src/configure" --prefix="$build/install" --disable-doc \
    CFLAGS="-g -O2 -mno-red-zone" CXXFLAGS="-g -O2 -mno-red-zone" \
    LDFLAGS="-L$deps" 2>&1 | tee -a "$log"
fi
make -j"$(nproc)" 2>&1 | tee -a "$log"

a=$first/$rel
b=$build/$rel
d=$(mktemp -d); trap 'rm -rf "$d"' EXIT
verdict=PASS
for sec in .text .data .rdata; do
  objcopy -O binary --only-section=$sec "$a" "$d/a$sec.bin"
  objcopy -O binary --only-section=$sec "$b" "$d/b$sec.bin"
  if cmp -s "$d/a$sec.bin" "$d/b$sec.bin"; then
    echo "repro: $sec identical" | tee -a "$log"
  else
    echo "repro: $sec DIFFERS" | tee -a "$log"
    [ "$sec" = .rdata ] || verdict=FAIL
  fi
done
echo "repro: $verdict" | tee -a "$log"
[ "$verdict" = PASS ]
