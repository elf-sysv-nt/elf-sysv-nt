/* WP-35: symbol lookup.
 *
 * WP-33 walked the object graph and WP-34 relocated it with the minimum symbol
 * resolution a relocation needs -- a linear first-definition scan of the scope
 * in load order. This package is the general resolver the rest of the loader
 * asks a name of: the reference "printf" in some object, answered with the
 * definition a real ld.so would bind it to. It carries the three things that
 * scan did not: the hash tables that make a lookup an O(1) probe instead of an
 * O(symbols) walk, the scope ordering that decides which objects are searched
 * and in what order, and the interposition rule that makes LD_PRELOAD mean on
 * this platform what it means on Linux.
 *
 * It is written from the generic ABI and Drepper's account of the hash tables
 * and the resolution order, not from glibc's resolver, which is LGPL and
 * assumes a kernel this platform does not have (DR-0000, DR-0004). The binding
 * rule it implements is glibc's observable one: across the whole search list
 * the first global (or GNU-unique) definition wins outright, and only when no
 * global exists does the first weak win. Interposition falls out of ordering
 * rather than being a rule of its own -- a definition reached earlier in the
 * search list wins, so a preloaded object placed right after the main object
 * shadows the same name in a regular dependency.
 *
 * Versioned lookup is WP-36 and sits directly on top of this one through a
 * single seam: the elf_version_matcher a caller may pass. WP-35 passes none,
 * which is unversioned lookup; WP-36 passes a matcher that reads .gnu.version
 * against the requester's verneed and prefers the default binding. Nothing else
 * in this interface changes when versioning arrives, which is the point of
 * drawing the seam here.
 */
#ifndef ELFSYSV_LOADER_LOOKUP_H
#define ELFSYSV_LOADER_LOOKUP_H

#include <stddef.h>
#include <stdint.h>

#include "../elf/elf_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* st_info accessors and the symbol constants the resolver tests. Named here,
 * not pulled from a host <elf.h>, for the same reason WP-31 carries its own
 * structures: this loader trusts its own definitions of the format. */
#define ELF_ST_BIND(i) ((unsigned char)((i) >> 4))
#define ELF_ST_TYPE(i) ((unsigned char)((i) & 0xf))

#define STB_LOCAL       0
#define STB_GLOBAL      1
#define STB_WEAK        2
#define STB_GNU_UNIQUE  10

#define STT_NOTYPE      0
#define STT_OBJECT      1
#define STT_FUNC        2
#define STT_SECTION     3
#define STT_FILE        4
#define STT_COMMON      5
#define STT_TLS         6
#define STT_GNU_IFUNC   10

#define STN_UNDEF   0
#define SHN_UNDEF   0
#define SHN_ABS     0xfff1
#define SHN_COMMON  0xfff2

/* One object presented to the resolver. Every pointer is a runtime address
 * inside the object's mapped image, already biased -- the dynamic view WP-34's
 * elf_reloc_add discovers, or what a test builds directly over synthetic
 * tables. The resolver reads these and never the file. */
typedef struct elf_lookup_object {
	const char      *name;       /* soname or path, for diagnostics */
	uint64_t         bias;       /* load bias; st_value + bias is the address */

	const char      *strtab;     /* .dynstr */
	uint64_t         strsz;
	const Elf64_Sym *symtab;     /* .dynsym */
	uint64_t         symcount;   /* entries, sized from a hash table */

	const uint32_t  *sysv_hash;  /* .hash, or NULL */
	const uint32_t  *gnu_hash;   /* .gnu.hash, or NULL */

	const Elf64_Versym *versym;  /* .gnu.version, or NULL until WP-36 uses it */
} elf_lookup_object;

/* The versioning seam (WP-36). For a candidate definition at symbol index
 * symidx in object o, match() returns:
 *     1  the candidate satisfies the request and is the default (@@) binding,
 *     0  the candidate satisfies the request as a non-default (@) binding,
 *    -1  the candidate does not satisfy the request and must be skipped.
 * A NULL matcher is unversioned lookup: every visible definition is accepted
 * and reported as default. WP-36 supplies a matcher that reads o->versym at
 * symidx and compares it against the referencing object's verneed; nothing else
 * in this interface moves when it does. */
typedef struct elf_version_matcher {
	int (*match)(const elf_lookup_object *o, uint32_t symidx, void *ctx);
	void *ctx;
} elf_version_matcher;

/* The two hash functions, exposed because a caller resolving one name against
 * many objects computes each hash once and passes it in, and because the tests
 * check the functions against known vectors. */
uint32_t elf_sysv_hash(const char *name);
uint32_t elf_gnu_hash(const char *name);

/* Find name within a single object. Uses .gnu.hash when the object carries one,
 * else .hash, else a linear scan of the symbol table. gnu_h and sysv_h are the
 * precomputed hashes (elf_gnu_hash(name) and elf_sysv_hash(name)); a caller
 * looping over a scope computes them once. Returns the symbol index of the
 * first visible definition of the name -- a global, weak, or GNU-unique symbol
 * that is not undefined and passes the version matcher -- walking the object's
 * hash chain and skipping locals, undefs, and version mismatches, exactly as a
 * real loader walks it. Returns STN_UNDEF (0) when the object defines no such
 * symbol. On a hit, *out_bind receives the definition's binding (STB_*) and
 * *out_default whether it was the default version (always 1 unversioned). */
uint32_t elf_object_find(const elf_lookup_object *o, const char *name,
                         uint32_t gnu_h, uint32_t sysv_h,
                         const elf_version_matcher *vm,
                         unsigned char *out_bind, int *out_default);

/* A search list: objects in the order they are searched. A scope is built once
 * as the graph settles and then read many times, so it is a plain fixed array;
 * ELF_SCOPE_MAX is generous for the closures this loader handles and a full
 * scope is a diagnostic, not a silent truncation. */
#define ELF_SCOPE_MAX 512

typedef struct elf_scope {
	const elf_lookup_object *obj[ELF_SCOPE_MAX];
	unsigned                 count;
} elf_scope;

void elf_scope_init(elf_scope *s);

/* Append one object to the search list. Returns 0, or -1 when the scope is
 * full. Order is the caller's responsibility and is the whole of the policy:
 * the global scope is built as main object, then LD_PRELOAD interposers, then
 * the breadth-first dependency closure, then any RTLD_GLOBAL dlopen additions,
 * which is the order that makes interposition and first-definition-wins come
 * out right. elf_scope_build_global writes exactly that order. */
int elf_scope_add(elf_scope *s, const elf_lookup_object *o);

/* Build a global scope in the canonical order: the main object first, then the
 * preload objects in the order given (LD_PRELOAD, left to right), then the
 * dependency closure in breadth-first load order. deps is that closure as
 * WP-33 produced it, already excluding the main object. Any of the arrays may
 * be empty. Returns 0, or -1 if the objects do not fit. This is the one place
 * the interposition order is written down as code. */
int elf_scope_build_global(elf_scope *s,
                           const elf_lookup_object *main_obj,
                           const elf_lookup_object *const *preload, unsigned npre,
                           const elf_lookup_object *const *deps, unsigned ndeps);

/* What a lookup resolved to. */
typedef struct elf_lookup_result {
	int              found;
	const elf_lookup_object *obj;   /* the defining object */
	uint32_t         symidx;        /* index of the defining symbol */
	const Elf64_Sym *sym;           /* the defining symbol itself */
	uint64_t         value;         /* runtime address: bias + st_value,
	                                 * or st_value alone for an SHN_ABS symbol */
	unsigned char    bind;          /* STB_GLOBAL, STB_WEAK, or STB_GNU_UNIQUE */
	int              is_default;    /* the default (@@) version was chosen */
} elf_lookup_result;

/* Resolve name across a reference's search order. The scopes are searched in
 * this order, which is the order the task and a real ld.so specify:
 *   1. global   -- the global scope (main object, LD_PRELOAD, the dependency
 *                  closure, and RTLD_GLOBAL dlopen additions),
 *   2. local    -- the referencing object's own dependency list, for an object
 *                  brought in by an RTLD_LOCAL dlopen; NULL when the reference
 *                  lives in the global scope already,
 *   3. glob_dl  -- RTLD_GLOBAL dlopen additions kept as a distinct trailing
 *                  list rather than folded into global; NULL when they were
 *                  appended to global directly.
 * Across the whole concatenation the binding rule is glibc's observable one:
 * the first global or GNU-unique definition wins outright and ends the search;
 * a weak definition is remembered but does not end it, so a global reached
 * later still overrides an earlier weak; with no global anywhere, the first
 * weak wins. A reference no object defines returns with out->found == 0, which
 * a caller treats as an error for a non-weak reference and as a null binding
 * for a weak one. Returns 0 on a definite answer (found or not), -1 only on a
 * malformed argument. vm is the version seam; pass NULL for unversioned. */
int elf_lookup(const elf_scope *global, const elf_scope *local,
               const elf_scope *glob_dl, const char *name,
               const elf_version_matcher *vm, elf_lookup_result *out);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_LOOKUP_H */
