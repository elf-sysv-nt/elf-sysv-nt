#!/usr/bin/env bash
# WP-32: build the static ELF specimens the mapping test maps.
#
# Each is an ET_EXEC with no libc and no dynamic relocations, linked at a high
# base so it does not compete for the low addresses the spike found already
# taken in a warmed Cygwin process. The three differ only in max-page-size,
# which is what decides whether each PT_LOAD lands in its own host allocation
# granule:
#
#   good-64k   64 KB pages -- each segment in its own 64 KB granule; the
#              ordinary case, protections exact.
#   good-2m    2 MB pages  -- the el8 default; each segment in its own granule
#              with megabytes of gap between, which the mapper protects away.
#   share-4k   4 KB pages  -- all four segments packed into one 64 KB granule;
#              the case the mapper must refuse, since it cannot give them
#              distinct protections through the host's mmap.
#
# Rerunning regenerates them; they are not committed.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/specimens}
cross=${CROSS:-x86_64-elfsysvnt-linux-gnu}
gcc=$cross-gcc
base=0x10000000

mkdir -p "$out"

common="-static -nostdlib -no-pie -O0 -ffreestanding -fno-stack-protector -fcf-protection=none"

build() {
	name=$1 mps=$2
	$gcc $common -Wl,-z,max-page-size="$mps" -Wl,-Ttext-segment="$base" \
	     -o "$out/$name" "$here/specimen.c"
}

build good-64k 0x10000
build good-2m  0x200000
build share-4k 0x1000

echo "specimens in $out:"
for f in good-64k good-2m share-4k; do
	printf '  %-10s %s\n' "$f" "$("$cross-readelf" -h "$out/$f" | awk -F: '/Type:/{print $2}' | tr -d ' ')"
done
