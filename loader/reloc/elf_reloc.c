/* WP-34: the relocation engine. See elf_reloc.h for the contract.
 *
 * The engine reads each object's dynamic section out of its own mapped image
 * -- every d_ptr tag is a link address the mapping turned into a runtime one by
 * adding the load bias -- and writes the computed relocation values back
 * through that image. It resolves cross-object references by scanning the scope
 * in load order and taking the first strong definition, which is the default a
 * fuller lookup (WP-35) refines rather than contradicts.
 */
#include "elf_reloc.h"

#include <string.h>
#include <stdio.h>

/* ---- small helpers ----------------------------------------------------- */

static elf_reloc_err fail(elf_reloc_diag *d, elf_reloc_err code,
                          const char *field, const char *obj, const char *what)
{
	if (d) {
		d->code = code;
		d->field = field;
		snprintf(d->msg, sizeof d->msg, "%.180s: %.60s", obj ? obj : "?", what);
	}
	return code;
}

/* Turn a validated file offset into the link virtual address the loaded image
 * carries it at, by finding the PT_LOAD that backs it. Returns 1 on success. */
static int off_to_vaddr(const elf_parsed *p, uint64_t off, uint64_t *vaddr)
{
	unsigned i;
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		if (off >= s->off && off < s->off + s->filesz) {
			*vaddr = s->vaddr + (off - s->off);
			return 1;
		}
	}
	return 0;
}

/* An ifunc resolver takes no argument in the form this engine calls it: the
 * body it returns is the address to use. Real glibc passes hwcap and a
 * cpu_features pointer to a resolver that opts in; the resolvers this engine
 * meets read the hardware themselves, which is the common el8 shape. */
typedef uint64_t (*ifunc_t)(void);

/* ---- symbol count discovery ------------------------------------------- */

/* Walk a GNU hash table to find how many symbols the dynamic table holds. The
 * count is one past the highest index any bucket's chain reaches. */
static int gnu_hash_symcount(const uint32_t *gh, uint64_t *out)
{
	uint32_t nbuckets = gh[0];
	uint32_t symoffset = gh[1];
	uint32_t bloom_size = gh[2];
	const uint64_t *bloom = (const uint64_t *)(const void *)(gh + 4);
	const uint32_t *buckets = (const uint32_t *)(const void *)(bloom + bloom_size);
	const uint32_t *chain = buckets + nbuckets;
	uint32_t i, last = 0;

	for (i = 0; i < nbuckets; i++)
		if (buckets[i] > last)
			last = buckets[i];

	if (last < symoffset) {
		*out = symoffset;  /* no exported symbols past the bias */
		return 1;
	}
	/* Follow the chain from the highest bucket to its terminator (low bit). */
	while (!(chain[last - symoffset] & 1))
		last++;
	*out = (uint64_t) last + 1;
	return 1;
}

/* ---- reading one object's dynamic view -------------------------------- */

void elf_reloc_scope_init(elf_reloc_scope *s)
{
	memset(s, 0, sizeof *s);
}

elf_reloc_err elf_reloc_add(elf_reloc_scope *s, elf_mapping *m,
                            const elf_parsed *p, const char *name,
                            elf_reloc_diag *diag)
{
	elf_reloc_object *o;
	uint64_t dyn_vaddr, i;
	const Elf64_Dyn *dyn;
	uint64_t rela = 0, relasz = 0, relaent = 24;
	uint64_t jmprel = 0, pltrelsz = 0, pltrel = 0;
	uint64_t relr = 0, relrsz = 0, relrent = 8;
	uint64_t strtab = 0, strsz = 0, symtab = 0;
	uint64_t hash = 0, gnu_hash = 0, pltgot = 0;
	uint64_t flags = 0, flags1 = 0;
	int seen_bind_now = 0;

	if (!s || !m || !p || !diag)
		return elf_reloc_err_arg;
	if (s->count >= ELF_RELOC_MAX_OBJ)
		return fail(diag, elf_reloc_err_scope_full, "scope", name,
		            "more objects than the scope can hold");
	if (!p->has_dynamic)
		return fail(diag, elf_reloc_err_dynamic, "PT_DYNAMIC", name,
		            "the object carries no dynamic section");

	o = &s->obj[s->count];
	memset(o, 0, sizeof *o);
	o->map = m;
	o->parsed = p;
	o->name = name;
	o->bias = m->load_bias;
	o->scope = s;

	if (!off_to_vaddr(p, p->dyn_off, &dyn_vaddr))
		return fail(diag, elf_reloc_err_dynamic, "PT_DYNAMIC", name,
		            "the dynamic section is not inside a PT_LOAD");
	dyn = (const Elf64_Dyn *)(uintptr_t)(dyn_vaddr + o->bias);

	for (i = 0; i < p->dyn_count; i++) {
		int64_t tag = dyn[i].d_tag;
		uint64_t val = dyn[i].d_un.d_val;
		switch (tag) {
		case DT_STRTAB:   strtab = val; break;
		case DT_STRSZ:    strsz = val; break;
		case DT_SYMTAB:   symtab = val; break;
		case DT_HASH:     hash = val; break;
		case DT_GNU_HASH: gnu_hash = val; break;
		case DT_RELA:     rela = val; break;
		case DT_RELASZ:   relasz = val; break;
		case DT_RELAENT:  relaent = val; break;
		case DT_JMPREL:   jmprel = val; break;
		case DT_PLTRELSZ: pltrelsz = val; break;
		case DT_PLTREL:   pltrel = val; break;
		case DT_PLTGOT:   pltgot = val; break;
		case DT_RELR:     relr = val; break;
		case DT_RELRSZ:   relrsz = val; break;
		case DT_RELRENT:  relrent = val; break;
		case DT_FLAGS:    flags = val; break;
		case DT_FLAGS_1:  flags1 = val; break;
		case DT_BIND_NOW: seen_bind_now = 1; break;
		default: break;
		}
	}

	if (relaent == 0) relaent = 24;
	if (relrent == 0) relrent = 8;

	o->strtab = strtab ? (const char *)(uintptr_t)(strtab + o->bias) : NULL;
	o->strsz = strsz;
	o->symtab = symtab ? (const Elf64_Sym *)(uintptr_t)(symtab + o->bias) : NULL;
	o->sysv_hash = hash ? (const uint32_t *)(uintptr_t)(hash + o->bias) : NULL;
	o->gnu_hash = gnu_hash ? (const uint32_t *)(uintptr_t)(gnu_hash + o->bias) : NULL;

	if (rela && relasz) {
		o->rela = (const Elf64_Rela *)(uintptr_t)(rela + o->bias);
		o->rela_n = relasz / relaent;
	}
	if (jmprel && pltrelsz) {
		if (pltrel != DT_RELA)
			return fail(diag, elf_reloc_err_unsupported, "DT_PLTREL", name,
			            "the PLT relocations are REL, not RELA");
		o->jmprel = (const Elf64_Rela *)(uintptr_t)(jmprel + o->bias);
		o->jmprel_n = pltrelsz / 24;
	}
	if (relr && relrsz) {
		o->relr = (const uint64_t *)(uintptr_t)(relr + o->bias);
		o->relr_n = relrsz / relrent;
	}
	o->pltgot = pltgot ? (uint64_t *)(uintptr_t)(pltgot + o->bias) : NULL;

	o->bind_now = seen_bind_now || (flags & DF_BIND_NOW) || (flags1 & DF_1_NOW);

	/* Symbol count: prefer .hash's nchain, else walk .gnu.hash. */
	if (o->symtab) {
		if (o->sysv_hash)
			o->symcount = o->sysv_hash[1];
		else if (o->gnu_hash)
			gnu_hash_symcount(o->gnu_hash, &o->symcount);
		else
			return fail(diag, elf_reloc_err_symtab, "DT_HASH", name,
			            "a symbol table with no hash to size it");
	}

	o->has_tls = p->has_tls;

	s->count++;
	return elf_reloc_ok;
}

/* ---- symbol resolution ------------------------------------------------- */

/* The result of resolving a name across the scope. */
struct sym_res {
	int found;
	int is_ifunc;
	uint64_t value;          /* defining object's bias + st_value */
	uint64_t size;           /* defining symbol's st_size */
	const elf_reloc_object *def;
};

static const char *sym_name(const elf_reloc_object *o, uint32_t idx)
{
	return o->strtab + o->symtab[idx].st_name;
}

/* First strong definition in load order wins; a weak one is remembered as a
 * fallback. exclude, when not NULL, is skipped -- a COPY relocation must find
 * the datum in some object other than the one importing it. */
static void resolve(const elf_reloc_scope *s, const char *name,
                    const elf_reloc_object *exclude, struct sym_res *r)
{
	unsigned i;
	uint64_t j;
	memset(r, 0, sizeof *r);
	for (i = 0; i < s->count; i++) {
		const elf_reloc_object *o = &s->obj[i];
		if (o == exclude || !o->symtab)
			continue;
		for (j = 0; j < o->symcount; j++) {
			const Elf64_Sym *sym = &o->symtab[j];
			unsigned char bind, type;
			if (sym->st_shndx == SHN_UNDEF || sym->st_name == 0)
				continue;
			bind = ELF64_ST_BIND(sym->st_info);
			type = ELF64_ST_TYPE(sym->st_info);
			if (bind != STB_GLOBAL && bind != STB_WEAK)
				continue;
			if (strcmp(name, o->strtab + sym->st_name) != 0)
				continue;
			/* a match */
			if (bind == STB_GLOBAL || !r->found) {
				r->found = 1;
				r->value = o->bias + sym->st_value;
				r->size = sym->st_size;
				r->def = o;
				r->is_ifunc = (type == STT_GNU_IFUNC);
			}
			if (bind == STB_GLOBAL)
				return;   /* strong wins outright */
		}
	}
}

/* Run an ifunc resolver and yield the address of the body it chose. */
static uint64_t run_ifunc(uint64_t resolver_addr)
{
	ifunc_t r = (ifunc_t)(uintptr_t) resolver_addr;
	return r();
}

/* ---- the lazy fixup ---------------------------------------------------- */

uint64_t elf_reloc_fixup(elf_reloc_object *o, uint64_t index)
{
	const Elf64_Rela *rl = &o->jmprel[index];
	uint32_t symidx = ELF64_R_SYM(rl->r_info);
	uint64_t *slot = (uint64_t *)(uintptr_t)(o->bias + rl->r_offset);
	struct sym_res r;
	uint64_t value;

	resolve(o->scope, sym_name(o, symidx), NULL, &r);
	if (!r.found)
		return 0;  /* a real loader would abort; the caller sees a null jump */
	value = r.is_ifunc ? run_ifunc(r.value) : r.value + (uint64_t) rl->r_addend;
	*slot = value;
	return value;
}

/* The assembly trampoline installed at GOT[2]; it calls elf_reloc_fixup. */
extern void elf_reloc_runtime_resolve(void);

/* ---- RELR -------------------------------------------------------------- */

void elf_reloc_relr(uint64_t bias, const uint64_t *relr, uint64_t n)
{
	uint64_t k;
	uint64_t *where = NULL;
	for (k = 0; k < n; k++) {
		uint64_t e = relr[k];
		if ((e & 1) == 0) {
			/* An even entry is an address: relocate the word there and set
			 * the running cursor to just past it. */
			where = (uint64_t *)(uintptr_t)(bias + e);
			*where += bias;
			where++;
		} else {
			/* An odd entry is a bitmap over the next 63 words from the
			 * cursor; a set bit relocates the word at that position. */
			uint64_t bits = e >> 1;
			unsigned i;
			for (i = 0; bits; bits >>= 1, i++)
				if (bits & 1)
					where[i] += bias;
			where += 63;
		}
	}
}

static void apply_relr(elf_reloc_object *o)
{
	elf_reloc_relr(o->bias, o->relr, o->relr_n);
}

/* ---- RELA (one table) -------------------------------------------------- */

/* deferred[] collects (object, rela) pairs for IRELATIVE, applied last so a
 * resolver sees a fully relocated world. */
struct ireloc { elf_reloc_object *o; const Elf64_Rela *r; };

static elf_reloc_err apply_rela_table(elf_reloc_object *o, const Elf64_Rela *tab,
                                      uint64_t n, int is_plt,
                                      struct ireloc *defer, unsigned *ndefer,
                                      elf_reloc_diag *d)
{
	uint64_t i, bias = o->bias;
	for (i = 0; i < n; i++) {
		const Elf64_Rela *rl = &tab[i];
		uint32_t type = ELF64_R_TYPE(rl->r_info);
		uint32_t symidx = ELF64_R_SYM(rl->r_info);
		uint64_t *where = (uint64_t *)(uintptr_t)(bias + rl->r_offset);
		struct sym_res r;

		switch (type) {
		case R_X86_64_NONE:
			break;

		case R_X86_64_RELATIVE:
			*where = bias + (uint64_t) rl->r_addend;
			break;

		case R_X86_64_IRELATIVE:
			defer[(*ndefer)++] = (struct ireloc){ o, rl };
			break;

		case R_X86_64_JUMP_SLOT:
			/* Under lazy binding this table is handled by the caller;
			 * reaching here means eager (BIND_NOW), so resolve fully. */
			resolve(o->scope, sym_name(o, symidx), NULL, &r);
			if (!r.found) {
				if (ELF64_ST_BIND(o->symtab[symidx].st_info) == STB_WEAK) {
					*where = 0; break;
				}
				return fail(d, elf_reloc_err_undef, "R_X86_64_JUMP_SLOT",
				            o->name, sym_name(o, symidx));
			}
			*where = r.is_ifunc ? run_ifunc(r.value)
			                    : r.value + (uint64_t) rl->r_addend;
			break;

		case R_X86_64_GLOB_DAT:
		case R_X86_64_64:
			resolve(o->scope, sym_name(o, symidx), NULL, &r);
			if (!r.found) {
				if (ELF64_ST_BIND(o->symtab[symidx].st_info) == STB_WEAK) {
					*where = (uint64_t) rl->r_addend; break;
				}
				return fail(d, elf_reloc_err_undef,
				            type == R_X86_64_64 ? "R_X86_64_64"
				                                : "R_X86_64_GLOB_DAT",
				            o->name, sym_name(o, symidx));
			}
			*where = (r.is_ifunc ? run_ifunc(r.value) : r.value)
			         + (uint64_t) rl->r_addend;
			break;

		case R_X86_64_COPY:
			resolve(o->scope, sym_name(o, symidx), o, &r);
			if (!r.found)
				return fail(d, elf_reloc_err_undef, "R_X86_64_COPY",
				            o->name, sym_name(o, symidx));
			memcpy(where, (const void *)(uintptr_t) r.value, (size_t) r.size);
			break;

		case R_X86_64_TPOFF64: {
			const elf_reloc_object *def = o;
			uint64_t stv = 0;
			if (symidx != 0) {
				resolve(o->scope, sym_name(o, symidx), NULL, &r);
				if (!r.found)
					return fail(d, elf_reloc_err_undef, "R_X86_64_TPOFF64",
					            o->name, sym_name(o, symidx));
				def = r.def;
				stv = r.value - def->bias;
			}
			if (!def->has_tls)
				return fail(d, elf_reloc_err_tls, "R_X86_64_TPOFF64",
				            o->name, "no static TLS block for the module");
			*where = (uint64_t)((int64_t) stv + rl->r_addend + def->tls_tpoff);
			break;
		}
		case R_X86_64_DTPMOD64: {
			const elf_reloc_object *def = o;
			if (symidx != 0) {
				resolve(o->scope, sym_name(o, symidx), NULL, &r);
				if (r.found) def = r.def;
			}
			*where = def->tls_modid;
			break;
		}
		case R_X86_64_DTPOFF64: {
			uint64_t stv = 0;
			if (symidx != 0) {
				resolve(o->scope, sym_name(o, symidx), NULL, &r);
				if (!r.found)
					return fail(d, elf_reloc_err_undef, "R_X86_64_DTPOFF64",
					            o->name, sym_name(o, symidx));
				stv = r.value - r.def->bias;
			}
			*where = stv + (uint64_t) rl->r_addend;
			break;
		}

		default:
			(void) is_plt;
			return fail(d, elf_reloc_err_unsupported, "r_info", o->name,
			            "a relocation type this engine does not carry");
		}
	}
	return elf_reloc_ok;
}

/* ---- apply everything -------------------------------------------------- */

/* x86-64 is TLS variant II: the static blocks sit below the thread pointer.
 * Each module's block is rounded to its alignment and stacked downward, so the
 * first module added is nearest the tp. This is the initial/local-exec layout a
 * TPOFF relocation is computed against; standing up a live thread pointer is
 * WP-40/WP-41's, and README.md records that boundary. */
static void assign_static_tls(elf_reloc_scope *s)
{
	unsigned i;
	uint64_t running = 0, modid = 1;
	for (i = 0; i < s->count; i++) {
		elf_reloc_object *o = &s->obj[i];
		const elf_parsed *p = o->parsed;
		uint64_t align, size;
		if (!o->has_tls)
			continue;
		align = p->tls_align ? p->tls_align : 1;
		size = p->tls_memsz;
		running += size;
		if (align > 1)
			running = (running + align - 1) & ~(align - 1);
		o->tls_modid = modid++;
		o->tls_tpoff = -(int64_t) running;   /* block start, below the tp */
	}
	s->tls_static_size = running;
}

elf_reloc_err elf_reloc_apply(elf_reloc_scope *s, elf_reloc_diag *diag)
{
	unsigned i;
	unsigned ndefer = 0;
	static struct ireloc defer[ELF_RELOC_MAX_OBJ * 64];
	elf_reloc_err rc;

	if (!s || !diag)
		return elf_reloc_err_arg;

	assign_static_tls(s);

	for (i = 0; i < s->count; i++) {
		elf_reloc_object *o = &s->obj[i];

		if (o->relr)
			apply_relr(o);

		if (o->rela) {
			rc = apply_rela_table(o, o->rela, o->rela_n, 0,
			                      defer, &ndefer, diag);
			if (rc != elf_reloc_ok)
				return rc;
		}

		if (o->jmprel) {
			if (o->bind_now) {
				rc = apply_rela_table(o, o->jmprel, o->jmprel_n, 1,
				                      defer, &ndefer, diag);
				if (rc != elf_reloc_ok)
					return rc;
			} else {
				/* Lazy: bias each slot's PLT return address, resolve any
				 * IRELATIVE eagerly, and arm the resolver hooks. */
				uint64_t k;
				for (k = 0; k < o->jmprel_n; k++) {
					const Elf64_Rela *rl = &o->jmprel[k];
					uint32_t type = ELF64_R_TYPE(rl->r_info);
					uint64_t *slot =
					    (uint64_t *)(uintptr_t)(o->bias + rl->r_offset);
					if (type == R_X86_64_IRELATIVE)
						defer[ndefer++] = (struct ireloc){ o, rl };
					else
						*slot += o->bias;
				}
				if (o->pltgot) {
					o->pltgot[1] = (uint64_t)(uintptr_t) o;
					o->pltgot[2] =
					    (uint64_t)(uintptr_t) &elf_reloc_runtime_resolve;
				}
			}
		}
	}

	/* IRELATIVE last, so resolvers see a relocated world. */
	for (i = 0; i < ndefer; i++) {
		elf_reloc_object *o = defer[i].o;
		const Elf64_Rela *rl = defer[i].r;
		uint64_t *where = (uint64_t *)(uintptr_t)(o->bias + rl->r_offset);
		*where = run_ifunc(o->bias + (uint64_t) rl->r_addend);
	}

	/* Freeze each object's relro now that every write through it is done. */
	for (i = 0; i < s->count; i++) {
		elf_map_diag md;
		if (elf_map_protect_relro(s->obj[i].map, &md) != elf_map_ok)
			return fail(diag, elf_reloc_err_relro, "PT_GNU_RELRO",
			            s->obj[i].name, md.msg);
	}

	return elf_reloc_ok;
}

/* Apply one object's self-contained relocations only. */
static elf_reloc_err bootstrap_table(elf_reloc_object *o, const Elf64_Rela *tab,
                                     uint64_t n, elf_reloc_diag *d)
{
	uint64_t i, bias = o->bias;
	for (i = 0; i < n; i++) {
		const Elf64_Rela *rl = &tab[i];
		uint32_t type = ELF64_R_TYPE(rl->r_info);
		uint32_t symidx = ELF64_R_SYM(rl->r_info);
		uint64_t *where = (uint64_t *)(uintptr_t)(bias + rl->r_offset);
		struct sym_res r;

		switch (type) {
		case R_X86_64_RELATIVE:
			*where = bias + (uint64_t) rl->r_addend;
			break;
		case R_X86_64_TPOFF64: {
			const elf_reloc_object *def = o;
			uint64_t stv = 0;
			if (symidx != 0) {
				resolve(o->scope, sym_name(o, symidx), NULL, &r);
				if (!r.found) break;   /* an external TLS symbol; leave it */
				def = r.def; stv = r.value - def->bias;
			}
			if (!def->has_tls)
				return fail(d, elf_reloc_err_tls, "R_X86_64_TPOFF64",
				            o->name, "no static TLS block for the module");
			*where = (uint64_t)((int64_t) stv + rl->r_addend + def->tls_tpoff);
			break;
		}
		case R_X86_64_DTPMOD64:
			*where = o->tls_modid;
			break;
		case R_X86_64_DTPOFF64: {
			uint64_t stv = 0;
			if (symidx != 0) {
				resolve(o->scope, sym_name(o, symidx), NULL, &r);
				if (r.found) stv = r.value - r.def->bias;
			}
			*where = stv + (uint64_t) rl->r_addend;
			break;
		}
		default:
			break;   /* symbol-resolving and PLT relocations are left alone */
		}
	}
	return elf_reloc_ok;
}

elf_reloc_err elf_reloc_apply_bootstrap(elf_reloc_scope *s, elf_reloc_diag *diag)
{
	unsigned i;
	elf_reloc_err rc;
	if (!s || !diag)
		return elf_reloc_err_arg;
	assign_static_tls(s);
	for (i = 0; i < s->count; i++) {
		elf_reloc_object *o = &s->obj[i];
		if (o->relr)
			apply_relr(o);
		if (o->rela) {
			rc = bootstrap_table(o, o->rela, o->rela_n, diag);
			if (rc != elf_reloc_ok) return rc;
		}
		if (o->jmprel) {
			rc = bootstrap_table(o, o->jmprel, o->jmprel_n, diag);
			if (rc != elf_reloc_ok) return rc;
		}
	}
	return elf_reloc_ok;
}

const char *elf_reloc_err_name(elf_reloc_err code)
{
	switch (code) {
	case elf_reloc_ok:              return "ok";
	case elf_reloc_err_arg:         return "arg";
	case elf_reloc_err_scope_full:  return "scope_full";
	case elf_reloc_err_dynamic:     return "dynamic";
	case elf_reloc_err_symtab:      return "symtab";
	case elf_reloc_err_unsupported: return "unsupported";
	case elf_reloc_err_undef:       return "undef";
	case elf_reloc_err_tls:         return "tls";
	case elf_reloc_err_relro:       return "relro";
	}
	return "?";
}
