#!/bin/bash
# WP-26: build newlib-cygwin (pinned b11613e47 + re-face commits) as
# elfsysv1.dll, -mno-red-zone throughout, out of tree under a/build/wp26.
# Run from anywhere in the primary Cygwin root (3.6.10, gcc 14).
set -euo pipefail

repo=/c/-/repo/elf-sysv-nt
src=/c/-/repo/newlib-cygwin
build=$repo/a/build/wp26
log=$repo/a/build-logs/wp26-winsup-dll.log
pin=b11613e47

mkdir -p "$build" "$repo/a/build-logs"

# libbfd needs zstd; the root ships only the runtime DLL, so make ld an
# import stub it can find under -lzstd.
deps=$repo/a/build/deps/lib
mkdir -p "$deps"
ln -sf /usr/bin/cygzstd-1.dll "$deps/cygzstd.dll"

git -C "$src" merge-base --is-ancestor $pin HEAD || {
  echo "source tree is not based on pinned ref $pin" >&2; exit 1; }

cd "$build"
if [ ! -f Makefile ]; then
  "$src/configure" --prefix="$build/install" --disable-doc \
    CFLAGS="-g -O2 -mno-red-zone" CXXFLAGS="-g -O2 -mno-red-zone" LDFLAGS="-L$deps" \
    2>&1 | tee -a "$log"
fi
make -j"$(nproc)" 2>&1 | tee -a "$log"
