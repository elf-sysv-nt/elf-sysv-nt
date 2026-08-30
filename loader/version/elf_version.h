/* WP-36: the symbol version matcher.
 *
 * WP-35 built the general resolver and drew one seam for versioning: an
 * optional elf_version_matcher a caller passes to elf_object_find and
 * elf_lookup. This package is what plugs into that seam. It reads the three
 * GNU symbol-versioning tables an object carries -- .gnu.version (a version
 * index per dynamic symbol), .gnu.version_d (the versions the object defines),
 * and .gnu.version_r (the versions the object requires of its dependencies) --
 * and turns them into two answers the loader needs:
 *
 *   1. Per reference, which definition binds. A reference to memcpy that names
 *      GLIBC_2.14 must reach the body defined at GLIBC_2.14 and not the one
 *      defined at GLIBC_2.2.5 in the same library, and an unversioned reference
 *      must reach the default (@@) definition rather than a non-default (@) one.
 *      This is the matcher callback elf_version_match, driven per reference by
 *      the ctx elf_version_ctx_init builds from that reference's verneed.
 *
 *   2. Per load, whether every required version is present. A consumer's
 *      verneed names, for each dependency, the version nodes it was linked
 *      against. If a required non-weak node is absent from the object that is
 *      supposed to provide it, the load is refused with the message a real
 *      ld.so gives rather than allowed to bind silently to whatever survives.
 *      This is elf_version_check_needed.
 *
 * It is written from the generic ABI and Drepper's account of the version
 * records and the resolution rule, not from glibc's resolver, which is
 * LGPL-2.1-or-later and assumes a kernel this platform does not have (DR-0000,
 * DR-0004, DR-0019). The behaviour it reproduces is glibc's observable one:
 * an exact version-name match binds; an unversioned reference binds to the
 * default version; a versioned reference with no exact match may fall back to
 * an unversioned base definition but never to a differently-named node; a
 * version-definition node carries its predecessors in its verdaux chain, so a
 * newer node implies the versions below it. DR-0023 records the load-bearing
 * choices.
 */
#ifndef ELFSYSV_LOADER_VERSION_H
#define ELFSYSV_LOADER_VERSION_H

#include <stddef.h>
#include <stdint.h>

#include "../elf/elf_types.h"
#include "../elf/elf_parse.h"
#include "../map/elf_map.h"
#include "../lookup/elf_lookup.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version-index and flag constants, from the generic ABI. A versym entry is a
 * 15-bit version index in its low bits with the high bit (VERSYM_HIDDEN) set on
 * a definition that is a non-default (@) version of its name. Indices 0 and 1
 * are reserved: 0 is a local symbol, 1 the unversioned global (base). */
#define ELF_VER_NDX_LOCAL   0
#define ELF_VER_NDX_GLOBAL  1
#define ELF_VERSYM_HIDDEN   0x8000
#define ELF_VERSYM_VERSION  0x7fff

/* verdef vd_flags and vernaux vna_flags. BASE marks the version-definition
 * record that names the object itself; WEAK marks a version whose absence is
 * tolerated rather than a load error. */
#define ELF_VER_FLG_BASE    0x1
#define ELF_VER_FLG_WEAK    0x2

/* The version tables of one object, as runtime pointers into its mapped image.
 * Every field is NULL or zero for an object that carries no versioning, which
 * the matcher reads as "every definition is the unversioned base". The pointers
 * are what elf_version_info_from computes by translating the file offsets WP-31
 * validated through the load bias WP-32 assigned. */
typedef struct elf_version_info {
	const char          *strtab;      /* .dynstr, for the version name strings */
	uint64_t             strsz;
	const Elf64_Versym  *versym;      /* .gnu.version, one per dynsym, or NULL */
	const unsigned char *verdef;      /* .gnu.version_d base, or NULL */
	uint32_t             verdefnum;   /* number of Elf64_Verdef records */
	const unsigned char *verneed;     /* .gnu.version_r base, or NULL */
	uint32_t             verneednum;  /* number of Elf64_Verneed records */
} elf_version_info;

/* Fill vi for a parsed and placed object. p must be the elf_ok parse of the
 * image m was mapped from. The .gnu.version{,_d,_r} file offsets p recorded are
 * translated to runtime addresses inside m's image; a table p did not find is
 * left NULL. Returns 0 on success, -1 on a malformed argument. An object with
 * no version tables yields an all-NULL vi, which is not an error. */
int elf_version_info_from(elf_version_info *vi, const elf_parsed *p,
                          const elf_mapping *m);

/* A lookup object paired with its version info. The matcher is handed a
 * candidate as an elf_lookup_object* (WP-35's currency) and finds its version
 * tables by locating the pairing whose .lo is that object. A scope's worth of
 * these is built once and read by every reference resolved against it. */
typedef struct elf_version_object {
	const elf_lookup_object *lo;
	elf_version_info         vi;
} elf_version_object;

/* The matcher state for resolving ONE reference. Built by elf_version_ctx_init
 * from the reference's own versym and verneed, then passed as the ctx of an
 * elf_version_matcher whose match is elf_version_match. req_name is the version
 * the reference demands, or NULL when the reference is unversioned. */
typedef struct elf_version_ctx {
	const char *req_name;    /* required version name, or NULL if unversioned */
	uint32_t    req_hash;    /* elf_sysv_hash(req_name) when req_name != NULL */
	int         req_hidden;  /* the reference's versym carried the hidden bit */
	const elf_version_object *objs;   /* the def-side pairing table */
	unsigned                  nobjs;
} elf_version_ctx;

/* Build the matcher ctx for the reference at symbol index ref_symidx in ref.
 * If ref carries a versym naming a version, that version's name and hash are
 * read from ref's verneed (the vernaux whose vna_other is the versym index) and
 * placed in c; out_file, when non-NULL, receives the needed file name. If the
 * reference is unversioned, c->req_name is left NULL. objs/nobjs is the pairing
 * table the resulting matcher searches for candidates. Returns 0 on success,
 * -1 if ref_symidx is out of range or the verneed does not name the index. */
int elf_version_ctx_init(elf_version_ctx *c, const elf_version_object *ref,
                         uint32_t ref_symidx,
                         const elf_version_object *objs, unsigned nobjs,
                         const char **out_file);

/* The elf_version_matcher.match callback (WP-35's seam). For the candidate
 * definition at symidx in object o, with ctx an elf_version_ctx*, returns
 *      1  the candidate satisfies the request and is the default (@@) binding,
 *      0  the candidate satisfies the request as a non-default (@) binding,
 *     -1  the candidate does not satisfy the request and must be skipped.
 * A candidate is a default match when its version name equals the requested one
 * and its versym lacks the hidden bit; an unversioned reference matches any
 * default definition and rejects hidden ones; a versioned reference with no
 * exact match accepts an unversioned base definition (as non-default) and
 * rejects every differently-named node. */
int elf_version_match(const elf_lookup_object *o, uint32_t symidx, void *ctx);

/* Convenience for a caller building an elf_version_matcher over an
 * elf_version_ctx: match is elf_version_match and ctx is the ctx. */
#define ELF_VERSION_MATCHER(ctxp) \
	((elf_version_matcher){ .match = elf_version_match, .ctx = (ctxp) })

/* The load-refusal check. Walk consumer's verneed and, for each required
 * version, find the loaded object it names (by soname among objs) and confirm
 * that object defines the version -- as a verdef node name or as a predecessor
 * in some node's verdaux chain, so a newer node satisfies a requirement for the
 * versions below it. On the first required NON-WEAK version that no such object
 * defines, returns -1, writes into msg the diagnostic a real ld.so gives --
 * "version `NAME' not found (required by CONSUMER_NAME)" -- and sets *bad_lib
 * (when non-NULL) to the name of the object that was supposed to provide it. A
 * weak requirement that is unmet is tolerated, counted in *weak_missing when
 * non-NULL, not refused, exactly as ld.so tolerates it. Returns 0 when every
 * non-weak requirement is satisfied. consumer_name is the string that appears
 * as the "required by" clause. */
int elf_version_check_needed(const elf_version_object *consumer,
                             const char *consumer_name,
                             const elf_version_object *objs, unsigned nobjs,
                             char *msg, size_t msgsz, const char **bad_lib,
                             unsigned *weak_missing);

/* Whether object o (as a version_object) defines version name. Exposed for the
 * check above and for tests: true when name is a verdef node name in o or a
 * predecessor named in a node's verdaux chain. An object with no verdef defines
 * no named version and returns 0. */
int elf_version_object_defines(const elf_version_object *o, const char *name,
                               uint32_t hash);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_VERSION_H */
