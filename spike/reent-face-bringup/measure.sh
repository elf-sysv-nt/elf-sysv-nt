#!/usr/bin/env bash
# reent-face-bringup 0.2-wip -- item 3 of the reent-tls-bringup rung.
#
# Question (unchanged): does a reent-consuming libc body, reached through the
# WP-53 libc.so.6 veneer resolving into a built elfsysv1.dll face, set the
# caller's reent (strtol overflow -> LONG_MAX, errno ERANGE) across the
# veneer->face resolution? See README.md.
#
# The terminal witness is a RUN: the veneer's own runtime-resolving thunk,
# entered through the loader's crossing (enter.S), resolving the face export
# from AT_BASE and returning the reent-consuming result. That run is not yet
# written, so the verdict stays `staged`. What this run of the skeleton adds
# over the file-existence check it replaced is the reachable half measured
# rather than assumed: with all three prerequisites present, it builds the
# veneer and records that the veneer->face crossing TARGET is real and matched
# end to end -- the precondition the live run rests on. Neither prior spike
# measured this pair: reent-veneer-thunk had no face DLL, reent-veneer-runtime
# predates the thunk bodies.
#
# Findings (deterministic; the numbers ride along as context, per the
# reproducible-spike contract):
#
#   veneer_libc_builds=yes|no        veneer/libc/build-libc produces libc.so.6.
#   strtol_body_is_thunk=yes|no      the reent-consuming body strtol is a real
#                                    runtime-resolving thunk (entry byte != 0xc3,
#                                    size > 1), not the single-byte `ret` stub
#                                    reent-veneer-runtime found.
#   reent_thunk_keys_on_face_name=yes|no
#                                    the literal "strtol" is in the veneer --
#                                    the name the thunk's resolver hands the
#                                    face's PE export directory at run time.
#   reent_carrier_present=yes|no     errno@@GLIBC_PRIVATE, the TLS carrier the
#                                    reent-consuming body writes and the ELF
#                                    caller reads, is a defined dynsym.
#   face_exports_reent_target=yes|no the built elfsysv1.dll face exports strtol
#                                    -- the run-time resolution target exists.
#   veneer_face_target_matched=yes|no  the name the thunk keys on IS a real face
#                                    export: the crossing has a target end to end.
#
# It is deliberately NOT in test/spike-regen.tsv: the terminal live-run witness
# is unmeasured, so this is a staged characterization of the crossing target,
# not a certified run (see the reproducible-spike contract). It is registered,
# and its transcript recorded, once the veneer thunk resolves and returns the
# reent across the loader crossing.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=/c/-/repo/elf-sysv-nt
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }
emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
emit_lines() { while IFS= read -r ln; do emit "$ln"; done; }
[ -n "$dest" ] && : >"$dest"

prefix=${BUILD_LIBC_PREFIX:-$HOME/x-elfsysvnt}
target=${BUILD_LIBC_TARGET:-x86_64-elfsysvnt-linux-gnu}
GCC=$prefix/bin/$target-gcc
RE=$prefix/bin/$target-readelf
dll=$repo/a/build/wp27-face/elfsysv1.dll

emit "script  reent-face-bringup 0.2-wip"
emit ""
emit "host        $(hostname)"
emit "cross gcc   $($GCC --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

# SKIP (verdict staged, exit 0) when a scratch prerequisite is absent, as the
# sibling reent spikes do -- an uncommitted build product is not a rot.
if [ ! -x "$GCC" ] || [ ! -x "$RE" ]; then
emit "skip=no cross toolchain at $prefix; build it first"
emit "verdict=staged"
exit 0
fi
if [ ! -f "$dll" ]; then
emit "skip=no faced DLL at $dll; runtime/face/build.sh first"
emit "verdict=staged"
exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-face-bringup.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# 1. Build the veneer libc.so.6 into scratch; nothing is patched afterwards.
if "$repo/veneer/libc/build-libc" -B "$tmp/libc" -q >"$tmp/build.out" 2>&1; then
emit "veneer_libc_builds=yes"
else
emit "veneer_libc_builds=no"
sed 's/^/    build: /' "$tmp/build.out" | emit_lines
emit "verdict=staged"
exit 0
fi
lib=$tmp/libc/libc.so.6

# 2. strtol is a real runtime-resolving thunk, not the 1-byte `ret` stub.
saddr=$("$RE" --dyn-syms "$lib" 2>/dev/null | awk '$8=="strtol@@GLIBC_2.2.5"{print $2;exit}')
ssize=$("$RE" --dyn-syms "$lib" 2>/dev/null | awk '$8=="strtol@@GLIBC_2.2.5"{print $3+0;exit}')
taddr=$("$RE" -SW "$lib" 2>/dev/null | awk '{for(i=1;i<=NF;i++)if($i==".text"){print $(i+2);exit}}')
toff=$("$RE" -SW "$lib" 2>/dev/null | awk '{for(i=1;i<=NF;i++)if($i==".text"){print $(i+3);exit}}')
off=$(( 16#$toff + 16#$saddr - 16#$taddr ))
entry_byte=$(od -An -tx1 -j "$off" -N1 "$lib" 2>/dev/null | tr -d ' \n')
if [ "${ssize:-0}" -gt 1 ] && [ "$entry_byte" != c3 ]; then
emit "strtol_body_is_thunk=yes  (size ${ssize}, entry byte 0x${entry_byte} != 0xc3 ret)"
else
emit "strtol_body_is_thunk=no  (size ${ssize:-0}, entry byte 0x${entry_byte})"
fi

# 3. The thunk keys on the face export name: the literal "strtol" is present.
if strings -a "$lib" 2>/dev/null | grep -qx 'strtol'; then
emit "reent_thunk_keys_on_face_name=yes"
else
emit "reent_thunk_keys_on_face_name=no"
fi

# 4. The reent carrier the body writes and the ELF caller reads is present.
if "$RE" --dyn-syms "$lib" 2>/dev/null | grep -q 'errno@@GLIBC_PRIVATE'; then
emit "reent_carrier_present=yes"
else
emit "reent_carrier_present=no"
fi

# 5. The face exports the resolution target the thunk names.
face_has=$(x86_64-w64-mingw32-objdump -p "$dll" 2>/dev/null \
| awk '/Ordinal\/Name Pointer.*Table/{f=1} f' \
| grep -cE '\bstrtol\b')
if [ "${face_has:-0}" -ge 1 ]; then
emit "face_exports_reent_target=yes"
else
emit "face_exports_reent_target=no  (matches ${face_has:-0})"
fi

# 6. End to end: the name the thunk keys on IS a real face export.
if strings -a "$lib" 2>/dev/null | grep -qx 'strtol' && [ "${face_has:-0}" -ge 1 ]; then
emit "veneer_face_target_matched=yes"
else
emit "veneer_face_target_matched=no"
fi

emit ""
emit "remaining=the live run: the veneer thunk resolving strtol from AT_BASE and"
emit "          returning LONG_MAX with errno ERANGE in the reent, entered"
emit "          through the loader crossing (enter.S) rather than measured here"
emit "          at the link/export level. Until that run, to-green's"
emit "          reent-tls-bringup row stays '-' and this spike stays unregistered."
emit "verdict=staged"
exit 0
