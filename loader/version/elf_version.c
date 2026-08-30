/* WP-36: the symbol version matcher.
 *
 * The three GNU version tables and the two answers the loader reads out of
 * them: which definition a versioned reference binds to, and whether every
 * required version is present at load. Written from the generic ABI and
 * Drepper's account, not from glibc's LGPL resolver (DR-0000, DR-0004,
 * DR-0019, DR-0023).
 */
#include "elf_version.h"

#include <string.h>
#include <stdio.h>

/* ---- table discovery --------------------------------------------------- */

/* Runtime address of a validated file offset, found through the PT_LOAD that
 * carries it. WP-31 proved every version-table offset lies inside a loadable
 * span, and WP-32 copied that span to link-vaddr plus load bias, so the byte at
 * file offset foff lives at load_bias + seg.vaddr + (foff - seg.off). */
static const unsigned char *rt_ptr(const elf_parsed *p, const elf_mapping *m,
                                   uint64_t foff)
{
	unsigned i;
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		if (foff >= s->off && foff < s->off + s->filesz)
			return (const unsigned char *) (uintptr_t)
			       (m->load_bias + s->vaddr + (foff - s->off));
	}
	return NULL;
}

int elf_version_info_from(elf_version_info *vi, const elf_parsed *p,
                          const elf_mapping *m)
{
	if (!vi || !p || !m)
		return -1;
	memset(vi, 0, sizeof *vi);

	if (p->has_strtab) {
		vi->strtab = (const char *) rt_ptr(p, m, p->strtab_off);
		vi->strsz = p->strsz;
	}
	if (p->has_versym)
		vi->versym = (const Elf64_Versym *) rt_ptr(p, m, p->versym_off);
	if (p->has_verdef) {
		vi->verdef = rt_ptr(p, m, p->verdef_off);
		vi->verdefnum = (uint32_t) p->verdefnum;
	}
	if (p->has_verneed) {
		vi->verneed = rt_ptr(p, m, p->verneed_off);
		vi->verneednum = (uint32_t) p->verneednum;
	}
	return 0;
}

/* A name in a version_info's string table, bounded by strsz. Returns NULL if
 * the offset is out of range so a malformed table cannot walk off the end. */
static const char *vstr(const elf_version_info *vi, uint32_t off)
{
	if (!vi->strtab || off >= vi->strsz)
		return NULL;
	return vi->strtab + off;
}

/* ---- verdef: the versions an object defines ---------------------------- */

/* The name of the version-definition node whose vd_ndx is ndx: the first
 * verdaux of that record. Returns NULL when no such node exists (for instance
 * ndx is the base or the global reserved index, which name no requirable
 * version). The walk is bounded by verdefnum and by vd_next being forward. */
static const char *verdef_node_name(const elf_version_info *vi, uint16_t ndx)
{
	const unsigned char *rec = vi->verdef;
	uint32_t i;

	if (!rec || vi->verdefnum == 0)
		return NULL;
	for (i = 0; i < vi->verdefnum; i++) {
		const Elf64_Verdef *vd = (const Elf64_Verdef *) rec;
		if ((vd->vd_ndx & ELF_VERSYM_VERSION) == (ndx & ELF_VERSYM_VERSION)) {
			const Elf64_Verdaux *aux =
			    (const Elf64_Verdaux *) (rec + vd->vd_aux);
			return vstr(vi, aux->vda_name);
		}
		if (vd->vd_next == 0)
			break;
		rec += vd->vd_next;
	}
	return NULL;
}

int elf_version_object_defines(const elf_version_object *o, const char *name,
                               uint32_t hash)
{
	const elf_version_info *vi;
	const unsigned char *rec;
	uint32_t i;

	if (!o || !name)
		return 0;
	vi = &o->vi;
	rec = vi->verdef;
	if (!rec || vi->verdefnum == 0)
		return 0;

	for (i = 0; i < vi->verdefnum; i++) {
		const Elf64_Verdef *vd = (const Elf64_Verdef *) rec;
		const unsigned char *ap = rec + vd->vd_aux;
		uint16_t j;
		/* Every verdaux of the node: index 0 is the node's own name, the
		 * rest its predecessors. Matching a predecessor is what makes a
		 * newer node satisfy a requirement for a version below it. */
		for (j = 0; j < vd->vd_cnt; j++) {
			const Elf64_Verdaux *aux = (const Elf64_Verdaux *) ap;
			const char *nm = vstr(vi, aux->vda_name);
			if (nm && strcmp(nm, name) == 0)
				return 1;
			if (aux->vda_next == 0)
				break;
			ap += aux->vda_next;
		}
		if (vd->vd_next == 0)
			break;
		rec += vd->vd_next;
	}
	(void) hash;   /* the name is the authority; the hash is a linker hint */
	return 0;
}

/* ---- the matcher ------------------------------------------------------- */

/* The version_info paired with a candidate object, or an all-NULL info (read as
 * "unversioned") when the object is not in the table. */
static const elf_version_info *info_for(const elf_version_object *objs,
                                        unsigned nobjs,
                                        const elf_lookup_object *o)
{
	static const elf_version_info none;
	unsigned i;
	for (i = 0; i < nobjs; i++)
		if (objs[i].lo == o)
			return &objs[i].vi;
	return &none;
}

/* The raw versym entry for a symbol, or the unversioned global index when the
 * object carries no version table. */
static uint16_t sym_versym(const elf_version_info *vi, uint32_t symidx)
{
	if (!vi->versym)
		return ELF_VER_NDX_GLOBAL;
	return vi->versym[symidx];
}

int elf_version_match(const elf_lookup_object *o, uint32_t symidx, void *ctx)
{
	elf_version_ctx *c = ctx;
	const elf_version_info *vi = info_for(c->objs, c->nobjs, o);
	uint16_t vs = sym_versym(vi, symidx);
	int hidden = (vs & ELF_VERSYM_HIDDEN) != 0;
	uint16_t ndx = vs & ELF_VERSYM_VERSION;
	const char *defname;

	/* A local definition is invisible across objects; WP-35's candidate test
	 * already rejects locals by binding, so this is a belt-and-braces skip. */
	if (ndx == ELF_VER_NDX_LOCAL)
		return -1;

	defname = verdef_node_name(vi, ndx);   /* NULL for the base/global index */

	if (c->req_name != NULL) {
		/* Versioned reference: an exact node-name match binds, reported
		 * default or non-default by the hidden bit. */
		if (defname && strcmp(defname, c->req_name) == 0)
			return hidden ? 0 : 1;
		/* No exact match. glibc lets a versioned reference fall back to an
		 * unversioned base definition -- the global index, which carries no
		 * version hash -- but only when the reference is not itself hidden,
		 * and never to a differently-named node. */
		if (!c->req_hidden && ndx == ELF_VER_NDX_GLOBAL && !hidden)
			return 0;
		return -1;
	}

	/* Unversioned reference: bind to the default version of the name, which is
	 * any definition without the hidden bit; reject a non-default (@) one. */
	if (hidden)
		return -1;
	return 1;
}

/* ---- building the per-reference ctx ------------------------------------ */

/* Find, in vi's verneed, the vernaux whose vna_other is the local version index
 * a reference's versym carries, and report its version name, hash, and the file
 * it is needed from. Returns 1 on a hit, 0 when no vernaux claims that index. */
static int verneed_lookup(const elf_version_info *vi, uint16_t ndx,
                          const char **out_name, uint32_t *out_hash,
                          const char **out_file)
{
	const unsigned char *rec = vi->verneed;
	uint32_t i;

	if (!rec || vi->verneednum == 0)
		return 0;
	for (i = 0; i < vi->verneednum; i++) {
		const Elf64_Verneed *vn = (const Elf64_Verneed *) rec;
		const unsigned char *ap = rec + vn->vn_aux;
		uint16_t j;
		for (j = 0; j < vn->vn_cnt; j++) {
			const Elf64_Vernaux *aux = (const Elf64_Vernaux *) ap;
			if ((aux->vna_other & ELF_VERSYM_VERSION) == ndx) {
				if (out_name) *out_name = vstr(vi, aux->vna_name);
				if (out_hash) *out_hash = aux->vna_hash;
				if (out_file) *out_file = vstr(vi, vn->vn_file);
				return 1;
			}
			if (aux->vna_next == 0)
				break;
			ap += aux->vna_next;
		}
		if (vn->vn_next == 0)
			break;
		rec += vn->vn_next;
	}
	return 0;
}

int elf_version_ctx_init(elf_version_ctx *c, const elf_version_object *ref,
                         uint32_t ref_symidx,
                         const elf_version_object *objs, unsigned nobjs,
                         const char **out_file)
{
	const elf_version_info *vi;
	uint16_t vs, ndx;

	if (!c || !ref)
		return -1;
	memset(c, 0, sizeof *c);
	c->objs = objs;
	c->nobjs = nobjs;
	if (out_file)
		*out_file = NULL;

	vi = &ref->vi;
	if (!vi->versym)
		return 0;                    /* no versym: an unversioned reference */
	vs = vi->versym[ref_symidx];
	c->req_hidden = (vs & ELF_VERSYM_HIDDEN) != 0;
	ndx = vs & ELF_VERSYM_VERSION;
	if (ndx <= ELF_VER_NDX_GLOBAL)
		return 0;                    /* local or global: unversioned reference */

	if (!verneed_lookup(vi, ndx, &c->req_name, &c->req_hash, out_file))
		return -1;                   /* versym names an index no vernaux claims */
	return 0;
}

/* ---- the load-refusal check -------------------------------------------- */

/* The version_object in objs whose soname matches file, or NULL when the named
 * dependency is not among the loaded objects. */
static const elf_version_object *provider_for(const elf_version_object *objs,
                                              unsigned nobjs, const char *file)
{
	unsigned i;
	if (!file)
		return NULL;
	for (i = 0; i < nobjs; i++)
		if (objs[i].lo && objs[i].lo->name &&
		    strcmp(objs[i].lo->name, file) == 0)
			return &objs[i];
	return NULL;
}

int elf_version_check_needed(const elf_version_object *consumer,
                             const char *consumer_name,
                             const elf_version_object *objs, unsigned nobjs,
                             char *msg, size_t msgsz, const char **bad_lib,
                             unsigned *weak_missing)
{
	const elf_version_info *vi;
	const unsigned char *rec;
	uint32_t i;

	if (bad_lib) *bad_lib = NULL;
	if (weak_missing) *weak_missing = 0;
	if (!consumer)
		return 0;
	vi = &consumer->vi;
	rec = vi->verneed;
	if (!rec || vi->verneednum == 0)
		return 0;                    /* the consumer requires no versions */

	/* Each verneed record names one dependency (vn_file) and, in its vernaux
	 * chain, the versions this consumer was linked against from it. The
	 * providing object is the loaded object whose soname is vn_file; a version
	 * it does not define -- and a dependency not loaded at all -- is a miss,
	 * refused when the requirement is non-weak. */
	for (i = 0; i < vi->verneednum; i++) {
		const Elf64_Verneed *vn = (const Elf64_Verneed *) rec;
		const char *file = vstr(vi, vn->vn_file);
		const elf_version_object *prov = provider_for(objs, nobjs, file);
		const unsigned char *ap = rec + vn->vn_aux;
		uint16_t j;

		for (j = 0; j < vn->vn_cnt; j++) {
			const Elf64_Vernaux *aux = (const Elf64_Vernaux *) ap;
			const char *name = vstr(vi, aux->vna_name);
			int weak = (aux->vna_flags & ELF_VER_FLG_WEAK) != 0;

			if (!(prov && elf_version_object_defines(prov, name, aux->vna_hash))) {
				if (weak) {
					if (weak_missing)
						(*weak_missing)++;
				} else {
					if (msg && msgsz)
						snprintf(msg, msgsz,
						    "version `%s' not found (required by %s)",
						    name ? name : "(null)",
						    consumer_name ? consumer_name : "(unknown)");
					if (bad_lib)
						*bad_lib = file;
					return -1;
				}
			}
			if (aux->vna_next == 0)
				break;
			ap += aux->vna_next;
		}
		if (vn->vn_next == 0)
			break;
		rec += vn->vn_next;
	}
	return 0;
}
