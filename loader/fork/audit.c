/* WP-42: reducing the loader's state to one comparable record.
 *
 * "The loader crossed intact" is not a thing a child can assert about itself.
 * It is a comparison, and this is the thing compared. Every field is either an
 * address that must be the same address in the child or a hash over a structure
 * that must have the same contents, and the diff names the first field that
 * moved rather than reporting that something did.
 *
 * The hash is FNV-1a, chosen because it is four lines and has no state to cross
 * a fork. It is not a checksum against corruption and is not asked to be: the
 * two sides are the same build in the same process family, so the only
 * difference it has to detect is a real one.
 */
#include <stdio.h>
#include <string.h>

#include "fork.h"

#define FNV_OFF UINT64_C(0xcbf29ce484222325)
#define FNV_PRM UINT64_C(0x100000001b3)

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
	const unsigned char *b = (const unsigned char *)p;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= FNV_PRM;
	}
	return h;
}

static uint64_t fnv_u64(uint64_t h, uint64_t v)
{
	return fnv(h, &v, sizeof v);
}

/* A string, length-prefixed so "ab" + "c" and "a" + "bc" do not collide. NULL
 * hashes differently from the empty string, which matters for a search
 * configuration where an unset field and an empty one mean different things. */
static uint64_t fnv_str(uint64_t h, const char *s)
{
	if (s == NULL)
		return fnv_u64(h, UINT64_MAX);
	size_t n = strlen(s);
	h = fnv_u64(h, n);
	return fnv(h, s, n);
}

/* The rebase probe. Its address is the loader's own code, and it is taken
 * through a function pointer the compiler may not fold, so what is hashed is
 * where this translation unit actually sits at run time. */
static void elf_fork_here(void) { }

static uint64_t self_address(void)
{
	void (*volatile p)(void) = elf_fork_here;
	return (uint64_t)(uintptr_t)p;
}

static uint64_t hash_map(const struct link_map *head, uint32_t *len_out)
{
	uint64_t h = FNV_OFF;
	uint32_t n = 0;
	for (const struct link_map *lm = head;
	     lm != NULL && n < ELF_FORK_MAP_WALK_MAX;
	     lm = lm->l_next, n++) {
		h = fnv_u64(h, (uint64_t)(uintptr_t)lm);
		h = fnv_u64(h, (uint64_t)lm->l_addr);
		h = fnv_u64(h, (uint64_t)(uintptr_t)lm->l_ld);
		h = fnv_str(h, lm->l_name);
	}
	*len_out = n;
	return h;
}

static uint64_t hash_objects(const dl_state *dl, uint32_t *count_out)
{
	uint64_t h = FNV_OFF;
	uint32_t n = 0;
	for (unsigned i = 0; i < dl->obj_count && i < DL_MAX_OBJECTS; i++) {
		const dl_object *o = &dl->obj[i];
		if (!o->in_use)
			continue;
		n++;
		h = fnv_u64(h, o->slot);
		h = fnv_u64(h, o->seq);
		h = fnv_str(h, o->soname);
		h = fnv_str(h, o->path);
		h = fnv_u64(h, o->map.base);
		h = fnv_u64(h, o->map.load_bias);
		h = fnv_u64(h, o->map.size);
		h = fnv_u64(h, (uint64_t)(uintptr_t)o->dyn);
		h = fnv_u64(h, (uint64_t)(uintptr_t)o->image);
		h = fnv_u64(h, (uint64_t)o->refcount);
		h = fnv_u64(h, (uint64_t)o->global);
		h = fnv_u64(h, (uint64_t)o->initialized);
		h = fnv_u64(h, o->dep_count);
		for (unsigned d = 0; d < o->dep_count && d < DL_MAX_DEPS; d++)
			h = fnv_u64(h, o->dep[d]);
	}
	*count_out = n;
	return h;
}

/* The search configuration is the thing a child most plausibly loses without
 * noticing: the strings it points at are the parent's environment, and an
 * environment the child rebuilt would still resolve most names. The pointers
 * are hashed alongside the contents so a copy that moved is caught too. */
static uint64_t hash_search(const elf_graph_config *c)
{
	uint64_t h = FNV_OFF;
	h = fnv_str(h, c->ld_library_path);
	h = fnv_u64(h, c->default_count);
	for (size_t i = 0; i < c->default_count && i < 64; i++)
		h = fnv_str(h, c->default_paths ? c->default_paths[i] : NULL);
	h = fnv_u64(h, (uint64_t)(uintptr_t)c->cache);
	h = fnv_u64(h, c->want_flags);
	h = fnv_str(h, c->origin_override);
	h = fnv_str(h, c->lib_token);
	h = fnv_str(h, c->platform_token);
	h = fnv_u64(h, c->max_objects);
	return h;
}

/* The DTV as this thread holds it: its length, its generation word, and every
 * slot with the kind that says whether teardown frees it. A DTV that crossed as
 * a copy but whose static slots point into a TCB that moved is exactly the
 * silent failure this is here to make loud. */
static uint64_t hash_dtv(const elfsysv_tcb_t *tcb, uint32_t *len_out)
{
	*len_out = 0;
	if (tcb == NULL || tcb->head == NULL || tcb->head->dtv == NULL)
		return FNV_OFF;

	const elf_tls_dtv *dtv = (const elf_tls_dtv *)tcb->head->dtv;
	uint64_t len = dtv[-1].counter;
	if (len > ELF_TLS_MAX_MOD + 1)
		len = ELF_TLS_MAX_MOD + 1;

	uint64_t h = FNV_OFF;
	h = fnv_u64(h, len);
	h = fnv_u64(h, dtv[0].counter);
	for (uint64_t i = 1; i <= len; i++) {
		h = fnv_u64(h, (uint64_t)(uintptr_t)dtv[i].pointer.val);
		h = fnv_u64(h, (uint64_t)dtv[i].pointer.is_static);
	}
	*len_out = (uint32_t)len;
	return h;
}

static uint64_t hash_regions(const elf_fork_state *fs)
{
	uint64_t h = FNV_OFF;
	for (uint32_t i = 0; i < fs->region_count; i++) {
		const elf_fork_region *r = &fs->region[i];
		h = fnv_u64(h, r->base);
		h = fnv_u64(h, r->size);
		h = fnv_u64(h, r->kind);
		h = fnv_u64(h, r->prot);
		h = fnv_str(h, r->what);
	}
	return h;
}

void elf_fork_audit_take(const elf_fork_state *fs, elf_fork_audit *a)
{
	memset(a, 0, sizeof(*a));
	a->magic = ELF_FORK_AUDIT_MAGIC;
	a->self_addr = self_address();

	a->rdebug_addr = (uint64_t)(uintptr_t)&_r_debug;
	a->rdebug_map = (uint64_t)(uintptr_t)_r_debug.r_map;
	a->rdebug_version = (uint32_t)_r_debug.r_version;
	a->rdebug_state = (uint32_t)_r_debug.r_state;
	a->map_hash = hash_map(_r_debug.r_map, &a->map_len);

	if (fs->dl != NULL) {
		a->obj_hash = hash_objects(fs->dl, &a->obj_count);
		a->search_hash = hash_search(&fs->dl->search);
	}

	if (fs->tls != NULL) {
		a->tls_static_size = fs->tls->static_size;
		a->tls_generation = fs->tls->generation;
		a->tls_nmod = (uint32_t)fs->tls->nmod;
	}

	a->dtv_hash = hash_dtv(fs->tcb, &a->dtv_len);
	a->tp = fs->tcb != NULL ? (uint64_t)(uintptr_t)fs->tcb->tp : 0;

	a->region_count = fs->region_count;
	a->region_hash = hash_regions(fs);
}

/* The order is the explanation. A loader image that moved is the Cygwin rebase
 * failure, and it makes every address below it differ too, so it is reported
 * first and by name; after it, the structures are reported outermost first, so
 * "the object table changed" precedes "a DTV slot changed" when both did. */
static int differs(char *why, size_t cap, const char *field,
                   uint64_t b, uint64_t a)
{
	if (why != NULL && cap > 0)
		snprintf(why, cap, "%s: parent 0x%llx, child 0x%llx", field,
		         (unsigned long long)b, (unsigned long long)a);
	return -1;
}

int elf_fork_audit_diff(const elf_fork_audit *before, const elf_fork_audit *after,
                        char *why, size_t why_cap)
{
	if (before->magic != ELF_FORK_AUDIT_MAGIC ||
	    after->magic != ELF_FORK_AUDIT_MAGIC)
		return differs(why, why_cap, "audit magic",
		               before->magic, after->magic);

	if (before->self_addr != after->self_addr)
		return differs(why, why_cap,
		               "the loader image was rebased across the fork",
		               before->self_addr, after->self_addr);

	struct { const char *name; uint64_t b, a; } f[] = {
		{ "rdebug address",   before->rdebug_addr,   after->rdebug_addr },
		{ "rdebug map head",  before->rdebug_map,    after->rdebug_map },
		{ "rdebug version",   before->rdebug_version, after->rdebug_version },
		{ "rdebug state",     before->rdebug_state,  after->rdebug_state },
		{ "link map length",  before->map_len,       after->map_len },
		{ "link map",         before->map_hash,      after->map_hash },
		{ "object count",     before->obj_count,     after->obj_count },
		{ "object table",     before->obj_hash,      after->obj_hash },
		{ "search paths",     before->search_hash,   after->search_hash },
		{ "static TLS size",  before->tls_static_size, after->tls_static_size },
		{ "TLS generation",   before->tls_generation, after->tls_generation },
		{ "TLS module count", before->tls_nmod,      after->tls_nmod },
		{ "thread pointer",   before->tp,            after->tp },
		{ "DTV length",       before->dtv_len,       after->dtv_len },
		{ "DTV",              before->dtv_hash,      after->dtv_hash },
		{ "region count",     before->region_count,  after->region_count },
		{ "regions",          before->region_hash,   after->region_hash },
	};

	for (size_t i = 0; i < sizeof f / sizeof f[0]; i++)
		if (f[i].b != f[i].a)
			return differs(why, why_cap, f[i].name, f[i].b, f[i].a);

	if (why != NULL && why_cap > 0)
		why[0] = '\0';
	return 0;
}
