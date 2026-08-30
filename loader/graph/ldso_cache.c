/* WP-33: shared-object cache reader and builder. See ldso_cache.h. */

#include "ldso_cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- reader ------------------------------------------------------------- */

const char *ldso_cache_err_name(ldso_cache_err code)
{
	switch (code) {
	case ldso_cache_ok:        return "ok";
	case ldso_cache_err_open:  return "open";
	case ldso_cache_err_size:  return "size";
	case ldso_cache_err_magic: return "magic";
	case ldso_cache_err_format:return "format";
	case ldso_cache_err_nomem: return "nomem";
	}
	return "?";
}

static void set_msg(char *msg, size_t n, const char *s)
{
	if (msg && n) { size_t k = strlen(s); if (k >= n) k = n - 1;
	                memcpy(msg, s, k); msg[k] = 0; }
}

static unsigned char *read_whole(const char *path, size_t *out_size)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	long n = ftell(f);
	if (n < 0) { fclose(f); return NULL; }
	if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
	unsigned char *buf = malloc((size_t)n ? (size_t)n : 1);
	if (!buf) { fclose(f); return NULL; }
	size_t got = fread(buf, 1, (size_t)n, f);
	fclose(f);
	if (got != (size_t)n) { free(buf); return NULL; }
	*out_size = (size_t)n;
	return buf;
}

/* True if off names a NUL-terminated string wholly inside the string table. */
static int str_in_bounds(const ldso_cache *c, uint32_t off)
{
	if (off >= c->strsz) return 0;
	const char *p = c->str + off;
	const char *end = c->str + c->strsz;
	while (p < end) { if (*p == 0) return 1; p++; }
	return 0;   /* ran off the end without a terminator */
}

ldso_cache_err ldso_cache_open(const char *path, ldso_cache *c,
                               char *msg, size_t msglen)
{
	memset(c, 0, sizeof *c);
	size_t size = 0;
	unsigned char *image = read_whole(path, &size);
	if (!image) { set_msg(msg, msglen, "cannot open or read cache file");
	              return ldso_cache_err_open; }

	if (size < sizeof(struct ldso_cache_header)) {
		free(image); set_msg(msg, msglen, "file smaller than header");
		return ldso_cache_err_size;
	}
	const struct ldso_cache_header *h = (const void *)image;
	if (memcmp(h->magic, LDSO_CACHE_MAGIC, sizeof h->magic) != 0) {
		free(image); set_msg(msg, msglen, "bad magic");
		return ldso_cache_err_magic;
	}
	if (h->version != LDSO_CACHE_VERSION) {
		free(image); set_msg(msg, msglen, "unsupported version");
		return ldso_cache_err_magic;
	}

	/* The entry array must fit between the header and the string table, and
	 * the string table must lie inside the file. All arithmetic is done in a
	 * 64-bit type so a 32-bit field cannot wrap a bounds check. */
	uint64_t ent_off  = sizeof(struct ldso_cache_header);
	uint64_t ent_size = (uint64_t)h->nentries * sizeof(struct ldso_cache_entry);
	uint64_t str_off  = h->strtab_off;
	uint64_t str_size = h->strtab_size;
	if (ent_off + ent_size > size ||
	    str_off < ent_off + ent_size ||
	    str_off > size || str_off + str_size > size) {
		free(image); set_msg(msg, msglen, "entry or string table out of range");
		return ldso_cache_err_format;
	}

	c->image = image;
	c->size  = size;
	c->hdr   = h;
	c->ent   = (const struct ldso_cache_entry *)(image + ent_off);
	c->str   = (const char *)(image + str_off);
	c->strsz = h->strtab_size;

	/* Every entry's offsets must be in range, and the sonames must be sorted
	 * ascending with no duplicate, or a binary search would be unsound. */
	const char *prev = NULL;
	for (uint32_t i = 0; i < h->nentries; i++) {
		if (!str_in_bounds(c, c->ent[i].soname_off) ||
		    !str_in_bounds(c, c->ent[i].path_off)) {
			ldso_cache_close(c);
			set_msg(msg, msglen, "entry string offset out of range");
			return ldso_cache_err_format;
		}
		const char *s = c->str + c->ent[i].soname_off;
		if (prev && strcmp(prev, s) >= 0) {
			ldso_cache_close(c);
			set_msg(msg, msglen, "entries not sorted or duplicated");
			return ldso_cache_err_format;
		}
		prev = s;
	}
	return ldso_cache_ok;
}

const char *ldso_cache_lookup(const ldso_cache *c, const char *soname,
                              uint32_t want_flags)
{
	if (!c || !c->hdr) return NULL;
	uint32_t lo = 0, hi = c->hdr->nentries;
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		const char *s = c->str + c->ent[mid].soname_off;
		int cmp = strcmp(soname, s);
		if (cmp == 0) {
			if ((c->ent[mid].flags & want_flags) == want_flags)
				return c->str + c->ent[mid].path_off;
			return NULL;
		}
		if (cmp < 0) hi = mid; else lo = mid + 1;
	}
	return NULL;
}

void ldso_cache_close(ldso_cache *c)
{
	if (!c) return;
	free(c->image);
	memset(c, 0, sizeof *c);
}

/* ---- builder ------------------------------------------------------------ */

struct bld_row { char *soname; char *path; uint32_t flags; };

struct ldso_cache_builder {
	struct bld_row *rows;
	size_t count, cap;
};

ldso_cache_builder *ldso_cache_builder_new(void)
{
	ldso_cache_builder *b = calloc(1, sizeof *b);
	return b;
}

void ldso_cache_builder_free(ldso_cache_builder *b)
{
	if (!b) return;
	for (size_t i = 0; i < b->count; i++) { free(b->rows[i].soname);
	                                        free(b->rows[i].path); }
	free(b->rows);
	free(b);
}

size_t ldso_cache_builder_count(const ldso_cache_builder *b)
{
	return b ? b->count : 0;
}

int ldso_cache_builder_add(ldso_cache_builder *b, const char *soname,
                           const char *path, uint32_t flags)
{
	/* A repeated soname replaces the earlier row rather than adding a second,
	 * so the last writer of a name wins and the serialized table has no
	 * duplicate for the reader's sort check to trip on. */
	for (size_t i = 0; i < b->count; i++) {
		if (strcmp(b->rows[i].soname, soname) == 0) {
			char *np = strdup(path);
			if (!np) return -1;
			free(b->rows[i].path);
			b->rows[i].path = np;
			b->rows[i].flags = flags;
			return 0;
		}
	}
	if (b->count == b->cap) {
		size_t nc = b->cap ? b->cap * 2 : 16;
		struct bld_row *nr = realloc(b->rows, nc * sizeof *nr);
		if (!nr) return -1;
		b->rows = nr; b->cap = nc;
	}
	char *ns = strdup(soname), *np = strdup(path);
	if (!ns || !np) { free(ns); free(np); return -1; }
	b->rows[b->count].soname = ns;
	b->rows[b->count].path   = np;
	b->rows[b->count].flags  = flags;
	b->count++;
	return 0;
}

static int row_cmp(const void *a, const void *b)
{
	const struct bld_row *x = a, *y = b;
	return strcmp(x->soname, y->soname);
}

int ldso_cache_builder_serialize(ldso_cache_builder *b,
                                 unsigned char **out, size_t *out_size)
{
	qsort(b->rows, b->count, sizeof *b->rows, row_cmp);

	/* String table: each soname then each path, NUL-terminated. Offsets are
	 * assigned in a first pass so the entry array can be filled in a second. */
	size_t strsz = 0;
	uint32_t *so_off = calloc(b->count ? b->count : 1, sizeof *so_off);
	uint32_t *pa_off = calloc(b->count ? b->count : 1, sizeof *pa_off);
	if (!so_off || !pa_off) { free(so_off); free(pa_off); return -1; }
	for (size_t i = 0; i < b->count; i++) {
		so_off[i] = (uint32_t)strsz; strsz += strlen(b->rows[i].soname) + 1;
		pa_off[i] = (uint32_t)strsz; strsz += strlen(b->rows[i].path) + 1;
	}

	size_t ent_off = sizeof(struct ldso_cache_header);
	size_t ent_sz  = b->count * sizeof(struct ldso_cache_entry);
	size_t str_off = ent_off + ent_sz;
	size_t total   = str_off + strsz;

	unsigned char *buf = calloc(total ? total : 1, 1);
	if (!buf) { free(so_off); free(pa_off); return -1; }

	struct ldso_cache_header *h = (void *)buf;
	memset(h->magic, 0, sizeof h->magic);
	memcpy(h->magic, LDSO_CACHE_MAGIC, sizeof LDSO_CACHE_MAGIC - 1);
	h->version     = LDSO_CACHE_VERSION;
	h->nentries    = (uint32_t)b->count;
	h->strtab_off  = (uint32_t)str_off;
	h->strtab_size = (uint32_t)strsz;

	struct ldso_cache_entry *e = (void *)(buf + ent_off);
	char *s = (char *)(buf + str_off);
	for (size_t i = 0; i < b->count; i++) {
		e[i].soname_off = so_off[i];
		e[i].path_off   = pa_off[i];
		e[i].flags      = b->rows[i].flags;
		e[i].reserved   = 0;
		strcpy(s + so_off[i], b->rows[i].soname);
		strcpy(s + pa_off[i], b->rows[i].path);
	}
	free(so_off); free(pa_off);
	*out = buf; *out_size = total;
	return 0;
}

int ldso_cache_builder_write(ldso_cache_builder *b, const char *path,
                             char *msg, size_t msglen)
{
	unsigned char *buf = NULL; size_t n = 0;
	if (ldso_cache_builder_serialize(b, &buf, &n) != 0) {
		set_msg(msg, msglen, "out of memory serializing cache");
		return -1;
	}
	/* Write to a sibling temp and rename over the target so a concurrent
	 * reader sees either the old cache or the new one, never a partial file. */
	size_t tlen = strlen(path) + 8;
	char *tmp = malloc(tlen);
	if (!tmp) { free(buf); set_msg(msg, msglen, "out of memory"); return -1; }
	snprintf(tmp, tlen, "%s.tmp", path);

	FILE *f = fopen(tmp, "wb");
	if (!f) { free(buf); free(tmp);
	          set_msg(msg, msglen, "cannot create temporary cache"); return -1; }
	size_t wrote = fwrite(buf, 1, n, f);
	int cerr = fclose(f);
	free(buf);
	if (wrote != n || cerr != 0) { remove(tmp); free(tmp);
	          set_msg(msg, msglen, "short write to temporary cache"); return -1; }
	if (rename(tmp, path) != 0) {
		/* Windows rename refuses to clobber; retry after removing target. */
		remove(path);
		if (rename(tmp, path) != 0) { remove(tmp); free(tmp);
			set_msg(msg, msglen, "cannot rename cache into place"); return -1; }
	}
	free(tmp);
	return 0;
}
