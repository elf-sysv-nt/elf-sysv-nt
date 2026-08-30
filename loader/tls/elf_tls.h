/* WP-37: TLS in the loader.
 *
 * WP-30 established the thread pointer and the TCB: carrier C3 of DR-0003, a
 * runtime-owned word reached through gs:[NtTib.StackBase], with the psABI
 * variant II TCB at the pointer and `static_size` bytes of static block below
 * it. WP-34 computes the static-TLS relocations TPOFF64/DTPMOD64/DTPOFF64 but
 * stops at the arithmetic; standing up a live block is here.
 *
 * This package owns the runtime side of ELF TLS: sizing the static block from
 * the initial PT_TLS set (variant II, modules at aligned negative offsets below
 * the thread pointer, with a documented surplus reserved for dlopen late
 * arrivals), building the DTV a thread reads through tcbhead_t.dtv at head
 * offset 0x08, resolving __tls_get_addr for general-dynamic and local-dynamic
 * with a dynamic module's block allocated lazily on first access, handing the
 * initial-exec/local-exec offsets to WP-34's TPOFF relocation, and tearing the
 * whole per-thread arrangement down.
 *
 * The four models resolve against one layout. Initial-exec and local-exec are
 * the static offsets in elf_tls_module.tpoff: a reference costs one load of
 * tp + offset and no call. General-dynamic and local-dynamic go through
 * __tls_get_addr; for a static module it returns the same address the IE/LE
 * offset names, and for a dynamic one the lazily allocated block.
 *
 * The layout formula matches WP-34's assign_static_tls exactly for the initial
 * set -- each module's size rounded up to its alignment and stacked downward
 * from the pointer, the first module nearest -- so a TPOFF64 that WP-34 computed
 * (tp + addend + tpoff) reads the datum this package placed. The only thing
 * this package adds to the size is the surplus, which sits below the initial
 * modules and never moves their offsets.
 *
 * TLS descriptors (TLSDESC) are not implemented: WP-12's binutils refuses
 * GOTPC32_TLSDESC and TLSDESC_CALL at link (spike/ld-tls-relaxation), so no
 * object this toolchain produces carries a descriptor relocation for the loader
 * to service. The plan lists them "if the toolchain emits them"; it does not.
 */
#ifndef ELFSYSV_LOADER_TLS_H
#define ELFSYSV_LOADER_TLS_H

#include <stddef.h>
#include <stdint.h>

#include "../../runtime/tls/tls.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most modules one static layout carries, one-based; index 0 is unused so
 * a module id reads straight into the table. Matches WP-34's ELF_RELOC_MAX_OBJ,
 * since at most one PT_TLS per object arrives from one scope. */
#define ELF_TLS_MAX_MOD 64

/*
 * The static-TLS surplus, in bytes: space reserved below the initial modules so
 * a module dlopen'd later can still take a static (initial-exec) offset that a
 * thread created before it has room for. It is a tunable with a default, not a
 * constant -- getting it too small fails at run time inside a library the
 * program did not know it would load, which is the risk the plan flags. The
 * default is glibc's historical TLS_STATIC_SURPLUS, 64 + DL_NNS*100 with the
 * DL_NNS=16 namespaces glibc ships, i.e. 1664 bytes. elf_tls_state_init takes
 * an override; 0 selects this default.
 */
#define ELF_TLS_SURPLUS_DEFAULT ((uint64_t)1664)

/* The DTV slot value for a dynamic module whose block has not been allocated in
 * this thread yet -- glibc's TLS_DTV_UNALLOCATED. Distinct from NULL, which a
 * validly resolved but zero-address block could in principle be. */
#define ELF_TLS_DTV_UNALLOCATED ((void *)-1l)

/*
 * The argument __tls_get_addr is called with: the psABI's tls_index. The
 * compiler emits a pointer to one of these per general-dynamic or local-dynamic
 * reference and passes it in %rdi. ti_module is the one-based module id a
 * DTPMOD64 relocation filled; ti_offset is the datum's offset within the
 * module's block, what a DTPOFF64 filled.
 */
typedef struct elf_tls_index {
	unsigned long int ti_module;
	unsigned long int ti_offset;
} elf_tls_index;

/*
 * One TLS module: a PT_TLS segment's initialization image and layout. init_image
 * points at init_size bytes to copy to the front of a fresh block; the remaining
 * mem_size - init_size bytes are zero (.tbss). align is the segment's p_align,
 * a power of two. A module in the initial set is static: it lives in the TCB's
 * static block at tpoff (negative, below the pointer). A module that arrived
 * later is dynamic: its block is allocated per thread on first __tls_get_addr.
 */
typedef struct elf_tls_module {
	const void *init_image;    /* p_filesz bytes, or NULL when init_size == 0 */
	uint64_t    init_size;     /* PT_TLS p_filesz                             */
	uint64_t    mem_size;      /* PT_TLS p_memsz                              */
	uint64_t    align;         /* PT_TLS p_align, power of two, >= 1          */
	int         is_static;     /* in the static block vs. dynamically allocated */
	int64_t     tpoff;         /* static only: offset from tp, negative        */
} elf_tls_module;

/*
 * The loader's TLS state for one scope: the module table, the running static
 * layout, and the generation counter. Shared by every thread; the per-thread
 * part is the DTV, which lives in each thread's tcbhead_t.dtv. Adding or
 * removing a module bumps generation, which is how a thread created before a
 * dlopen learns its DTV is stale on the next access.
 */
typedef struct elf_tls_state {
	elf_tls_module mod[ELF_TLS_MAX_MOD + 1]; /* one-based; mod[0] unused */
	uint64_t nmod;              /* highest module id assigned            */
	uint64_t static_used;       /* bytes the initial modules occupy      */
	uint64_t static_size;       /* bytes below tp: modules + surplus, aligned */
	uint64_t surplus;           /* reserved for late initial-exec arrivals */
	uint64_t max_align;         /* largest module alignment seen         */
	uint64_t generation;        /* bumped on every add/remove            */
} elf_tls_state;

/*
 * The DTV entry, glibc's shape. dtv[0].counter is the generation this thread's
 * vector was last reconciled to; dtv[-1].counter is its length (module slots).
 * dtv[i].pointer.val is module i's block in this thread, or
 * ELF_TLS_DTV_UNALLOCATED. is_static distinguishes a slot pointing into the TCB
 * (never freed) from a dynamically allocated block teardown must free.
 */
typedef union elf_tls_dtv {
	uint64_t counter;
	struct {
		void *val;
		int   is_static;
	} pointer;
} elf_tls_dtv;

/* ---- the scope: sizing the static layout ------------------------------- */

/* Reset a state to empty and set the surplus. surplus == 0 selects
 * ELF_TLS_SURPLUS_DEFAULT. */
void elf_tls_state_init(elf_tls_state *s, uint64_t surplus);

/*
 * Add a module to the initial (startup) set. Assigns it the next module id and
 * a static offset below the thread pointer by the variant II rule WP-34 uses,
 * and accumulates the static size. Returns the one-based module id, or 0 if the
 * table is full. Call once per PT_TLS in the initial scope, in load order
 * (root first), then elf_tls_finish_layout.
 */
uint64_t elf_tls_add_initial(elf_tls_state *s, const elf_tls_module *m);

/*
 * Finalize the static block size: the initial modules' extent plus the surplus,
 * rounded up to the largest module alignment (at least 64, the TCB alignment).
 * After this, static_size is what elfsysv_tp_alloc must be given so the block
 * has room for every initial module and the surplus.
 */
void elf_tls_finish_layout(elf_tls_state *s);

/*
 * Register a module that arrived after startup (a dlopen). It is dynamic: it
 * gets a module id but no static offset, and its block is allocated per thread
 * on first access. Bumps generation. Returns the module id, or 0 if full.
 */
uint64_t elf_tls_add_dynamic(elf_tls_state *s, const elf_tls_module *m);

/* ---- per-thread: the DTV and the static block -------------------------- */

/*
 * Stand up one thread's TLS from a TCB elfsysv_tp_alloc gave `static_size`
 * bytes (== s->static_size). Copies each initial module's init image into its
 * static offset and zeroes the rest, allocates the DTV, points every static
 * slot at tp + tpoff, marks every dynamic slot unallocated, and installs the
 * DTV into tcbhead_t.dtv (head offset 0x08). Returns 0, or -1 on allocation
 * failure. Idempotent per TCB is not promised; call once, at thread creation.
 */
int elf_tls_thread_install(elf_tls_state *s, elfsysv_tcb_t *tcb);

/*
 * The resolver, taking the thread pointer explicitly. Reconciles the DTV at
 * tcbhead_t.dtv to the current generation (growing and refilling it if a module
 * was registered since), allocates module ti->ti_module's block if it is a
 * dynamic slot not yet present in this thread, and returns the datum address
 * (block + ti_offset). Returns NULL only on allocation failure. This is the
 * body __tls_get_addr wraps; the test drives it with the tp it owns, without
 * depending on the live %gs carrier.
 */
void *elf_tls_get_addr_at(elf_tls_state *s, void *tp, const elf_tls_index *ti);

/*
 * The psABI entry point. Resolves against the process's TLS state (set by the
 * most recent elf_tls_thread_install, or elf_tls_set_state) for the calling
 * thread, whose pointer it reads through carrier C3 (elfsysv_tp_get). General-
 * dynamic and local-dynamic references reach the datum through this.
 */
void *__tls_get_addr(const elf_tls_index *ti);

/* Point __tls_get_addr at the state it should resolve against. Set implicitly
 * by elf_tls_thread_install; exposed so a caller can bind it without installing
 * a thread. */
void elf_tls_set_state(elf_tls_state *s);

/*
 * Release one thread's TLS: free every dynamically allocated module block the
 * DTV holds (never the static slots, which live in the TCB) and free the DTV.
 * The TCB itself is freed by elfsysv_tp_free; this clears tcbhead_t.dtv first.
 */
void elf_tls_thread_teardown(elfsysv_tcb_t *tcb);

/* The initial-exec / local-exec offset for a module: tp + this reads the datum.
 * The same value WP-34 hands a TPOFF64. Zero if the id is not a static module. */
int64_t elf_tls_static_tpoff(const elf_tls_state *s, uint64_t modid);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_TLS_H */
