/* WP-33: the loader's shared-object cache.
 *
 * The object graph resolves a DT_NEEDED name to a file by searching a list of
 * directories. Walking those directories on every lookup is what an ldconfig
 * cache exists to avoid: a directory scan is done once, ahead of time, into a
 * file that maps a soname straight to the path of the object that provides it.
 * This is that file's format, a reader for it, and the writer the ldconfig
 * tool uses to build it.
 *
 * The format is this project's own, not glibc's binary cache; the reasoning is
 * doc/decisions/0011-ldso-cache-format.md. It is read as untrusted input all
 * the same -- a cache is a file on disk that anything may have written -- so
 * the reader proves every offset in-bounds before it dereferences it and
 * rejects a truncated or self-inconsistent file with a diagnostic rather than
 * faulting, the same discipline WP-31 holds the ELF parser to.
 */
#ifndef ELFSYSV_LOADER_LDSO_CACHE_H
#define ELFSYSV_LOADER_LDSO_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On-disk layout, little-endian, offsets in bytes from the file start.
 *
 *   struct ldso_cache_header      (magic, version, counts)
 *   struct ldso_cache_entry[n]    sorted by soname, ascending strcmp order
 *   string table                  NUL-terminated soname and path strings
 *
 * Every string offset is relative to the start of the string table and names a
 * NUL-terminated string that lies wholly inside it. Entries are sorted so a
 * lookup is a binary search; the writer guarantees the order and the reader
 * verifies it, since a search over an unsorted table would miss.
 */

#define LDSO_CACHE_MAGIC   "elfsysv-ldcache"  /* 15 chars + NUL == 16 bytes */
#define LDSO_CACHE_VERSION 1

/* Entry flags. The cache records the object's ELF class and machine so a
 * lookup can refuse a 32-bit object where a 64-bit one is wanted; this project
 * is x86-64 only, but recording it keeps a mixed cache honest. */
#define LDSO_CACHE_F_ELF64   0x0001u  /* ELFCLASS64 */
#define LDSO_CACHE_F_X86_64  0x0002u  /* EM_X86_64  */

struct ldso_cache_header {
	char     magic[16];       /* LDSO_CACHE_MAGIC, NUL-padded */
	uint32_t version;         /* LDSO_CACHE_VERSION */
	uint32_t nentries;        /* number of ldso_cache_entry records */
	uint32_t strtab_off;      /* byte offset of the string table */
	uint32_t strtab_size;     /* byte length of the string table */
};

struct ldso_cache_entry {
	uint32_t soname_off;      /* into the string table */
	uint32_t path_off;        /* into the string table */
	uint32_t flags;           /* LDSO_CACHE_F_* */
	uint32_t reserved;        /* zero */
};

/* A cache opened for reading. The reader keeps the whole file in memory and
 * hands out pointers into it; the strings a lookup returns point into that
 * buffer and are valid until ldso_cache_close. */
typedef struct {
	unsigned char *image;     /* owned; freed by ldso_cache_close */
	size_t         size;
	const struct ldso_cache_header  *hdr;
	const struct ldso_cache_entry   *ent;   /* hdr->nentries records */
	const char                      *str;   /* string table base */
	uint32_t                         strsz;
} ldso_cache;

/* Reader result codes. */
typedef enum {
	ldso_cache_ok = 0,
	ldso_cache_err_open,      /* the file could not be opened or read */
	ldso_cache_err_size,      /* too small to hold what it claims */
	ldso_cache_err_magic,     /* wrong magic or version */
	ldso_cache_err_format,    /* an offset, count, or sort order is wrong */
	ldso_cache_err_nomem      /* allocation failed */
} ldso_cache_err;

/* Open and validate a cache file. On ldso_cache_ok, c is filled and owns the
 * mapping; on any other code c is left zeroed and need not be closed. msg, if
 * non-null, receives a short human-readable reason on failure. */
ldso_cache_err ldso_cache_open(const char *path, ldso_cache *c,
                               char *msg, size_t msglen);

/* Look a soname up. Returns the provider's path (a pointer into the cache
 * image, valid until close) or NULL if the soname is absent. If want_flags is
 * nonzero, only an entry whose flags include every set bit is returned. */
const char *ldso_cache_lookup(const ldso_cache *c, const char *soname,
                              uint32_t want_flags);

/* Release the mapping and zero c. Safe on a zeroed cache. */
void ldso_cache_close(ldso_cache *c);

/* A stable name for a reader code, for diagnostics and tests. */
const char *ldso_cache_err_name(ldso_cache_err code);

/* Building a cache. The builder accumulates (soname, path, flags) rows, then
 * serializes them sorted into the on-disk form. A soname added twice keeps the
 * row added last, which lets a caller scan directories in precedence order and
 * let a later directory win, or -- ldconfig's own rule -- keep the greater
 * version; the tool decides, the builder only records. */
typedef struct ldso_cache_builder ldso_cache_builder;

/* Create an empty builder, or NULL on allocation failure. */
ldso_cache_builder *ldso_cache_builder_new(void);

/* Record one provider. soname and path are copied. Returns 0 on success,
 * nonzero on allocation failure. A repeated soname replaces the earlier row. */
int ldso_cache_builder_add(ldso_cache_builder *b, const char *soname,
                           const char *path, uint32_t flags);

/* Number of distinct sonames recorded. */
size_t ldso_cache_builder_count(const ldso_cache_builder *b);

/* Serialize to a freshly malloc'd buffer in the on-disk format, sorted by
 * soname. On success *out and *out_size are set and 0 is returned; the caller
 * frees *out. Returns nonzero on allocation failure. */
int ldso_cache_builder_serialize(ldso_cache_builder *b,
                                 unsigned char **out, size_t *out_size);

/* Serialize and write to path atomically (write to a temporary beside it and
 * rename over it, so a reader never sees a half-written cache). Returns 0 on
 * success; on failure returns nonzero and, if msg is non-null, fills it. */
int ldso_cache_builder_write(ldso_cache_builder *b, const char *path,
                             char *msg, size_t msglen);

/* Free a builder. Safe on NULL. */
void ldso_cache_builder_free(ldso_cache_builder *b);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_LDSO_CACHE_H */
