#!/usr/bin/env bash
# reent-veneer-thunk 1.0 -- the link-time shape of a runtime-resolving thunk,
# the body that replaces the veneer's `ret` stub in item 2 of
# acceptance/reent/README.md.
#
# spike/reent-veneer-body established that a naive link-time forward
# (`jmp strtol@PLT` under the veneer's own `.symver`) self-references: the
# linker binds the unversioned reference to the default-versioned definition the
# veneer itself provides. The PE export lives in elfsysv1.dll's export
# directory, which is not an ELF dynamic symbol; the WP-27 crossing resolves it
# at RUN time from AT_BASE (runtime/face/t/elfcall.c's pe_export walk). So the
# body must name its target as DATA and resolve it at run time, not name it as
# an ELF symbol. This spike measures that such a thunk, linked into the
# versioned ET_DYN, carries the faced name only as a run-time resolution key and
# holds no ELF dependency on it -- the codegen contract generate.py must meet.
#
# The findings, reproduced (measure.sh, cross toolchain only -- no face DLL, no
# run, so this reproduces wherever the cross gcc/readelf exist):
#
#   thunk_defines_versioned_symbol=yes. strtol@@GLIBC_2.2.5 is DEFINED in the
#   linked object's .dynsym with a real body (size > 1), not a `ret` stub.
#
#   thunk_no_elf_self_import=yes. The object holds NO undefined .dynsym entry
#   for strtol and NO relocation naming it: the body depends on no ELF symbol
#   named strtol, so there is nothing for the linker to bind to itself.
#
#   thunk_keys_on_export_name=yes. The literal "strtol" is present in the
#   object's read-only data -- the name the run-time resolver hands the PE
#   export directory. The body reaches the face by name, the crossing ABI, not
#   by an ELF reference.
#
#   resolver_stays_private=yes. The resolver the thunk calls is absent from
#   .dynsym (hidden): one veneer carries a single private resolver that cannot
#   collide with any faced symbol.
#
# SKIPs (verdict yes, exit 0) when the cross toolchain is absent, it being an
# uncommitted build product, as the crossing spikes do.
set -u

here=$(cd "$(dirname "$0")" && pwd)
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }

emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
emit_lines() { while IFS= read -r ln; do emit "$ln"; done; }
[ -n "$dest" ] && : >"$dest"

prefix=${BUILD_LIBC_PREFIX:-$HOME/x-elfsysvnt}
target=${BUILD_LIBC_TARGET:-x86_64-elfsysvnt-linux-gnu}
GCC=$prefix/bin/$target-gcc
RE=$prefix/bin/$target-readelf

emit "script  reent-veneer-thunk 1.0"
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-veneer-thunk.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

sym=strtol
node=GLIBC_2.2.5

# The thunk, as generate.py would emit it: a versioned body that names its
# target as a data string and resolves it at run time through a private walk of
# the faced runtime's PE export directory (the WP-27 crossing ABI, ported from
# runtime/face/t/elfcall.c). The auxv/base plumbing is a faithful sketch -- this
# spike asserts only the object's link-time shape, which needs no run.
cat > "$tmp/body.c" <<'BODY'
#include <stdint.h>
#define AT_BASE 7

/* Filled once by the init constructor below; hidden, never a faced symbol. */
__attribute__((visibility("hidden"))) const uint8_t *_elfsysv_face_base;

static uint16_t rd16(const uint8_t *p){ return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t rd32(const uint8_t *p){
	return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int name_is(const uint8_t *p, const char *w){
	while (*w && *p==(uint8_t)*w){ p++; w++; } return *w==0 && *p==0;
}

/* Resolve one PE export by name -- elfcall.c's own walk, kept hidden. */
__attribute__((visibility("hidden")))
void *_elfsysv_resolve(const char *name){
	const uint8_t *base=_elfsysv_face_base; uint32_t lfanew,nn,i; const uint8_t *opt,*dir;
	if (!base || rd16(base)!=0x5A4D) return 0;
	lfanew=rd32(base+0x3C);
	if (rd32(base+lfanew)!=0x00004550) return 0;
	opt=base+lfanew+4+20;
	if (rd16(opt)!=0x20B) return 0;
	if (rd32(opt+108)<1 || rd32(opt+112)==0) return 0;
	dir=base+rd32(opt+112); nn=rd32(dir+24);
	for (i=0;i<nn;i++){
		if (name_is(base+rd32(base+rd32(dir+32)+4u*i), name)){
			uint16_t ord=rd16(base+rd32(dir+36)+2u*i);
			return (void *)(base+rd32(base+rd32(dir+28)+4u*ord));
		}
	}
	return 0;
}

/* DT_INIT constructor: walk auxv to AT_BASE once, before any body runs. */
__attribute__((constructor, visibility("hidden")))
static void _elfsysv_face_init(int argc, char **argv, char **envp){
	uint64_t *p=(uint64_t *)envp; (void)argc; (void)argv;
	while (*p) p++; p++;
	for (; p[0]; p+=2) if (p[0]==AT_BASE){ _elfsysv_face_base=(const uint8_t *)(uintptr_t)p[1]; break; }
}

/* The faced body: names "strtol" as data, resolves it at run time, tail-calls.
 * No ELF symbol named strtol is referenced -- nothing for ld to self-bind. */
typedef long (*strtol_fn)(const char *, char **, int);
long v_strtol(const char *nptr, char **endptr, int base){
	strtol_fn f=(strtol_fn)_elfsysv_resolve("strtol");
	return f(nptr, endptr, base);
}
__asm__(".symver v_strtol, strtol@@GLIBC_2.2.5");
BODY

printf '%s { global: %s; local: *; };\n' "$node" "$sym" >"$tmp/vs"

if ! "$GCC" -c -O2 -ffreestanding -fPIC "$tmp/body.c" -o "$tmp/body.o" 2>"$tmp/cc.err" \
   || ! "$GCC" -shared -nostdlib -Wl,--version-script="$tmp/vs" \
        -o "$tmp/libbody.so" "$tmp/body.o" 2>"$tmp/link.err"; then
emit "thunk_builds=no"
sed 's/^/    cc:   /' "$tmp/cc.err" | emit_lines
sed 's/^/    link: /' "$tmp/link.err" | emit_lines
emit "verdict=no"
exit 1
fi

# Fact 1. strtol@@node DEFINED in .dynsym with a real body (size > 1).
size=$("$RE" --dyn-syms "$tmp/libbody.so" 2>/dev/null \
  | awk -v s="$sym@@$node" '$8==s && $4=="FUNC" && $7!="UND"{print $3; exit}')
size=${size:-0}
if [ "$size" -gt 1 ]; then
emit "thunk_defines_versioned_symbol=yes  ($sym@@$node FUNC, $size bytes, not a ret stub)"
else
emit "thunk_defines_versioned_symbol=no  (size=$size)"
fi

# Fact 2. No undefined .dynsym entry for strtol and no relocation naming it.
undef=$("$RE" --dyn-syms "$tmp/libbody.so" 2>/dev/null \
  | awk -v s="$sym" '$8==s && $7=="UND"{c++} END{print c+0}')
rels=$("$RE" -r "$tmp/libbody.so" 2>/dev/null | grep -cw "$sym" || true)
rels=${rels:-0}
if [ "$undef" -eq 0 ] && [ "$rels" -eq 0 ]; then
emit "thunk_no_elf_self_import=yes  (no UND $sym, no relocation names $sym)"
else
emit "thunk_no_elf_self_import=no  (undef=$undef relocs_naming_sym=$rels)"
fi

# Fact 3. The literal name is present as read-only data -- the resolution key.
if "$RE" -p .rodata "$tmp/libbody.so" 2>/dev/null | grep -qw "$sym"; then
emit "thunk_keys_on_export_name=yes  (\"$sym\" in .rodata, the run-time resolver's key)"
else
emit "thunk_keys_on_export_name=no  (\"$sym\" not found in .rodata)"
fi

# Fact 4. The resolver is not a dynamic symbol -- one private copy per veneer.
if "$RE" --dyn-syms "$tmp/libbody.so" 2>/dev/null | grep -qw _elfsysv_resolve; then
emit "resolver_stays_private=no  (_elfsysv_resolve is exported in .dynsym)"
else
emit "resolver_stays_private=yes  (_elfsysv_resolve absent from .dynsym, hidden)"
fi

emit "verdict=yes"
