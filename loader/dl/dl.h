/* WP-38: the dl surface.
 *
 * Everything a program can ask its loader about itself at runtime, and the one
 * thing it can ask the loader to do at runtime: load another object. This is
 * dlopen, dlsym, dlvsym, dlclose, dlerror, dladdr, dladdr1, dlinfo and
 * dl_iterate_phdr, over the packages already delivered -- WP-31 parses, WP-32
 * maps, WP-33 walks the dependency graph, WP-34 relocates, WP-35 resolves,
 * WP-36 versions, WP-37 lays out TLS, WP-39 announces to a debugger.
 *
 * It also owns initialization order, which nothing before it did: the root's
 * DT_PREINIT_ARRAY first, then every object's DT_INIT and DT_INIT_ARRAY with
 * dependencies before dependents, and DT_FINI_ARRAY and DT_FINI in the exact
 * reverse on the way out. A dependency cycle makes "dependencies first"
 * unsatisfiable by definition; the tie-break is written down here and in
 * doc/decisions/0025-the-dl-surface-and-init-order.md rather than left to
 * whatever a traversal happens to do.
 *
 * The package owns no host policy. Reading a file is a function pointer the
 * caller supplies, so the certification drives the whole surface over both real
 * cross-linked objects and synthetic ones with no host in the loop. What it
 * does own is the object table: one record per loaded object, a reference count
 * per dlopen, and the promise that a dlclose that drops the last reference
 * gives back every byte the dlopen took -- the mapping, the file image, the
 * scope slot, the link-map node, the TLS module id.
 */
#ifndef ELFSYSV_LOADER_DL_H
#define ELFSYSV_LOADER_DL_H

#include <stddef.h>
#include <stdint.h>

#include "../elf/elf_parse.h"
#include "../elf/elf_types.h"
#include "../map/elf_map.h"
#include "../reloc/elf_reloc.h"
#include "../lookup/elf_lookup.h"
#include "../graph/elf_graph.h"
#include "../rdebug/rdebug.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The dlopen mode bits, at the values the platform's <dlfcn.h> gives them; a
 * program compiled against the vendor header passes these numbers in. */
#define RTLD_LAZY     0x00001
#define RTLD_NOW      0x00002
#define RTLD_BINDING_MASK 0x00003
#define RTLD_NOLOAD   0x00004
#define RTLD_DEEPBIND 0x00008
#define RTLD_GLOBAL   0x00100
#define RTLD_LOCAL    0x00000
#define RTLD_NODELETE 0x01000

/* The two pseudo-handles dlsym accepts in place of an object. */
#define RTLD_DEFAULT  ((void *) 0)
#define RTLD_NEXT     ((void *) -1L)

/* dlinfo requests, at the platform's values. */
#define RTLD_DI_LMID        1
#define RTLD_DI_LINKMAP     2
#define RTLD_DI_ORIGIN      6
#define RTLD_DI_TLS_MODID   9
#define RTLD_DI_TLS_DATA   10

#define DL_MAX_OBJECTS  ELF_RELOC_MAX_OBJ
#define DL_MAX_DEPS     32
#define DL_PATH_MAX     ELF_GRAPH_PATH_MAX
#define DL_NAME_MAX     ELF_GRAPH_NAME_MAX
#define DL_ERR_MAX      256

/* An initializer or finalizer: DT_INIT and DT_FINI take the argc/argv/envp
 * triple on this platform, and so does an array entry. */
typedef void (*dl_init_fn)(int argc, char **argv, char **envp);

/* One loaded object. A handle returned by dlopen is a pointer to one of these,
 * and the same pointer comes back for a second dlopen of the same file. */
typedef struct dl_object {
	int      in_use;
	unsigned slot;                 /* index in the table; stable while loaded */
	unsigned seq;                  /* load order, monotonic across the session */

	char     path[DL_PATH_MAX];    /* the file it was loaded from */
	char     soname[DL_NAME_MAX];  /* DT_SONAME, else the basename: its identity */

	int      refcount;             /* dlopen references; 0 for a startup object */
	int      is_startup;           /* from the initial graph; never unloaded */
	int      global;               /* RTLD_GLOBAL: in the global search scope */
	int      nodelete;             /* RTLD_NODELETE: dlclose never unmaps it */
	int      initialized;          /* its DT_INIT/DT_INIT_ARRAY have run */

	unsigned char *image;          /* the file bytes, owned; freed on unload */
	size_t         image_size;

	elf_parsed        parsed;
	elf_mapping       map;
	elf_reloc_object *ro;          /* its slot in the shared relocation scope */
	elf_lookup_object lo;          /* its symbol view, for WP-35 */
	struct link_map   lm;          /* its node in WP-39's rendezvous map */

	const Elf64_Phdr *phdr;        /* the mapped program headers */
	uint16_t          phnum;

	const Elf64_Dyn  *dyn;         /* the mapped dynamic array */

	unsigned dep[DL_MAX_DEPS];     /* slots of its direct DT_NEEDED objects */
	unsigned dep_count;
} dl_object;

/* How the loader reads a file. Supplied by the caller so this package makes no
 * host call of its own: read() fills *buf with a freshly allocated image of the
 * whole file and *size with its length, returning 0, or returns nonzero and
 * touches neither. release() gives that buffer back. dl_host_stdio() returns
 * the ordinary stdio implementation for a caller that wants one. */
typedef struct dl_host {
	int  (*read)(void *ctx, const char *path, unsigned char **buf, size_t *size);
	void (*release)(void *ctx, unsigned char *buf, size_t size);
	void *ctx;
} dl_host;

const dl_host *dl_host_stdio(void);

/* The loader's runtime state: the object table, the shared relocation scope,
 * the global search scope, and the last error. One of these is the process's;
 * the tests stand up their own so a case starts from nothing. */
typedef struct dl_state {
	dl_object       obj[DL_MAX_OBJECTS];
	unsigned        obj_count;     /* high-water mark over the table */
	unsigned        next_seq;

	elf_reloc_scope reloc;
	elf_graph_config search;       /* how a DT_NEEDED name is resolved */
	dl_host         host;

	/* argc/argv/envp as the initializers want them, set by dl_state_init. */
	int    argc;
	char **argv;
	char **envp;

	/* The last error, and whether dlerror has already consumed it. The real
	 * surface makes this per-thread; a single-threaded loader startup and the
	 * certification both read it here, and the thread-local carrier arrives
	 * with WP-42's thread work rather than being faked now. */
	char err[DL_ERR_MAX];
	int  err_pending;

	int  rdebug_wired;             /* the rendezvous is being kept current */
} dl_state;

/* Bring a state up empty. host may be NULL, which installs dl_host_stdio(). */
void dl_state_init(dl_state *st, const dl_host *host,
                   int argc, char **argv, char **envp);

/* Adopt an object the startup path already mapped and relocated: it enters the
 * table with refcount 0, is never unloaded, and joins the global scope. `ro` is
 * its slot in st->reloc, which the caller filled through elf_reloc_add. Returns
 * the object, or NULL when the table is full. */
dl_object *dl_adopt(dl_state *st, const char *path, elf_reloc_object *ro,
                    const elf_parsed *p, elf_mapping *m, unsigned char *image,
                    size_t image_size);

/* Record that `o` directly needs the object in slot `dep_slot`. The startup
 * path calls this as it walks WP-33's graph; dlopen calls it for what it
 * loads. The edges are what initialization order is computed over. */
int dl_add_dep(dl_object *o, unsigned dep_slot);

/* ---- the surface ------------------------------------------------------- */

/* Load the object at `path` and everything it needs, relocate it against the
 * world already loaded, run initializers in dependency order, and return a
 * handle. A second dlopen of an object already loaded returns the same handle
 * with its reference count raised and runs no initializer twice. Returns NULL
 * on failure with the reason readable through dl_error. RTLD_NOLOAD returns a
 * handle only if the object is already loaded. RTLD_NOW binds every PLT entry
 * before returning; RTLD_LAZY leaves them to the resolver. */
void *dl_open(dl_state *st, const char *path, int flags);

/* Drop one reference. On the last reference, run finalizers in the exact
 * reverse of the initialization order, unmap, and release everything the load
 * took -- unless the object is RTLD_NODELETE or came from startup. Returns 0,
 * or nonzero with dl_error set. */
int dl_close(dl_state *st, void *handle);

/* Resolve `name` starting from `handle`: an object handle searches that object
 * and its dependencies, RTLD_DEFAULT searches the global scope in load order,
 * and RTLD_NEXT searches the objects after `from` in load order -- which is
 * what an interposer calls to reach the function it wrapped. Returns the
 * runtime address, or NULL with dl_error set. A symbol that resolves to address
 * zero is reported as found through the out parameter form below. */
void *dl_sym(dl_state *st, void *handle, const char *name);

/* dlsym with an explicit referencing object, which RTLD_NEXT needs to know
 * where "next" starts from. `from` may be NULL for the other handle forms. */
void *dl_sym_from(dl_state *st, void *handle, const char *name,
                  const dl_object *from);

/* dlsym restricted to one version of the name. An object that defines the name
 * at another version does not answer. */
void *dl_vsym(dl_state *st, void *handle, const char *name, const char *version);

/* The last error, or NULL when none is pending. Reading it clears it, which is
 * the documented behaviour every caller depends on: two dlerror calls in a row
 * return the message and then NULL. */
const char *dl_error(dl_state *st);

/* What object an address lies in. Fills `info` and returns nonzero when the
 * address is inside a loaded object, zero when it is not -- which is dladdr's
 * inverted convention, kept so a program's own error handling is unchanged. */
typedef struct dl_info {
	const char *dli_fname;   /* path of the object containing addr */
	void       *dli_fbase;   /* its load base */
	const char *dli_sname;   /* name of the nearest preceding symbol, or NULL */
	void       *dli_saddr;   /* that symbol's address */
} dl_info;

int dl_addr(dl_state *st, const void *addr, dl_info *info);

/* dladdr1's extra_info requests. */
#define RTLD_DL_SYMENT   1
#define RTLD_DL_LINKMAP  2

/* dladdr, plus one of the two extras: the defining Elf64_Sym itself, or the
 * link_map node of the containing object. */
int dl_addr1(dl_state *st, const void *addr, dl_info *info,
             void **extra_info, int flags);

/* Ask an object about itself. Returns 0 on success, -1 with dl_error set. */
int dl_info_get(dl_state *st, void *handle, int request, void *p);

/* The phdr iteration an unwinder walks to find .eh_frame. The callback sees
 * every loaded object, in load order, and stops the walk by returning nonzero,
 * whose value is returned here. The struct is the platform's, field for field,
 * because libgcc's unwinder reads it by name. */
struct dl_phdr_info {
	uint64_t          dlpi_addr;       /* load bias */
	const char       *dlpi_name;       /* path, "" for the main object */
	const Elf64_Phdr *dlpi_phdr;       /* the mapped program headers */
	uint16_t          dlpi_phnum;
	unsigned long long dlpi_adds;      /* objects added since startup */
	unsigned long long dlpi_subs;      /* objects removed since startup */
	size_t            dlpi_tls_modid;  /* 0 when the object carries no PT_TLS */
	void             *dlpi_tls_data;   /* this thread's block, or NULL */
};

int dl_iterate_phdr(dl_state *st,
                    int (*cb)(struct dl_phdr_info *, size_t, void *),
                    void *data);

/* ---- initialization order ---------------------------------------------- */

/* The initialization order over the objects in `slots`: dependencies before
 * dependents, and where a cycle makes that impossible, the object reached
 * first from the lowest load order goes first. Writes at most `cap` slots into
 * `out` and returns how many it wrote. Exposed because the order is the
 * specification and the certification checks it directly, not only through
 * whether the right constructors ran. */
unsigned dl_init_order(const dl_state *st, const unsigned *slots, unsigned n,
                       unsigned *out, unsigned cap);

/* Run initializers over `slots` in that order: DT_PREINIT_ARRAY first and only
 * for a startup root, then DT_INIT and DT_INIT_ARRAY per object, marking each
 * initialized so it never runs twice. Returns the number of objects it ran. */
unsigned dl_run_init(dl_state *st, const unsigned *slots, unsigned n);

/* Run one object's DT_FINI_ARRAY (in reverse) and DT_FINI, and clear its
 * initialized mark. Finalizing a whole set in reverse initialization order is
 * dl_close's job; this is the single-object step it walks. */
void dl_run_fini(dl_state *st, dl_object *o);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_DL_H */
