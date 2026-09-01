/*
 * WP-56: which crossing an image needs. See exec_kind.h.
 *
 * The rule is Linux's, read off the same fields the kernel reads. The
 * kernel's load_elf_binary keys the interpreter path on a PT_INTERP
 * program header and nothing else: an image with one is entered through
 * its interpreter, an image without one is entered at e_entry. So the
 * presence of PT_INTERP -- elf_parse()'s has_interp -- is the whole
 * dynamic/static split, and e_type only sorts what is left.
 *
 * has_interp already stands for a PT_INTERP whose bytes elf_parse()
 * bounds-checked, so this reads a flag rather than re-walking the phdrs.
 */
#include "exec_kind.h"
#include "../elf/elf_types.h"

exec_kind exec_kind_of(const elf_parsed *p)
{
/* An interpreter is named: the loader stands in for it, whatever the
 * image's e_type. bzip2 is ET_EXEC with an interp; a PIE is ET_DYN
 * with one. Both cross the same way; the load bias is the driver's. */
if (p->has_interp)
return EXEC_KIND_DYNAMIC;

/* No interpreter. Only a fixed-address executable runs with no loader.
 * ET_DYN here is a shared object -- loadable through the dl surface,
 * not runnable as a program on this route -- and ET_REL / ET_CORE /
 * ET_NONE were never programs. */
if (p->e_type == ET_EXEC)
return EXEC_KIND_STATIC;

return EXEC_KIND_UNSUPPORTED;
}

const char *exec_kind_name(exec_kind k)
{
switch (k) {
case EXEC_KIND_STATIC:      return "static";
case EXEC_KIND_DYNAMIC:     return "dynamic";
case EXEC_KIND_UNSUPPORTED: return "unsupported";
}
return "unknown";
}
