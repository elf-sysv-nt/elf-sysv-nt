/* WP-38: what the loader tells a program about an address and about itself.
 *
 * dladdr answers "what object and what symbol is this address in", which a
 * backtrace printer and a crash handler both need. dl_iterate_phdr answers
 * "what objects are loaded, and where are their program headers", which is how
 * an unwinder finds .eh_frame -- PT_GNU_EH_FRAME is a program header, so the
 * unwinder does not need the loader to know anything about exception handling,
 * only to hand out the phdrs. That is the whole of the coupling, and it is why
 * an object that arrived through dlopen becomes unwindable the moment it is in
 * the table: nothing has to be registered anywhere.
 *
 * The two counters dlpi_adds and dlpi_subs exist so an unwinder can cache its
 * per-object lookup and know when the cache is stale. They count loads and
 * unloads since the state came up and are never reset.
 */
#include <string.h>

#include "dl.h"

/* The object whose mapped span contains addr, or NULL. */
static dl_object *object_at(dl_state *st, uint64_t a)
{
	unsigned i;
	for (i = 0; i < st->obj_count; i++) {
		dl_object *o = &st->obj[i];
		if (!o->in_use)
			continue;
		if (a >= o->map.base && a < o->map.base + o->map.size)
			return o;
	}
	return NULL;
}

/* The defined symbol with the greatest address not above addr -- the symbol the
 * address is "in". A symbol with a size is preferred when the address falls
 * inside it, which is what keeps an address in a function's body from being
 * attributed to a zero-sized label that happens to sit closer. */
static const Elf64_Sym *nearest_symbol(const dl_object *o, uint64_t a,
                                       uint32_t *out_idx)
{
	const Elf64_Sym *best = NULL;
	uint64_t best_addr = 0;
	uint64_t j;

	if (!o->lo.symtab || !o->lo.strtab)
		return NULL;

	for (j = 1; j < o->lo.symcount; j++) {
		const Elf64_Sym *s = &o->lo.symtab[j];
		uint64_t sa;
		if (s->st_shndx == SHN_UNDEF || s->st_name == 0)
			continue;
		if (ELF64_ST_TYPE(s->st_info) == STT_TLS)
			continue;             /* a TLS value is an offset, not an address */
		sa = o->lo.bias + s->st_value;
		if (sa > a)
			continue;
		if (s->st_size && a >= sa + s->st_size)
			continue;             /* past the end of a sized symbol */
		if (!best || sa > best_addr) {
			best = s;
			best_addr = sa;
			if (out_idx)
				*out_idx = (uint32_t) j;
		}
	}
	return best;
}

int dl_addr1(dl_state *st, const void *addr, dl_info *info,
             void **extra_info, int flags)
{
	uint64_t a = (uint64_t)(uintptr_t) addr;
	dl_object *o;
	const Elf64_Sym *s;
	uint32_t idx = 0;

	if (!st || !info)
		return 0;
	memset(info, 0, sizeof *info);

	o = object_at(st, a);
	if (!o)
		return 0;              /* dladdr's inverted convention: 0 is "no" */

	info->dli_fname = o->path;
	info->dli_fbase = (void *)(uintptr_t) o->map.base;

	s = nearest_symbol(o, a, &idx);
	if (s) {
		info->dli_sname = o->lo.strtab + s->st_name;
		info->dli_saddr = (void *)(uintptr_t)(o->lo.bias + s->st_value);
	}

	if (extra_info) {
		if (flags == RTLD_DL_SYMENT)
			*extra_info = (void *) s;      /* NULL when no symbol matched */
		else if (flags == RTLD_DL_LINKMAP)
			*extra_info = &o->lm;
		else
			*extra_info = NULL;
	}
	return 1;
}

int dl_addr(dl_state *st, const void *addr, dl_info *info)
{
	return dl_addr1(st, addr, info, NULL, 0);
}

int dl_info_get(dl_state *st, void *handle, int request, void *p)
{
	dl_object *o = handle;

	if (!st)
		return -1;
	if (!o || o < st->obj || o >= st->obj + DL_MAX_OBJECTS || !o->in_use || !p) {
		dl_set_err(st, "dlinfo", "invalid handle");
		return -1;
	}

	switch (request) {
	case RTLD_DI_LINKMAP:
		*(struct link_map **) p = &o->lm;
		return 0;
	case RTLD_DI_LMID:
		*(long *) p = 0;          /* one namespace; see the header */
		return 0;
	case RTLD_DI_ORIGIN: {
		const char *slash = strrchr(o->path, '/');
		size_t n = slash ? (size_t)(slash - o->path) : 0;
		memcpy(p, o->path, n);
		((char *) p)[n] = '\0';
		return 0;
	}
	case RTLD_DI_TLS_MODID:
		*(size_t *) p = o->ro ? (size_t) o->ro->tls_modid : 0;
		return 0;
	case RTLD_DI_TLS_DATA:
		*(void **) p = NULL;      /* this thread's block; WP-37 owns it */
		return 0;
	default:
		return -1;
	}
}

int dl_iterate_phdr(dl_state *st,
                    int (*cb)(struct dl_phdr_info *, size_t, void *),
                    void *data)
{
	unsigned i;

	if (!st || !cb)
		return 0;

	for (i = 0; i < st->obj_count; i++) {
		dl_object *o = &st->obj[i];
		struct dl_phdr_info info;
		int rc;

		if (!o->in_use || !o->phdr)
			continue;

		memset(&info, 0, sizeof info);
		info.dlpi_addr = o->map.load_bias;
		/* The main object reports an empty name, as the platform's loader
		 * does; every other object reports the path it was loaded from. */
		info.dlpi_name = (o->is_startup && o->slot == 0) ? "" : o->path;
		info.dlpi_phdr = o->phdr;
		info.dlpi_phnum = o->phnum;
		info.dlpi_adds = st->adds;
		info.dlpi_subs = st->subs;
		info.dlpi_tls_modid = o->ro ? (size_t) o->ro->tls_modid : 0;
		info.dlpi_tls_data = NULL;

		rc = cb(&info, sizeof info, data);
		if (rc != 0)
			return rc;
	}
	return 0;
}
