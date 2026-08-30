/* WP-35: the two ELF hash tables and the per-object symbol probe.
 *
 * Every dynamic object carries one or both of two hash tables over its dynamic
 * symbols: the original SysV .hash and the newer .gnu.hash. Each maps a name to
 * a short chain of candidate symbol indices; the resolver walks that chain and
 * takes the first entry whose name matches and which is a visible definition.
 * This file is the arithmetic of both tables and the chain walk over them,
 * written from the generic ABI and Drepper's account rather than from glibc.
 */
#include "elf_lookup.h"

#include <string.h>

/* ---- the hash functions ------------------------------------------------ */

/* The SysV hash of the ELF specification. The masking keeps the result inside
 * 28 bits, which is the historical shape and what a .hash table was built
 * against. */
uint32_t elf_sysv_hash(const char *name)
{
	const unsigned char *s = (const unsigned char *) name;
	uint32_t h = 0, g;
	while (*s) {
		h = (h << 4) + *s++;
		g = h & 0xf0000000u;
		if (g)
			h ^= g >> 24;
		h &= ~g;
	}
	return h;
}

/* The djb2 variant glibc's .gnu.hash is built against: h = h*33 + c from a
 * 5381 seed, in 32-bit wraparound. */
uint32_t elf_gnu_hash(const char *name)
{
	const unsigned char *s = (const unsigned char *) name;
	uint32_t h = 5381;
	for (; *s; s++)
		h = h * 33u + *s;
	return h;
}

/* ---- the candidate test ------------------------------------------------ */

/* Whether the symbol at idx is a definition this object exports under name,
 * and, if a version matcher is supplied, whether it satisfies the request. A
 * candidate is skipped when it is a reference rather than a definition, when it
 * is local (locals are not visible across objects), when its type is not one a
 * lookup binds to, when the name differs, or when the matcher rejects its
 * version. On acceptance *out_bind carries the binding and *out_default whether
 * the default version was chosen. */
static int candidate_ok(const elf_lookup_object *o, uint32_t idx,
                        const char *name, const elf_version_matcher *vm,
                        unsigned char *out_bind, int *out_default)
{
	const Elf64_Sym *s;
	unsigned char bind, type;
	int def = 1;

	if (idx >= o->symcount)
		return 0;
	s = &o->symtab[idx];
	if (s->st_shndx == SHN_UNDEF)
		return 0;                 /* a reference, not a definition */
	if (s->st_name == 0 || s->st_name >= o->strsz)
		return 0;                 /* no name, or one the parser did not vouch for */

	bind = ELF_ST_BIND(s->st_info);
	if (bind != STB_GLOBAL && bind != STB_WEAK && bind != STB_GNU_UNIQUE)
		return 0;                 /* locals are invisible to other objects */

	type = ELF_ST_TYPE(s->st_info);
	switch (type) {
	case STT_NOTYPE: case STT_OBJECT: case STT_FUNC:
	case STT_COMMON:  case STT_TLS:    case STT_GNU_IFUNC:
		break;
	default:
		return 0;                 /* section, file, and the rest are not targets */
	}

	if (strcmp(name, o->strtab + s->st_name) != 0)
		return 0;

	if (vm && vm->match) {
		int r = vm->match(o, idx, vm->ctx);
		if (r < 0)
			return 0;             /* version mismatch: keep walking the chain */
		def = (r > 0);
	}

	*out_bind = bind;
	*out_default = def;
	return 1;
}

/* ---- the three probes -------------------------------------------------- */

/* GNU hash. The table is nbuckets, symoffset, bloom_size, bloom_shift, then a
 * Bloom filter of bloom_size 64-bit words, then nbuckets bucket heads, then a
 * chain word per hashable symbol starting at symoffset. A chain word carries
 * the symbol's hash with its low bit stolen as an end-of-chain marker, so two
 * names collide in a chain only when their hashes agree in all but that bit. */
static uint32_t gnu_find(const elf_lookup_object *o, const char *name,
                         uint32_t h1, const elf_version_matcher *vm,
                         unsigned char *ob, int *od)
{
	const uint32_t *gh = o->gnu_hash;
	uint32_t nbuckets   = gh[0];
	uint32_t symoffset  = gh[1];
	uint32_t bloom_size = gh[2];
	uint32_t bloom_shift = gh[3];
	const uint64_t *bloom = (const uint64_t *) &gh[4];
	const uint32_t *buckets = (const uint32_t *) &bloom[bloom_size];
	const uint32_t *chain = &buckets[nbuckets];
	uint64_t word, mask;
	uint32_t n;

	if (nbuckets == 0 || bloom_size == 0)
		return STN_UNDEF;

	/* Bloom filter: two bit positions must both be set, or the name is
	 * certainly absent and no chain need be walked. */
	word = bloom[(h1 / 64) % bloom_size];
	mask = ((uint64_t) 1 << (h1 % 64))
	     | ((uint64_t) 1 << ((h1 >> bloom_shift) % 64));
	if ((word & mask) != mask)
		return STN_UNDEF;

	n = buckets[h1 % nbuckets];
	if (n < symoffset)
		return STN_UNDEF;             /* empty bucket, or a non-hashable index */

	for (;; n++) {
		uint32_t h2 = chain[n - symoffset];
		if (((h1 | 1) == (h2 | 1)) &&
		    candidate_ok(o, n, name, vm, ob, od))
			return n;
		if (h2 & 1)
			break;                    /* end of chain */
	}
	return STN_UNDEF;
}

/* SysV hash. The table is nbucket, nchain, then nbucket bucket heads and
 * nchain chain words, one per symbol. bucket[h % nbucket] is the first index;
 * chain[i] is the next, until STN_UNDEF ends it. The chain-bound and self-loop
 * guards keep a malformed or hostile table from spinning. */
static uint32_t sysv_find(const elf_lookup_object *o, const char *name,
                          uint32_t h, const elf_version_matcher *vm,
                          unsigned char *ob, int *od)
{
	const uint32_t *ht = o->sysv_hash;
	uint32_t nbucket = ht[0];
	uint32_t nchain  = ht[1];
	const uint32_t *bucket = &ht[2];
	const uint32_t *chain = &bucket[nbucket];
	uint32_t y;

	if (nbucket == 0)
		return STN_UNDEF;
	for (y = bucket[h % nbucket]; y != STN_UNDEF; ) {
		uint32_t ny;
		if (y >= nchain)
			break;                    /* chain index past its table */
		if (candidate_ok(o, y, name, vm, ob, od))
			return y;
		ny = chain[y];
		if (ny == y)
			break;                    /* a self-loop is malformed */
		y = ny;
	}
	return STN_UNDEF;
}

/* No hash table: a linear scan of the symbol table. A valid dynamic object
 * always carries a hash, so this is a fallback for a synthetic object and for
 * robustness, not a path a real load takes. */
static uint32_t linear_find(const elf_lookup_object *o, const char *name,
                            const elf_version_matcher *vm,
                            unsigned char *ob, int *od)
{
	uint32_t i;
	for (i = 0; i < o->symcount; i++)
		if (candidate_ok(o, i, name, vm, ob, od))
			return i;
	return STN_UNDEF;
}

uint32_t elf_object_find(const elf_lookup_object *o, const char *name,
                         uint32_t gnu_h, uint32_t sysv_h,
                         const elf_version_matcher *vm,
                         unsigned char *out_bind, int *out_default)
{
	unsigned char bind = STB_GLOBAL;
	int def = 1;
	if (out_bind) *out_bind = bind;
	if (out_default) *out_default = def;
	if (!o || !name || !o->symtab || !o->strtab)
		return STN_UNDEF;

	if (o->gnu_hash)
		return gnu_find(o, name, gnu_h, vm, out_bind ? out_bind : &bind,
		                out_default ? out_default : &def);
	if (o->sysv_hash)
		return sysv_find(o, name, sysv_h, vm, out_bind ? out_bind : &bind,
		                 out_default ? out_default : &def);
	return linear_find(o, name, vm, out_bind ? out_bind : &bind,
	                   out_default ? out_default : &def);
}
