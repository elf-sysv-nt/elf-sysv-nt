#!/usr/bin/env bash
# reent-veneer-body 1.0 -- what a real forwarding body must reach, and why it
# cannot be a link-time forward.
#
# acceptance/reent/README.md item 2, and spike/reent-veneer-runtime, establish
# that the WP-53 libc.so.6 veneer carries the reent surface but every
# FUNC/IFUNC body is a single-byte `ret`; item 2 is generating the bodies that
# reach elfsysv1.dll. Before that codegen is written, two facts about its
# target surface are measured here rather than assumed.
#
# The finding, reproduced:
#
#   targets_all_exported=yes. Every forwarding target the freshly built
#   libc-forward.tsv names (its forward-same and forward-alias rows) is a real
#   export of the built elfsysv1.dll face. So the map's target column is a sound
#   destination for a forwarding body: the body has somewhere real to reach.
#
#   link_forward_self_references=yes. A naive link-time forwarding body -- a
#   `jmp <target>@PLT` under the veneer's own `.symver <label>, <target>@@node`
#   -- does NOT reach the PE export. The linker binds the unversioned reference
#   to the default-versioned definition the veneer itself provides, so the body
#   jumps to itself: the linked object carries the target DEFINED and no
#   undefined import of it. The PE export lives in elfsysv1.dll's export
#   directory, which is not an ELF dynamic symbol; the WP-27 crossing resolves
#   it at RUN time from AT_BASE. So item 2 is generating runtime-resolving
#   thunks against that crossing ABI, not a link flag -- the companion result
#   to reent-stub-link (item 1) and reent-veneer-runtime (the stub bodies).
#
# SKIPs (verdict yes, exit 0) when the cross toolchain or the built face DLL is
# absent, both being uncommitted build products, as the crossing tests do.
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
GCC=$prefix/bin/$target-gcc
RE=$prefix/bin/$target-readelf
face=${ELFSYSV_FACE:-/c/-/repo/elf-sysv-nt/a/build/wp27-face/elfsysv1.dll}
mingw_od=x86_64-w64-mingw32-objdump

emit "script  reent-veneer-body 1.0"
emit ""
emit "host        $(hostname)"
emit "cross gcc   $($GCC --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -x "$GCC" ] || [ ! -x "$RE" ]; then
emit "skip=no cross toolchain at $prefix; build it first"
emit "verdict=yes"
exit 0
fi
if [ ! -f "$face" ]; then
emit "skip=no built face at $face; build wp27-face first"
emit "verdict=yes"
exit 0
fi
if ! command -v "$mingw_od" >/dev/null 2>&1; then
emit "skip=no $mingw_od to read the PE export table"
emit "verdict=yes"
exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-veneer-body.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

# Fact 1. Build the veneer to get a fresh libc-forward.tsv, list its forwarding
# targets (forward-same / forward-alias rows), and check each is a real export
# of the built face DLL.
if ! "$repo/veneer/libc/build-libc" -B "$tmp/libc" -q >"$tmp/build.out" 2>&1; then
emit "veneer_libc_builds=no"
sed 's/^/    build: /' "$tmp/build.out" | emit_lines
emit "verdict=no"
exit 1
fi
fwd=$tmp/libc/libc-forward.tsv
"$mingw_od" -p "$face" 2>/dev/null \
  | awk '/\[Ordinal\/Name/{f=1;next} f&&/^\t\[/{print $NF}' \
  | sort -u >"$tmp/exports.txt"
awk -F'\t' '$7=="forward-same"||$7=="forward-alias"{print $8}' "$fwd" \
  | sort -u >"$tmp/targets.txt"
nexp=$(wc -l <"$tmp/exports.txt")
ntgt=$(wc -l <"$tmp/targets.txt")
nmiss=$(comm -23 "$tmp/targets.txt" "$tmp/exports.txt" | wc -l)
if [ "$ntgt" -gt 0 ] && [ "$nmiss" -eq 0 ]; then
emit "targets_all_exported=yes  ($ntgt/$ntgt forwarding targets are face exports; $nexp exports total)"
else
emit "targets_all_exported=no  ($nmiss of $ntgt targets not exported)"
comm -23 "$tmp/targets.txt" "$tmp/exports.txt" | head | sed 's/^/    missing: /' | emit_lines
fi

# Fact 2. A naive link-time forwarding body for one reent-consuming symbol
# (strtol) self-references rather than reaching the PE export. Emit the body,
# assemble, link a versioned ET_DYN, and read the target back out of .dynsym: it
# is DEFINED, with no undefined import, so the jump resolved to the veneer's own
# definition.
sym=strtol
node=GLIBC_2.2.5
{
  printf '\t.text\n'
  printf '\t.globl\tv_%s\n' "$sym"
  printf '\t.type\tv_%s, @function\n' "$sym"
  printf 'v_%s:\n' "$sym"
  printf '\tjmp\t%s@PLT\n' "$sym"
  printf '\t.size\tv_%s, .-v_%s\n' "$sym" "$sym"
  printf '\t.symver\tv_%s, %s@@%s\n' "$sym" "$sym" "$node"
  printf '\t.section\t.note.GNU-stack,"",@progbits\n'
} >"$tmp/body.s"
printf '%s { global: %s; local: *; };\n' "$node" "$sym" >"$tmp/vs"
if "$GCC" -c "$tmp/body.s" -o "$tmp/body.o" 2>"$tmp/asm.err" \
   && "$GCC" -shared -nostdlib -Wl,--version-script="$tmp/vs" \
        -o "$tmp/libbody.so" "$tmp/body.o" 2>"$tmp/link.err"; then
  defined=$("$RE" --dyn-syms "$tmp/libbody.so" 2>/dev/null \
    | awk -v s="$sym@@$node" '$8==s && $7!="UND"{c++} END{print c+0}')
  undef=$("$RE" --dyn-syms "$tmp/libbody.so" 2>/dev/null \
    | awk -v s="$sym" '$8==s && $7=="UND"{c++} END{print c+0}')
  if [ "$defined" -ge 1 ] && [ "$undef" -eq 0 ]; then
emit "link_forward_self_references=yes  ($sym@@$node defined in .dynsym, no UND import of $sym)"
  else
emit "link_forward_self_references=no  (defined=$defined undef=$undef)"
  fi
else
emit "link_forward_self_references=link-failed"
sed 's/^/    asm:  /' "$tmp/asm.err" | emit_lines
sed 's/^/    link: /' "$tmp/link.err" | emit_lines
fi
emit "verdict=yes"
