/* WP-38: dlopen, dlclose, dlsym, dlvsym, dlerror and the object table.
 *
 * The invariant the whole package is built around is that a load and its
 * matching unload are exact inverses. A dlopen takes a table slot, a file
 * image, a mapping, a relocation-scope slot, a link-map node and possibly a
 * TLS module id; the dlclose that drops the last reference gives all six back,
 * in reverse. Ten thousand cycles of that leave the loader in the state it
 * started in, which is the done-when and is checked directly rather than
 * inferred from a memory figure that could hide a slow leak.
 *
 * Search order is WP-35's, not a second implementation of it: dlsym over a
 * handle searches the object and its dependency closure, RTLD_DEFAULT searches
 * the global scope, and RTLD_NEXT searches what follows the caller in load
 * order. This file builds the scopes; elf_lookup decides.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dl.h"

/* ---- the host seam ----------------------------------------------------- */

static int stdio_read(void *ctx, const char *path,
                      unsigned char **buf, size_t *size)
{
	FILE *f;
	long n;
	unsigned char *p;

	(void) ctx;
	f = fopen(path, "rb");
	if (!f)
		return -1;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0) {
		fclose(f);
		return -1;
	}
	rewind(f);
	p = malloc((size_t) n ? (size_t) n : 1);
	if (!p) {
		fclose(f);
		return -1;
	}
	if (fread(p, 1, (size_t) n, f) != (size_t) n) {
		free(p);
		fclose(f);
		return -1;
	}
	fclose(f);
	*buf = p;
	*size = (size_t) n;
	return 0;
}

static void stdio_release(void *ctx, unsigned char *buf, size_t size)
{
	(void) ctx;
	(void) size;
	free(buf);
}

const dl_host *dl_host_stdio(void)
{
	static const dl_host h = { stdio_read, stdio_release, NULL };
	return &h;
}

/* ---- errors ------------------------------------------------------------ */

void dl_set_err(dl_state *st, const char *what, const char *detail)
{
	if (!st)
		return;
	if (detail)
		snprintf(st->err, sizeof st->err, "%s: %s", what, detail);
	else
		snprintf(st->err, sizeof st->err, "%s", what);
	st->err_pending = 1;
}

const char *dl_error(dl_state *st)
{
	if (!st || !st->err_pending)
		return NULL;
	st->err_pending = 0;      /* one report per error, as the surface promises */
	return st->err;
}

/* ---- the table --------------------------------------------------------- */

void dl_state_init(dl_state *st, const dl_host *host,
                   int argc, char **argv, char **envp)
{
	if (!st)
		return;
	memset(st, 0, sizeof *st);
	st->host = host ? *host : *dl_host_stdio();
	st->argc = argc;
	st->argv = argv;
	st->envp = envp;
	elf_reloc_scope_init(&st->reloc);
}

static dl_object *table_alloc(dl_state *st)
{
	unsigned i;
	for (i = 0; i < DL_MAX_OBJECTS; i++) {
		if (!st->obj[i].in_use) {
			dl_object *o = &st->obj[i];
			memset(o, 0, sizeof *o);
			o->in_use = 1;
			o->slot = i;
			o->seq = st->next_seq++;
			if (i + 1 > st->obj_count)
				st->obj_count = i + 1;
			return o;
		}
	}
	return NULL;
}

/* The identity of an object is its DT_SONAME when it carries one, and the
 * basename of the path it was found at otherwise -- the same identity WP-33's
 * graph uses, so a name loaded twice by two spellings is one object. */
static void set_identity(dl_object *o, const char *path)
{
	const char *base;
	snprintf(o->path, sizeof o->path, "%s", path ? path : "");

	if (o->parsed.has_soname && o->ro && o->ro->strtab &&
	    o->parsed.soname < o->parsed.strsz) {
		snprintf(o->soname, sizeof o->soname, "%s",
		         o->ro->strtab + o->parsed.soname);
		if (o->soname[0])
			return;
	}
	base = strrchr(o->path, '/');
	base = base ? base + 1 : o->path;
	snprintf(o->soname, sizeof o->soname, "%s", base);
}

/* The mapped program headers and dynamic array, which dl_iterate_phdr and the
 * initializer walk read. Both live inside a PT_LOAD, so both are the link
 * address plus the bias. */
static void set_views(dl_object *o)
{
	uint64_t bias = o->map.load_bias;
	uint64_t phaddr = 0;
	unsigned i;

	/* The program headers are at a file offset; find the PT_LOAD that carries
	 * that offset and translate through it, which is how AT_PHDR is derived
	 * when no PT_PHDR is present. */
	for (i = 0; i < o->parsed.load_count; i++) {
		const elf_load_seg *s = &o->parsed.load[i];
		if (o->parsed.phoff >= s->off && o->parsed.phoff < s->off + s->filesz) {
			phaddr = bias + s->vaddr + (o->parsed.phoff - s->off);
			break;
		}
	}
	o->phdr = (const Elf64_Phdr *)(uintptr_t) phaddr;
	o->phnum = phaddr ? o->parsed.phnum : 0;

	o->dyn = NULL;
	for (i = 0; i < o->parsed.load_count; i++) {
		const elf_load_seg *s = &o->parsed.load[i];
		if (o->parsed.has_dynamic && o->parsed.dyn_off >= s->off &&
		    o->parsed.dyn_off < s->off + s->filesz) {
			o->dyn = (const Elf64_Dyn *)(uintptr_t)
			         (bias + s->vaddr + (o->parsed.dyn_off - s->off));
			break;
		}
	}
}

/* The symbol view WP-35 reads, lifted from the relocation object's dynamic
 * view so the two never disagree about where a table is. */
static void set_lookup(dl_object *o)
{
	memset(&o->lo, 0, sizeof o->lo);
	if (!o->ro)
		return;
	o->lo.name = o->soname;
	o->lo.bias = o->ro->bias;
	o->lo.strtab = o->ro->strtab;
	o->lo.strsz = o->ro->strsz;
	o->lo.symtab = o->ro->symtab;
	o->lo.symcount = o->ro->symcount;
	o->lo.sysv_hash = o->ro->sysv_hash;
	o->lo.gnu_hash = o->ro->gnu_hash;

	/* The version tables, so dlvsym and WP-36's matcher see the same view. */
	memset(&o->vinfo, 0, sizeof o->vinfo);
	elf_version_info_from(&o->vinfo, &o->parsed, &o->map);
	o->lo.versym = o->vinfo.versym;
}

dl_object *dl_adopt(dl_state *st, const char *path, elf_reloc_object *ro,
                    const elf_parsed *p, elf_mapping *m, unsigned char *image,
                    size_t image_size)
{
	dl_object *o;

	if (!st || !ro || !p || !m)
		return NULL;
	o = table_alloc(st);
	if (!o)
		return NULL;

	o->parsed = *p;
	o->map = *m;
	o->ro = ro;
	o->image = image;
	o->image_size = image_size;
	o->is_startup = 1;
	o->global = 1;
	o->refcount = 0;

	set_identity(o, path);
	set_views(o);
	set_lookup(o);

	o->lm.l_addr = o->map.load_bias;
	o->lm.l_name = o->path;
	o->lm.l_ld = (Elf64_Dyn *) o->dyn;
	return o;
}

int dl_add_dep(dl_object *o, unsigned dep_slot)
{
	unsigned i;
	if (!o || dep_slot >= DL_MAX_OBJECTS)
		return -1;
	for (i = 0; i < o->dep_count; i++)
		if (o->dep[i] == dep_slot)
			return 0;
	if (o->dep_count >= DL_MAX_DEPS)
		return -1;
	o->dep[o->dep_count++] = dep_slot;
	return 0;
}

static dl_object *find_loaded(dl_state *st, const char *path)
{
	const char *base = strrchr(path, '/');
	unsigned i;

	base = base ? base + 1 : path;
	for (i = 0; i < st->obj_count; i++) {
		dl_object *o = &st->obj[i];
		if (!o->in_use)
			continue;
		if (strcmp(o->path, path) == 0 || strcmp(o->soname, base) == 0)
			return o;
	}
	return NULL;
}

/* ---- load and unload --------------------------------------------------- */

/* Undo a partial load in the reverse of the order it was built, so a failure
 * halfway leaves the table exactly as the call found it. */
static void unload(dl_state *st, dl_object *o)
{
	if (!o || !o->in_use)
		return;

	if (o->ro) {
		elf_reloc_drop(&st->reloc, o->ro);
		o->ro = NULL;
	}
	elf_unmap(&o->map);
	if (o->image) {
		st->host.release(st->host.ctx, o->image, o->image_size);
		o->image = NULL;
	}
	memset(o, 0, sizeof *o);
	/* The slot itself is freed by the memset clearing in_use; obj_count is a
	 * high-water mark over the table and deliberately does not shrink, so a
	 * surviving object's slot number never moves under it. */
}

/* The floor a dlopen'd object is placed at: clear of where a program and its
 * startup libraries are placed, and clear of the host's own low allocations. */
#define DL_PLACEMENT_FLOOR 0x40000000ULL

/* A base above every mapping the loader currently holds. With the table empty
 * this is the floor, so a plugin loaded and unloaded repeatedly is placed at
 * the same address every time and the address space does not creep. */
static uint64_t choose_base(const dl_state *st)
{
	uint64_t granule = elf_map_host_granule();
	uint64_t base = DL_PLACEMENT_FLOOR;
	unsigned i;

	for (i = 0; i < st->obj_count; i++) {
		const dl_object *o = &st->obj[i];
		uint64_t end;
		if (!o->in_use || !o->map.size)
			continue;
		end = o->map.base + o->map.size;
		if (end > base)
			base = end;
	}
	if (granule > 1)
		base = (base + granule - 1) & ~(granule - 1);
	return base;
}

/* Take one file into a fresh table slot: read it, parse it, place it, and
 * register it in the shared relocation scope. Nothing is relocated here and no
 * initializer runs; the caller does both once the whole group is in, so that a
 * dependency and its dependent are relocated against each other in one pass.
 * Returns NULL with the error set, having released whatever it had taken. */
static dl_object *load_one(dl_state *st, const char *path, int flags)
{
	dl_object *o;
	elf_reloc_object *ro;
	elf_reloc_diag rdiag;
	elf_map_diag mdiag;
	elf_diag pdiag;
	unsigned char *image = NULL;
	size_t image_size = 0;

	if (st->host.read(st->host.ctx, path, &image, &image_size) != 0) {
		dl_set_err(st, "dlopen", "cannot open shared object file");
		return NULL;
	}

	o = table_alloc(st);
	if (!o) {
		st->host.release(st->host.ctx, image, image_size);
		dl_set_err(st, "dlopen", "the object table is full");
		return NULL;
	}
	o->image = image;
	o->image_size = image_size;

	if (elf_parse(image, image_size, &o->parsed, &pdiag) != elf_ok) {
		dl_set_err(st, "dlopen", pdiag.msg);
		unload(st, o);
		return NULL;
	}

	/* The mapper places an ET_DYN where it is told and honours an ET_EXEC at
	 * its own addresses. Choosing the where is the loader's, so a plugin lands
	 * clear of everything already mapped; a refusal is retried a few granules
	 * along, because the host may have something of its own at the address.
	 * The choice is computed from the table each time rather than from a
	 * running cursor, so ten thousand load-unload cycles reuse one region
	 * instead of walking up the address space. */
	{
		uint64_t granule = elf_map_host_granule();
		uint64_t base = choose_base(st);
		unsigned attempt;
		elf_map_err rc = elf_map_err_reserve;

		for (attempt = 0; attempt < 64; attempt++) {
			rc = elf_map(image, image_size, &o->parsed, base, &o->map, &mdiag);
			if (rc != elf_map_err_reserve)
				break;
			base += granule;
		}
		if (rc != elf_map_ok) {
			dl_set_err(st, "dlopen", mdiag.msg);
			unload(st, o);
			return NULL;
		}
	}

	if (st->reloc.count >= ELF_RELOC_MAX_OBJ) {
		dl_set_err(st, "dlopen", "the relocation scope is full");
		unload(st, o);
		return NULL;
	}
	ro = &st->reloc.obj[st->reloc.count];
	snprintf(o->path, sizeof o->path, "%s", path);
	if (elf_reloc_add(&st->reloc, &o->map, &o->parsed, o->path, &rdiag)
	    != elf_reloc_ok) {
		dl_set_err(st, "dlopen", rdiag.msg);
		unload(st, o);
		return NULL;
	}
	o->ro = ro;
	o->ro->late = 1;              /* after startup: not in the static TLS block */
	if (flags & RTLD_NOW)
		o->ro->bind_now = 1;

	set_identity(o, path);
	set_views(o);
	set_lookup(o);

	o->refcount = 1;
	o->lm.l_addr = o->map.load_bias;
	o->lm.l_name = o->path;
	o->lm.l_ld = (Elf64_Dyn *) o->dyn;
	return o;
}

void *dl_open(dl_state *st, const char *path, int flags)
{
	dl_object *o = NULL;
	elf_graph g;
	elf_reloc_diag rdiag;
	unsigned slot_of[ELF_SCOPE_MAX];
	unsigned fresh[DL_MAX_OBJECTS];
	unsigned order[DL_MAX_OBJECTS];
	unsigned nfresh = 0, n, i;

	if (!st) return NULL;
	if (!path || !*path) {
		dl_set_err(st, "dlopen", "no filename");
		return NULL;
	}

	/* An object already loaded is the same object: one more reference, no
	 * second mapping, and no initializer run a second time. */
	o = find_loaded(st, path);
	if (o) {
		o->refcount++;
		if (flags & RTLD_GLOBAL)
			o->global = 1;
		if (flags & RTLD_NODELETE)
			o->nodelete = 1;
		return o;
	}
	if (flags & RTLD_NOLOAD)
		return NULL;   /* not loaded, and the caller asked not to load it */

	/* WP-33 resolves the closure: the object, everything its DT_NEEDED names
	 * reach, and the file each name resolved to, in load order. */
	memset(&g, 0, sizeof g);
	if (elf_graph_build(path, &st->search, &g) != 0 || g.error) {
		dl_set_err(st, "dlopen", g.errmsg[0] ? g.errmsg : "cannot walk the object graph");
		elf_graph_free(&g);
		return NULL;
	}
	if (g.missing_count > 0) {
		for (i = 0; i < g.count; i++)
			if (!g.obj[i].found) {
				dl_set_err(st, "dlopen", g.obj[i].name);
				break;
			}
		elf_graph_free(&g);
		return NULL;
	}
	if (g.count > ELF_SCOPE_MAX) {
		dl_set_err(st, "dlopen", "the dependency closure is too large");
		elf_graph_free(&g);
		return NULL;
	}

	/* Dependencies before dependents. The graph is breadth-first from the
	 * root, so walking it backwards puts every leaf in before anything that
	 * needs it, and an object already loaded is joined rather than reloaded. */
	for (i = g.count; i-- > 0; ) {
		dl_object *e = find_loaded(st, g.obj[i].path);
		if (!e) {
			e = load_one(st, g.obj[i].path, flags);
			if (!e)
				goto fail;
			fresh[nfresh++] = e->slot;
		} else if (i > 0) {
			e->refcount++;    /* a dependency this load now holds too */
		}
		slot_of[i] = e->slot;
	}

	/* The edges, taken from the graph: each node names the object that first
	 * introduced it. They are what initialization order is computed over and
	 * what a handle's own search scope is built from. */
	for (i = 0; i < g.count; i++)
		if (g.obj[i].parent >= 0)
			dl_add_dep(&st->obj[slot_of[(unsigned) g.obj[i].parent]],
			           slot_of[i]);

	/* Relocate. The scope holds everything loaded, so the new objects resolve
	 * against the world and against each other; the incremental mark keeps the
	 * world from being relocated a second time. */
	if (elf_reloc_apply(&st->reloc, &rdiag) != elf_reloc_ok) {
		dl_set_err(st, "dlopen", rdiag.msg);
		goto fail;
	}

	o = &st->obj[slot_of[0]];
	o->global = (flags & RTLD_GLOBAL) ? 1 : 0;
	o->nodelete = (flags & RTLD_NODELETE) ? 1 : 0;

	/* Announce each new object to a debugger, in load order. */
	if (st->rdebug_wired && nfresh > 0) {
		rdebug_map_change_begin(RT_ADD);
		for (i = nfresh; i-- > 0; )
			rdebug_map_add(&st->obj[fresh[i]].lm);
		rdebug_map_change_end();
	}
	st->adds += nfresh;

	/* Initializers over what is new, dependencies first. */
	n = dl_init_order(st, fresh, nfresh, order, DL_MAX_OBJECTS);
	dl_run_init(st, order, n);

	elf_graph_free(&g);
	return o;

fail:
	while (nfresh > 0)
		unload(st, &st->obj[fresh[--nfresh]]);
	elf_graph_free(&g);
	return NULL;
}

int dl_close(dl_state *st, void *handle)
{
	dl_object *o = handle;

	if (!st) return -1;
	if (!o || !o->in_use || o < st->obj || o >= st->obj + DL_MAX_OBJECTS) {
		dl_set_err(st, "dlclose", "invalid handle");
		return -1;
	}

	if (o->refcount > 0)
		o->refcount--;
	if (o->refcount > 0 || o->is_startup || o->nodelete)
		return 0;

	dl_run_fini(st, o);

	if (st->rdebug_wired) {
		rdebug_map_change_begin(RT_DELETE);
		rdebug_map_remove(&o->lm);
		rdebug_map_change_end();
	}

	/* The load took a reference on each dependency it brought in or joined;
	 * releasing the object releases those too, which is what makes a plugin
	 * and its private libraries go away together. The list is copied first,
	 * because unload clears it. */
	{
		unsigned dep[DL_MAX_DEPS];
		unsigned ndep = o->dep_count, i;
		memcpy(dep, o->dep, ndep * sizeof dep[0]);

		unload(st, o);
		st->subs++;

		for (i = 0; i < ndep; i++)
			if (dep[i] < DL_MAX_OBJECTS && st->obj[dep[i]].in_use)
				dl_close(st, &st->obj[dep[i]]);
	}
	return 0;
}

/* ---- symbol resolution ------------------------------------------------- */

/* The global scope in load order: every object marked RTLD_GLOBAL, which is
 * every startup object plus the dlopens that asked for it. */
static void build_global(const dl_state *st, elf_scope *s)
{
	unsigned i;
	elf_scope_init(s);
	for (i = 0; i < st->obj_count; i++) {
		const dl_object *o = &st->obj[i];
		if (o->in_use && o->global && o->lo.symtab)
			elf_scope_add(s, &o->lo);
	}
}

/* One object's own scope: itself, then its dependency closure in the order the
 * edges were recorded. This is what a handle searches. */
static void build_local(const dl_state *st, const dl_object *root, elf_scope *s)
{
	unsigned queue[DL_MAX_OBJECTS];
	unsigned char seen[DL_MAX_OBJECTS];
	unsigned head = 0, tail = 0, i;

	elf_scope_init(s);
	memset(seen, 0, sizeof seen);
	queue[tail++] = root->slot;
	seen[root->slot] = 1;

	while (head < tail) {
		const dl_object *o = &st->obj[queue[head++]];
		if (!o->in_use)
			continue;
		if (o->lo.symtab)
			elf_scope_add(s, &o->lo);
		for (i = 0; i < o->dep_count; i++) {
			unsigned d = o->dep[i];
			if (d < DL_MAX_OBJECTS && !seen[d] && tail < DL_MAX_OBJECTS) {
				seen[d] = 1;
				queue[tail++] = d;
			}
		}
	}
}

/* Everything after `from` in load order, which is where RTLD_NEXT starts. */
static void build_next(const dl_state *st, const dl_object *from, elf_scope *s)
{
	unsigned i;
	elf_scope_init(s);
	for (i = 0; i < st->obj_count; i++) {
		const dl_object *o = &st->obj[i];
		if (o->in_use && o->lo.symtab && from && o->seq > from->seq)
			elf_scope_add(s, &o->lo);
	}
}

static void *lookup_in(dl_state *st, const elf_scope *s, const char *name,
                       const elf_version_matcher *vm, const char *what)
{
	elf_lookup_result r;

	if (elf_lookup(s, NULL, NULL, name, vm, &r) != 0 || !r.found) {
		dl_set_err(st, what, "undefined symbol");
		return NULL;
	}
	return (void *)(uintptr_t) r.value;
}

void *dl_sym_from(dl_state *st, void *handle, const char *name,
                  const dl_object *from)
{
	static elf_scope s;

	if (!st) return NULL;
	if (!name || !*name) {
		dl_set_err(st, "dlsym", "no symbol name");
		return NULL;
	}

	if (handle == RTLD_DEFAULT) {
		build_global(st, &s);
	} else if (handle == RTLD_NEXT) {
		if (!from) {
			dl_set_err(st, "dlsym", "RTLD_NEXT used outside a loaded object");
			return NULL;
		}
		build_next(st, from, &s);
	} else {
		const dl_object *o = handle;
		if (o < st->obj || o >= st->obj + DL_MAX_OBJECTS || !o->in_use) {
			dl_set_err(st, "dlsym", "invalid handle");
			return NULL;
		}
		build_local(st, o, &s);
	}
	return lookup_in(st, &s, name, NULL, "dlsym");
}

void *dl_sym(dl_state *st, void *handle, const char *name)
{
	return dl_sym_from(st, handle, name, NULL);
}

/* The version seam for dlvsym: accept a definition only when the object's
 * .gnu.version entry for it names the requested version string. */
struct vsym_ctx {
	const dl_state *st;
	const char     *version;
};

/* The name a versym index carries in an object's own verdef table: the first
 * verdaux of the record whose vd_ndx is that index. Returns NULL when the
 * object has no verdef or no record with that index. */
static const char *verdef_name(const dl_object *d, uint16_t ndx)
{
	const unsigned char *p = d->vinfo.verdef;
	uint32_t i;

	if (!p || !d->vinfo.strtab)
		return NULL;
	for (i = 0; i < d->vinfo.verdefnum; i++) {
		const Elf64_Verdef *vd = (const Elf64_Verdef *) p;
		if (vd->vd_ndx == ndx && vd->vd_cnt > 0) {
			const Elf64_Verdaux *aux =
			    (const Elf64_Verdaux *)(p + vd->vd_aux);
			if (aux->vda_name < d->vinfo.strsz)
				return d->vinfo.strtab + aux->vda_name;
			return NULL;
		}
		if (vd->vd_next == 0)
			break;
		p += vd->vd_next;
	}
	return NULL;
}

static int vsym_match(const elf_lookup_object *o, uint32_t symidx, void *ctx)
{
	struct vsym_ctx *v = ctx;
	const dl_object *d = NULL;
	unsigned i;
	uint16_t ndx;
	const char *have;

	for (i = 0; i < v->st->obj_count; i++)
		if (v->st->obj[i].in_use && &v->st->obj[i].lo == o) {
			d = &v->st->obj[i];
			break;
		}
	if (!d)
		return -1;

	/* An object with no version table defines only unversioned names, which
	 * satisfy no explicit version request. */
	if (!o->versym)
		return -1;
	ndx = (uint16_t)(o->versym[symidx] & 0x7fff);
	have = verdef_name(d, ndx);
	if (!have || strcmp(have, v->version) != 0)
		return -1;
	return (o->versym[symidx] & 0x8000) ? 0 : 1;
}

void *dl_vsym(dl_state *st, void *handle, const char *name, const char *version)
{
	static elf_scope s;
	struct vsym_ctx vc;
	elf_version_matcher vm;

	if (!st) return NULL;
	if (!version || !*version)
		return dl_sym(st, handle, name);
	if (!name || !*name) {
		dl_set_err(st, "dlvsym", "no symbol name");
		return NULL;
	}

	if (handle == RTLD_DEFAULT) {
		build_global(st, &s);
	} else {
		const dl_object *o = handle;
		if (o < st->obj || o >= st->obj + DL_MAX_OBJECTS || !o->in_use) {
			dl_set_err(st, "dlvsym", "invalid handle");
			return NULL;
		}
		build_local(st, o, &s);
	}

	vc.st = st;
	vc.version = version;
	vm.match = vsym_match;
	vm.ctx = &vc;
	return lookup_in(st, &s, name, &vm, "dlvsym");
}
