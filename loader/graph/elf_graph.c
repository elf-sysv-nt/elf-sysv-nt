/* WP-33: the object graph walker. See elf_graph.h. */

#include "elf_graph.h"
#include "../elf/elf_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The two dynamic tags WP-31's validated view does not surface on their own.
 * They are read back out of the proven dynamic array here; see read_rpaths. */
#define DT_NULL_TAG     0
#define DT_RPATH_TAG    15
#define DT_RUNPATH_TAG  29

struct elf64_dyn { int64_t d_tag; uint64_t d_un; };  /* 16 bytes, ELF64 */

const char *elf_graph_source_name(elf_graph_source s)
{
	switch (s) {
	case elf_src_none:             return "none";
	case elf_src_root:             return "root";
	case elf_src_direct:           return "direct";
	case elf_src_rpath:            return "rpath";
	case elf_src_ld_library_path:  return "LD_LIBRARY_PATH";
	case elf_src_runpath:          return "runpath";
	case elf_src_cache:            return "cache";
	case elf_src_default:          return "default";
	}
	return "?";
}

/* ---- small helpers ------------------------------------------------------ */

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

/* basename and dirname that do not modify their input and write into a caller
 * buffer. dirname of a path with no slash is ".". */
static void path_basename(const char *p, char *dst, size_t n)
{
	const char *s = strrchr(p, '/');
	s = s ? s + 1 : p;
	snprintf(dst, n, "%s", s);
}

static void path_dirname(const char *p, char *dst, size_t n)
{
	const char *s = strrchr(p, '/');
	if (!s) { snprintf(dst, n, "."); return; }
	if (s == p) { snprintf(dst, n, "/"); return; }
	size_t len = (size_t)(s - p);
	if (len >= n) len = n - 1;
	memcpy(dst, p, len);
	dst[len] = 0;
}

/* Join dir and file with a single slash into dst. Returns 0, or -1 if the
 * result would not fit. */
static int path_join(const char *dir, const char *file, char *dst, size_t n)
{
	int k;
	if (dir[0] && dir[strlen(dir) - 1] == '/')
		k = snprintf(dst, n, "%s%s", dir, file);
	else
		k = snprintf(dst, n, "%s/%s", dir, file);
	return (k < 0 || (size_t)k >= n) ? -1 : 0;
}

/* True if path is a regular file that opens as a 64-bit x86-64 ELF object.
 * A candidate that exists but is the wrong class or machine returns false, so
 * the search steps past it rather than stopping on it -- glibc's behaviour. */
static int is_usable_elf(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	unsigned char e[20];
	size_t got = fread(e, 1, sizeof e, f);
	fclose(f);
	if (got < sizeof e) return 0;
	if (e[0] != 0x7f || e[1] != 'E' || e[2] != 'L' || e[3] != 'F') return 0;
	if (e[4] != 2 /*ELFCLASS64*/ || e[5] != 1 /*ELFDATA2LSB*/) return 0;
	uint16_t machine = (uint16_t)(e[18] | (e[19] << 8));
	if (machine != 62 /*EM_X86_64*/) return 0;
	return 1;
}

/* ---- dynamic-string token expansion ------------------------------------- */

/* Match a token at src, either $NAME or ${NAME}. On a match, returns the
 * length consumed from src and points *val at the replacement; otherwise 0. */
static size_t match_token(const char *src, const char *origin,
                          const char *lib, const char *platform,
                          const char **val)
{
	struct { const char *name; const char *v; } tk[3] = {
		{ "ORIGIN", origin }, { "LIB", lib }, { "PLATFORM", platform },
	};
	int braced = (src[1] == '{');
	const char *nm = src + (braced ? 2 : 1);
	for (int i = 0; i < 3; i++) {
		size_t l = strlen(tk[i].name);
		if (strncmp(nm, tk[i].name, l) != 0) continue;
		const char *after = nm + l;
		if (braced) { if (*after != '}') continue; after++; }
		if (!tk[i].v) return 0;          /* no value supplied; leave literal */
		*val = tk[i].v;
		return (size_t)(after - src);
	}
	return 0;
}

int elf_graph_expand_tokens(const char *src, const char *origin,
                            const char *lib, const char *platform,
                            char *dst, size_t dstsz)
{
	size_t o = 0;
	for (const char *p = src; *p; ) {
		if (*p == '$') {
			const char *val = NULL;
			size_t used = match_token(p, origin, lib, platform, &val);
			if (used) {
				size_t vl = strlen(val);
				if (o + vl >= dstsz) return -1;
				memcpy(dst + o, val, vl); o += vl; p += used;
				continue;
			}
		}
		if (o + 1 >= dstsz) return -1;
		dst[o++] = *p++;
	}
	dst[o] = 0;
	return 0;
}

/* ---- reading the search paths out of the dynamic section ---------------- */

static int try_dir(const char *dir, const char *name, char *out, size_t n);

/* Append one already-expanded directory to a colon-joined list buffer, adding
 * a ':' separator when the buffer is non-empty. Silently drops a directory
 * that would overflow the buffer. */
static void join_append(char *buf, size_t n, const char *dir)
{
	size_t have = strlen(buf), add = strlen(dir);
	size_t need = have + (have ? 1 : 0) + add + 1;
	if (need > n) return;
	if (have) buf[have++] = ':';
	memcpy(buf + have, dir, add);
	buf[have + add] = 0;
}

/* Expand each element of a colon-separated raw list and append it, expanded,
 * to a colon-joined buffer. An empty element denotes the current directory. */
static void join_list_expanded(char *buf, size_t n, const char *list,
                               const char *origin, const char *lib,
                               const char *platform)
{
	char elem[ELF_GRAPH_PATH_MAX], exp[ELF_GRAPH_PATH_MAX];
	size_t k = 0;
	for (const char *p = list; ; p++) {
		if (*p == ':' || *p == 0) {
			elem[k] = 0;
			const char *in = elem[0] ? elem : ".";
			if (elf_graph_expand_tokens(in, origin, lib, platform,
			                            exp, sizeof exp) == 0)
				join_append(buf, n, exp);
			k = 0;
			if (*p == 0) break;
		} else if (k + 1 < sizeof elem) {
			elem[k++] = *p;
		}
	}
}

/* Try each directory of a colon-joined (already-expanded) list against name;
 * on the first usable-ELF hit copy the full path into out and return 1. */
static int search_joined(const char *list, const char *name,
                         char *out, size_t n)
{
	char dir[ELF_GRAPH_PATH_MAX];
	size_t k = 0;
	for (const char *p = list; ; p++) {
		if (*p == ':' || *p == 0) {
			dir[k] = 0;
			if (k && try_dir(dir, name, out, n)) return 1;
			k = 0;
			if (*p == 0) break;
		} else if (k + 1 < sizeof dir) {
			dir[k++] = *p;
		}
	}
	return 0;
}

/* Read DT_RPATH and DT_RUNPATH out of the validated dynamic array and fill the
 * object's expanded rpath/runpath directory lists. WP-31 has already proven
 * the dynamic array and the string table lie inside the image and that strsz
 * bounds the table; this only has to bound each RPATH/RUNPATH value's own
 * offset against strsz and confirm its string terminates inside the table
 * before trusting it. Returns nothing: a malformed value is skipped, which is
 * the same conservative choice the parser makes for a name it cannot vouch
 * for, and leaves that search source empty rather than reading out of bounds. */
static void read_rpaths(const unsigned char *image, const elf_parsed *p,
                        elf_graph_object *o, const char *lib,
                        const char *platform)
{
	if (!p->has_dynamic || !p->has_strtab) return;
	const char *strtab = (const char *)image + p->strtab_off;
	const struct elf64_dyn *dyn =
		(const struct elf64_dyn *)(image + p->dyn_off);

	for (uint64_t i = 0; i < p->dyn_count; i++) {
		int64_t tag = dyn[i].d_tag;
		if (tag != DT_RPATH_TAG && tag != DT_RUNPATH_TAG) continue;
		uint64_t off = dyn[i].d_un;
		if (off >= p->strsz) continue;                 /* offset out of table */
		/* Confirm the string is terminated within the table. */
		uint64_t j = off; int ok = 0;
		for (; j < p->strsz; j++) { if (strtab[j] == 0) { ok = 1; break; } }
		if (!ok) continue;
		const char *val = strtab + off;
		if (tag == DT_RPATH_TAG) {
			join_list_expanded(o->rpath, sizeof o->rpath, val,
			                   o->origin, lib, platform);
		} else {
			o->has_runpath = 1;
			join_list_expanded(o->runpath, sizeof o->runpath, val,
			                   o->origin, lib, platform);
		}
	}

	/* DT_RPATH is ignored on an object that also carries DT_RUNPATH; dropping
	 * it here is what stops it being inherited by this object's dependents. */
	if (o->has_runpath) o->rpath[0] = 0;
}

/* ---- object array ------------------------------------------------------- */

static elf_graph_object *new_object(elf_graph *g)
{
	if (g->count == g->cap) {
		unsigned nc = g->cap ? g->cap * 2 : 16;
		elf_graph_object *no = realloc(g->obj, (size_t)nc * sizeof *no);
		if (!no) return NULL;
		g->obj = no; g->cap = nc;
	}
	elf_graph_object *o = &g->obj[g->count++];
	memset(o, 0, sizeof *o);
	o->parent = -1;
	return o;
}

/* Read path, parse it, and fill an object's identity and search paths from it:
 * DT_SONAME (or the file's basename when it has none), origin, and the
 * expanded DT_RPATH/DT_RUNPATH lists. o->path and o->origin must already be
 * set. Returns 0 on a clean parse, -1 if the file could not be read or parsed
 * -- in which case the object keeps its basename identity and contributes no
 * dependencies, the conservative reading of an object whose dynamic section
 * cannot be trusted. */
static int fill_from_file(elf_graph_object *o, const char *lib,
                          const char *platform)
{
	path_basename(o->path, o->soname, sizeof o->soname);  /* default identity */

	size_t size = 0;
	unsigned char *image = read_whole(o->path, &size);
	if (!image) return -1;

	elf_parsed p; elf_diag d;
	if (elf_parse(image, size, &p, &d) != elf_ok) { free(image); return -1; }

	if (p.has_soname && p.has_strtab) {
		const char *s = (const char *)image + p.strtab_off + p.soname;
		snprintf(o->soname, sizeof o->soname, "%s", s);
	}
	read_rpaths(image, &p, o, lib, platform);
	free(image);
	return 0;
}

/* ---- name resolution ---------------------------------------------------- */

/* Try dir/name; on a usable ELF hit copy the full path into out and return 1. */
static int try_dir(const char *dir, const char *name, char *out, size_t n)
{
	char cand[ELF_GRAPH_PATH_MAX];
	if (path_join(dir, name, cand, sizeof cand) != 0) return 0;
	if (!is_usable_elf(cand)) return 0;
	snprintf(out, n, "%s", cand);
	return 1;
}

/* Resolve one DT_NEEDED name on behalf of loader li, filling out->path,
 * out->found and out->source. out->name and out->origin are already set.
 * The precedence is the loader's: DT_RPATH of the loader and its loader chain,
 * then LD_LIBRARY_PATH, then the loader's own DT_RUNPATH, then the cache, then
 * the system default directories. A name containing a slash skips the search
 * and is used as given (after token expansion). */
static void resolve(elf_graph *g, const elf_graph_config *cfg, int li,
                    elf_graph_object *out, const char *lib, const char *plat)
{
	const char *name = out->name;

	if (strchr(name, '/')) {
		char cand[ELF_GRAPH_PATH_MAX];
		if (elf_graph_expand_tokens(name, g->obj[li].origin, lib, plat,
		                            cand, sizeof cand) == 0 &&
		    is_usable_elf(cand)) {
			snprintf(out->path, sizeof out->path, "%s", cand);
			out->found = 1; out->source = elf_src_direct;
		}
		return;
	}

	/* DT_RPATH: the loader's own, then each ancestor's, up the chain. An
	 * ancestor that carries DT_RUNPATH contributed no rpath (read_rpaths
	 * cleared it), which is precisely why RUNPATH does not reach here. */
	for (int x = li; x >= 0; x = g->obj[x].parent) {
		if (g->obj[x].rpath[0] &&
		    search_joined(g->obj[x].rpath, name, out->path, sizeof out->path)) {
			out->found = 1; out->source = elf_src_rpath; return;
		}
	}

	/* LD_LIBRARY_PATH, expanded relative to the loading object. */
	if (cfg->ld_library_path && cfg->ld_library_path[0]) {
		char list[ELF_GRAPH_PATH_MAX]; list[0] = 0;
		join_list_expanded(list, sizeof list, cfg->ld_library_path,
		                   g->obj[li].origin, lib, plat);
		if (search_joined(list, name, out->path, sizeof out->path)) {
			out->found = 1; out->source = elf_src_ld_library_path; return;
		}
	}

	/* DT_RUNPATH of the loader only -- never inherited from an ancestor. */
	if (g->obj[li].runpath[0] &&
	    search_joined(g->obj[li].runpath, name, out->path, sizeof out->path)) {
		out->found = 1; out->source = elf_src_runpath; return;
	}

	/* The ldconfig cache. */
	if (cfg->cache) {
		const char *hit = ldso_cache_lookup(cfg->cache, name, cfg->want_flags);
		if (hit && is_usable_elf(hit)) {
			snprintf(out->path, sizeof out->path, "%s", hit);
			out->found = 1; out->source = elf_src_cache; return;
		}
	}

	/* System default directories. */
	for (size_t k = 0; k < cfg->default_count; k++)
		if (try_dir(cfg->default_paths[k], name, out->path, sizeof out->path)) {
			out->found = 1; out->source = elf_src_default; return;
		}

	/* Nothing resolved it: leave found == 0. */
}

/* True if name is already represented in the graph, by the DT_NEEDED string
 * that introduced a node or by a resolved node's DT_SONAME. */
static int already_present(const elf_graph *g, const char *name)
{
	for (unsigned k = 0; k < g->count; k++) {
		if (strcmp(g->obj[k].name, name) == 0) return 1;
		if (g->obj[k].found && strcmp(g->obj[k].soname, name) == 0) return 1;
	}
	return 0;
}

/* ---- the walk ----------------------------------------------------------- */

static void set_error(elf_graph *g, const char *m)
{
	g->error = 1;
	snprintf(g->errmsg, sizeof g->errmsg, "%s", m);
}

int elf_graph_build(const char *root_path, const elf_graph_config *cfg,
                    elf_graph *g)
{
	memset(g, 0, sizeof *g);
	const char *lib  = cfg->lib_token      ? cfg->lib_token      : "lib64";
	const char *plat = cfg->platform_token ? cfg->platform_token : "x86_64";
	unsigned cap = cfg->max_objects ? cfg->max_objects : 256;

	size_t rsz = 0;
	unsigned char *rimg = read_whole(root_path, &rsz);
	if (!rimg) { set_error(g, "cannot open or read the root object"); return -1; }

	elf_parsed rp; elf_diag rd;
	if (elf_parse(rimg, rsz, &rp, &rd) != elf_ok) {
		free(rimg);
		char m[256];
		snprintf(m, sizeof m, "root object rejected by the parser: %.200s",
		         rd.msg);
		set_error(g, m);
		return -1;
	}

	elf_graph_object *root = new_object(g);
	if (!root) { free(rimg); set_error(g, "out of memory"); return -1; }
	root->parent = -1;
	root->found  = 1;
	root->source = elf_src_root;

	/* An absolute path for the root gives $ORIGIN a stable base. */
	char *abs = realpath(root_path, NULL);
	snprintf(root->path, sizeof root->path, "%s", abs ? abs : root_path);
	free(abs);
	path_basename(root->path, root->name, sizeof root->name);
	if (cfg->origin_override)
		snprintf(root->origin, sizeof root->origin, "%s", cfg->origin_override);
	else
		path_dirname(root->path, root->origin, sizeof root->origin);

	/* Root identity and search paths, and its PT_INTERP for ldd output. */
	if (rp.has_soname && rp.has_strtab) {
		const char *s = (const char *)rimg + rp.strtab_off + rp.soname;
		snprintf(root->soname, sizeof root->soname, "%s", s);
	} else {
		path_basename(root->path, root->soname, sizeof root->soname);
	}
	read_rpaths(rimg, &rp, root, lib, plat);
	if (rp.has_interp) {
		const char *ip = (const char *)rimg + rp.interp_off;
		/* NUL-terminated within its own span, per WP-31's bounds. */
		size_t max = rp.interp_size < sizeof g->interp
		             ? (size_t)rp.interp_size : sizeof g->interp - 1;
		size_t k = 0; while (k < max && ip[k]) { g->interp[k] = ip[k]; k++; }
		g->interp[k] = 0;
		g->has_interp = 1;
	}
	free(rimg);

	/* Breadth-first over the array: expanding object i appends its
	 * dependencies past the end, and the loop only reaches them once every
	 * object at i's level has been expanded. That array-and-index walk is the
	 * breadth-first order, and it is the order ldd prints. */
	for (unsigned i = 0; i < g->count; i++) {
		if (!g->obj[i].found) continue;

		size_t sz = 0;
		unsigned char *img = read_whole(g->obj[i].path, &sz);
		if (!img) continue;
		elf_parsed p; elf_diag d;
		if (elf_parse(img, sz, &p, &d) != elf_ok) { free(img); continue; }

		for (unsigned n = 0; n < p.needed_count; n++) {
			if (!(p.has_strtab)) break;
			const char *nm = (const char *)img + p.strtab_off + p.needed[n];
			if (already_present(g, nm)) continue;
			if (g->count >= cap) break;

			elf_graph_object *child = new_object(g);
			if (!child) break;
			child->parent = (int)i;
			snprintf(child->name, sizeof child->name, "%s", nm);

			resolve(g, cfg, (int)i, child, lib, plat);
			if (child->found) {
				path_dirname(child->path, child->origin, sizeof child->origin);
				fill_from_file(child, lib, plat);
			} else {
				g->missing_count++;
			}
		}
		free(img);
	}
	return 0;
}

void elf_graph_free(elf_graph *g)
{
	if (!g) return;
	free(g->obj);
	memset(g, 0, sizeof *g);
}
