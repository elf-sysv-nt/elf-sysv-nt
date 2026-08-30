/* WP-33: the object graph.
 *
 * A dynamically linked program is the root of a graph: it names the libraries
 * it needs in DT_NEEDED, each of those names more, and the loader has to find
 * every object, in the order a real ld.so finds it, before anything can be
 * relocated. This package walks that graph. Given a root ELF file and a search
 * configuration, it resolves each DT_NEEDED name to a file, follows the names
 * those files carry, and returns the load order -- the same list, in the same
 * order, that `ldd` prints.
 *
 * The order is not an implementation detail; it is the specification. It is
 * breadth-first over DT_NEEDED, an object's identity is its DT_SONAME, and a
 * name is resolved against DT_RPATH, LD_LIBRARY_PATH, DT_RUNPATH, the cache,
 * and the system default directories in exactly that precedence -- with the
 * standing difference that DT_RPATH is searched before LD_LIBRARY_PATH and
 * inherited by an object's dependencies, while DT_RUNPATH is searched after it
 * and applies only to the object that carries it. README.md is the account of
 * why each of those is where it is.
 *
 * Each object's dynamic section is read through WP-31's validated view: the
 * parser proves the dynamic array and string table in-bounds, and this package
 * reads DT_NEEDED, DT_SONAME, DT_RPATH and DT_RUNPATH out of that proven view
 * without trusting a byte the parser did not vouch for. Nothing here maps or
 * relocates; producing a running image from this order is WP-34 and beyond.
 */
#ifndef ELFSYSV_LOADER_ELF_GRAPH_H
#define ELFSYSV_LOADER_ELF_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ELF_GRAPH_NAME_MAX 256
#define ELF_GRAPH_PATH_MAX 4096

#include "ldso_cache.h"

/* The search configuration. A field left NULL or zero contributes nothing to
 * the search, so an empty config resolves only absolute and slash-bearing
 * names. Precedence among the sources is fixed by the loader and is not a
 * configurable order; this only supplies the contents of each source. */
typedef struct {
	const char *ld_library_path;       /* colon-separated, or NULL */
	const char *const *default_paths;  /* system directories, in order */
	size_t      default_count;
	const ldso_cache *cache;           /* consulted after RUNPATH, or NULL */
	uint32_t    want_flags;            /* LDSO_CACHE_F_* a cache hit must carry */

	const char *origin_override;       /* $ORIGIN for the root; NULL = derive */
	const char *lib_token;             /* $LIB expansion; NULL = "lib64" */
	const char *platform_token;        /* $PLATFORM; NULL = "x86_64" */
	unsigned    max_objects;           /* graph cap; 0 = a built-in default */
} elf_graph_config;

/* How a name was resolved, recorded so a diagnostic and a test can see which
 * source of the search each object came from. */
typedef enum {
	elf_src_none = 0,   /* not resolved (the object was not found) */
	elf_src_root,       /* the root object itself */
	elf_src_direct,     /* the name contained a slash and was used as given */
	elf_src_rpath,      /* DT_RPATH of the loader or one of its loaders */
	elf_src_ld_library_path,
	elf_src_runpath,    /* DT_RUNPATH of the loader */
	elf_src_cache,      /* the ldconfig cache */
	elf_src_default     /* a system default directory */
} elf_graph_source;

/* One node of the graph, in load order. obj[0] is always the root. */
typedef struct {
	char soname[ELF_GRAPH_NAME_MAX];   /* identity: DT_SONAME, else basename */
	char name[ELF_GRAPH_NAME_MAX];     /* the DT_NEEDED string that asked for it */
	char path[ELF_GRAPH_PATH_MAX];     /* resolved file, empty when not found */
	int  parent;                       /* index of the loader that first
	                                    * introduced it; -1 for the root */
	int  found;                        /* 1 resolved, 0 not found */
	elf_graph_source source;           /* which search source resolved it */

	/* Search directories this object contributes, already token-expanded and
	 * colon-joined. rpath is left empty when the object carries DT_RUNPATH,
	 * which is what makes RUNPATH non-inherited: only rpath is passed down the
	 * loader chain, and an object with a runpath contributes no rpath to it. */
	char origin[ELF_GRAPH_PATH_MAX];   /* dirname of path, for $ORIGIN */
	char rpath[ELF_GRAPH_PATH_MAX];    /* colon-joined, expanded; may be empty */
	char runpath[ELF_GRAPH_PATH_MAX];  /* colon-joined, expanded; may be empty */
	int  has_runpath;
} elf_graph_object;

/* The walked graph. obj is a dynamically grown array of count nodes in load
 * order; a node with found == 0 is a DT_NEEDED that no search resolved, kept
 * in place so the order matches what ldd prints for a broken graph. */
typedef struct {
	elf_graph_object *obj;
	unsigned count;
	unsigned cap;

	int  has_interp;                   /* the root's PT_INTERP, for ldd output */
	char interp[ELF_GRAPH_PATH_MAX];

	unsigned missing_count;            /* nodes with found == 0 */
	int  error;                        /* nonzero: the root could not be read
	                                    * or parsed and the graph is unusable */
	char errmsg[256];
} elf_graph;

/* Walk the graph rooted at the ELF file at root_path. On return the graph is
 * filled: g->error is zero and g->obj holds count nodes in load order, or
 * g->error is nonzero and g->errmsg says why the root could not be read. A
 * missing dependency is not an error -- it is a node with found == 0 -- so a
 * caller that wants ldd's exit status checks g->missing_count. Always call
 * elf_graph_free on a graph that elf_graph_build returned, error or not.
 * Returns 0 when the root was read and walked, nonzero when it was not. */
int elf_graph_build(const char *root_path, const elf_graph_config *cfg,
                    elf_graph *g);

/* Release everything the graph owns and zero it. Safe on a zeroed graph. */
void elf_graph_free(elf_graph *g);

/* A stable name for a source, for diagnostics and test output. */
const char *elf_graph_source_name(elf_graph_source s);

/* Expand the dynamic-string tokens $ORIGIN, $LIB and $PLATFORM (and their
 * ${...} forms) in src into dst, using the given values; a NULL value leaves
 * the corresponding token unexpanded. Returns 0 on success, nonzero if the
 * result would not fit in dstsz. Exposed because both the walker and its tests
 * need to agree on exactly what expansion means. */
int elf_graph_expand_tokens(const char *src, const char *origin,
                            const char *lib, const char *platform,
                            char *dst, size_t dstsz);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_ELF_GRAPH_H */
