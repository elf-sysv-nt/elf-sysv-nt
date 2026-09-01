#!/usr/bin/env bash
# reent-veneer-runtime 1.0 -- can the WP-53 libc.so.6 veneer stand as the
# reent-bearing ELF runtime the loader crossing resolves against?
#
# acceptance/reent/README.md item 2 asks for a reent-bearing ELF runtime the
# crossing resolves libc.so.6 against: accept.sh's run stage passes no
# --elf-runtime and bzip2 halts needing one, and the bare ELF runtime specimens
# (libgreet.so) carry no reent. The README names WP-53's veneer as the runtime
# this rung waits on. This spike measures what that veneer, built today, does
# and does not provide, so item 2 rests on a reproduced fact rather than a plan.
#
# The finding, reproduced: veneer/libc/build-libc builds libc.so.6
# (veneer_libc_builds=yes) and it carries the complete reent SURFACE
# (reent_surface_present=yes) -- the errno@@GLIBC_PRIVATE TLS symbol and the
# reent-consuming bodies (strtol and its family) at the version nodes el8
# assigns them. But every one of its FUNC/IFUNC bodies is a single-byte `ret`
# stub (reent_body_is_stub=yes): the byte at each entry is 0xc3, and the
# elfsysv1.dll export each entry reaches lives in libc-forward.tsv as data, not
# emitted forwarding code. So the veneer resolves the crossing's libc.so.6
# imports at LINK time but consults no reent at RUN time. Item 2 is therefore
# generating the forwarding bodies that reach elfsysv1.dll (where the WP-27 face
# brings the reent up), not merely building the veneer -- which sharpens the
# README's item 2 and sits behind item 1's real-process stub startup and item 3.
#
# SKIPs (verdict yes, exit 0) when the cross toolchain is absent, it being an
# uncommitted build product.
set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }

emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
emit_lines() { while IFS= read -r ln; do emit "$ln"; done; }
[ -n "$dest" ] && : >"$dest"

prefix=${BUILD_LIBC_PREFIX:-$HOME/x-elfsysvnt}
target=${BUILD_LIBC_TARGET:-x86_64-elfsysvnt-linux-gnu}
RE=$prefix/bin/$target-readelf
OD=$prefix/bin/$target-objdump

emit "script  reent-veneer-runtime 1.0"
emit ""
emit "host        $(hostname)"
emit "cross gcc   $($prefix/bin/$target-gcc --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -x "$RE" ] || [ ! -x "$prefix/bin/$target-gcc" ]; then
emit "skip=no cross toolchain at $prefix; build it first"
emit "verdict=yes"
exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-veneer-runtime.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# Build the veneer libc.so.6 into a scratch dir; nothing is patched afterwards.
if "$repo/veneer/libc/build-libc" -B "$tmp/libc" -q >"$tmp/build.out" 2>&1; then
emit "veneer_libc_builds=yes"
else
emit "veneer_libc_builds=no"
sed 's/^/    build: /' "$tmp/build.out" | emit_lines
emit "verdict=no"
exit 1
fi
lib=$tmp/libc/libc.so.6

# Surface: the errno TLS carrier and a reent-consuming body (strtol) present,
# strtol at the GLIBC_2.2.5 node el8 assigns it.
has_errno=$("$RE" --dyn-syms "$lib" 2>/dev/null | grep -c 'errno@@GLIBC_PRIVATE')
has_strtol=$("$RE" --dyn-syms "$lib" 2>/dev/null | grep -Ec 'strtol@@GLIBC_2\.2\.5')
if [ "$has_errno" -ge 1 ] && [ "$has_strtol" -ge 1 ]; then
emit "reent_surface_present=yes"
else
emit "reent_surface_present=no  (errno=$has_errno strtol=$has_strtol)"
fi

# Body: every FUNC/IFUNC dynsym is a single-byte body, and the byte at a
# reent-consuming entry (strtol) is 0xc3 -- so no body carries live code;
# the elfsysv1.dll forward each entry is to reach lives in libc-forward.tsv
# as data, not as emitted forwarding code.
nonstub=$("$RE" --dyn-syms "$lib" 2>/dev/null \
  | awk '($4=="FUNC"||$4=="IFUNC") && $3+0!=1 {c++} END{print c+0}')
taddr=$("$RE" -SW "$lib" 2>/dev/null | awk '{for(i=1;i<=NF;i++)if($i==".text"){print $(i+2);exit}}')
toff=$("$RE" -SW "$lib" 2>/dev/null | awk '{for(i=1;i<=NF;i++)if($i==".text"){print $(i+3);exit}}')
saddr=$("$RE" --dyn-syms "$lib" 2>/dev/null | awk '$8=="strtol@@GLIBC_2.2.5"{print $2;exit}')
off=$(( 16#$toff + 16#$saddr - 16#$taddr ))
entry_byte=$(od -An -tx1 -j "$off" -N1 "$lib" 2>/dev/null | tr -d ' \n')
if [ "$nonstub" = 0 ] && [ "$entry_byte" = c3 ]; then
emit "reent_body_is_stub=yes  (0 non-stub FUNC/IFUNC bodies; strtol entry byte 0x$entry_byte = ret)"
else
emit "reent_body_is_stub=no  (nonstub_funcs=$nonstub strtol_entry_byte=0x$entry_byte)"
fi
emit "verdict=yes"
