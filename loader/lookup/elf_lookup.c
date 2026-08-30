/* WP-35: scope ordering, interposition, and the master lookup.
 *
 * elf_object_find answers a name within one object. This file decides which
 * objects are asked and in what order -- the scope -- and applies the binding
 * rule across the whole search list. The scope order is the entire content of
 * the interposition policy: an object earlier in the list is asked first, so a
 * definition there shadows the same name later, and placing the LD_PRELOAD
 * objects right after the main object is what makes them interpose.
 */
#include "elf_lookup.h"

#include <string.h>

void elf_scope_init(elf_scope *s)
{
	if (s)
		s->count = 0;
}

int elf_scope_add(elf_scope *s, const elf_lookup_object *o)
{
	if (!s || !o)
		return -1;
	if (s->count >= ELF_SCOPE_MAX)
		return -1;
	s->obj[s->count++] = o;
	return 0;
}

int elf_scope_build_global(elf_scope *s,
                           const elf_lookup_object *main_obj,
                           const elf_lookup_object *const *preload, unsigned npre,
                           const elf_lookup_object *const *deps, unsigned ndeps)
{
	unsigned i;
	if (!s)
		return -1;
	elf_scope_init(s);
	if (main_obj && elf_scope_add(s, main_obj) != 0)
		return -1;
	for (i = 0; i < npre; i++)
		if (elf_scope_add(s, preload[i]) != 0)
			return -1;
	for (i = 0; i < ndeps; i++)
		if (elf_scope_add(s, deps[i]) != 0)
			return -1;
	return 0;
}

/* ---- the master lookup ------------------------------------------------- */

/* State carried across the scope walk: the best answer so far. A global (or
 * GNU-unique) definition ends the walk; a weak one is kept only as a fallback,
 * and only the first weak, so it never overrides a global reached later. */
struct acc {
	int found_global;
	elf_lookup_result weak;   /* first weak seen, if any */
	int have_weak;
};

/* The runtime value of a definition: its address, unless the symbol is
 * absolute, in which case st_value is used as-is. */
static uint64_t sym_value(const elf_lookup_object *o, const Elf64_Sym *s)
{
	if (s->st_shndx == SHN_ABS)
		return s->st_value;
	return o->bias + s->st_value;
}

/* Search one scope. Returns 1 when a global/unique definition was found and the
 * whole lookup is settled; 0 to keep searching later scopes. Fills out on the
 * settling hit; records the first weak into a->weak otherwise. */
static int search_scope(const elf_scope *sc, const char *name,
                        uint32_t gnu_h, uint32_t sysv_h,
                        const elf_version_matcher *vm,
                        struct acc *a, elf_lookup_result *out)
{
	unsigned i;
	if (!sc)
		return 0;
	for (i = 0; i < sc->count; i++) {
		const elf_lookup_object *o = sc->obj[i];
		unsigned char bind = STB_GLOBAL;
		int def = 1;
		uint32_t idx;
		if (!o)
			continue;
		idx = elf_object_find(o, name, gnu_h, sysv_h, vm, &bind, &def);
		if (idx == STN_UNDEF)
			continue;
		if (bind == STB_WEAK) {
			if (!a->have_weak) {
				a->have_weak = 1;
				a->weak.found = 1;
				a->weak.obj = o;
				a->weak.symidx = idx;
				a->weak.sym = &o->symtab[idx];
				a->weak.value = sym_value(o, &o->symtab[idx]);
				a->weak.bind = bind;
				a->weak.is_default = def;
			}
			continue;   /* a weak does not end the search */
		}
		/* global or GNU-unique: settles it */
		out->found = 1;
		out->obj = o;
		out->symidx = idx;
		out->sym = &o->symtab[idx];
		out->value = sym_value(o, &o->symtab[idx]);
		out->bind = bind;
		out->is_default = def;
		a->found_global = 1;
		return 1;
	}
	return 0;
}

int elf_lookup(const elf_scope *global, const elf_scope *local,
               const elf_scope *glob_dl, const char *name,
               const elf_version_matcher *vm, elf_lookup_result *out)
{
	struct acc a;
	uint32_t gnu_h, sysv_h;

	if (!out || !name)
		return -1;
	memset(out, 0, sizeof *out);
	memset(&a, 0, sizeof a);

	/* One hash of the name, reused for every object in every scope. */
	gnu_h = elf_gnu_hash(name);
	sysv_h = elf_sysv_hash(name);

	/* The three stages, in order: the global scope, then the reference's own
	 * dependency list, then a trailing RTLD_GLOBAL dlopen list. The first
	 * global definition in this concatenation ends the search. */
	if (search_scope(global, name, gnu_h, sysv_h, vm, &a, out))
		return 0;
	if (search_scope(local, name, gnu_h, sysv_h, vm, &a, out))
		return 0;
	if (search_scope(glob_dl, name, gnu_h, sysv_h, vm, &a, out))
		return 0;

	/* No global anywhere: the first weak, if one was seen, is the answer. */
	if (a.have_weak) {
		*out = a.weak;
		return 0;
	}
	/* Nothing defines it: found stays 0, which a caller reads as unresolved. */
	return 0;
}
