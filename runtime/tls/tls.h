/*
 * tls.h -- the thread pointer and its TCB (WP-30).
 *
 * DR-0003 settled the TLS model: the thread pointer is a word this runtime
 * owns, kept a fixed distance below the thread's stack base and reached through
 * %gs as gs:[NtTib.StackBase] then that offset -- carrier C3 of
 * spike/gs-thread-pointer, the shape Cygwin's _my_tls already uses. TLS
 * accesses fetch the pointer through that %gs chain and then address the block
 * relative to it; the block keeps its glibc layout, with tcbhead_t at the
 * thread pointer and the static TLS block at negative offsets. This unit
 * establishes that pointer at thread creation and re-establishes it wherever
 * the host can disturb it, and reads it back.
 *
 * Why the carrier is at the bottom of a runtime-owned stack, rather than the
 * spike stand-in's blind one page below StackBase, is the finding this package
 * re-measured against the real _my_tls; the reasoning and the numbers are in
 * README.md and DR-0021, and the probes that took them are in measure/. The
 * one-line version: the real _cygtls reservation is CYGTLS_PADSIZE = 0x3200
 * (not the stand-in's 0x1000), and on the main thread its words near StackBase
 * are Cygwin's live signal state, so a carrier word cannot be squatted there.
 * The runtime owns the word by owning the thread's stack and placing the word
 * at its committed floor, below both the _cygtls reservation and the working
 * stack.
 *
 * Interface note. The contract DR-0003 names is "a thread pointer, established
 * and readable," and it is the same whatever produces the carrier. In the
 * eventual forked elfsysv1.dll the carrier is a reserved field inside the
 * runtime's own _cygtls and the offset below StackBase is a build constant;
 * here, building against a real Cygwin whose _cygtls this runtime does not yet
 * own, the carrier is the floor of a runtime-allocated stack. elfsysv_tp_get()
 * and the tcbhead_t layout do not change between the two.
 */

#ifndef ELFSYSV_RUNTIME_TLS_H
#define ELFSYSV_RUNTIME_TLS_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

/* NtTib.StackBase, TEB offset 0x08, reached through %gs. Confirmed against the
 * running kernel by spike/gs-thread-pointer and re-confirmed by measure/. */
#define ELFSYSV_TEB_STACKBASE 0x08

/*
 * The stack a managed thread runs on, and the carrier offset within it.
 *
 * A managed thread runs on a stack this runtime allocates and fully commits,
 * so NtTib.StackBase is the top of that allocation and the whole allocation is
 * writable with no Windows guard-page growth to trip over. The carrier word --
 * the word that holds this thread's pointer -- sits sixteen bytes above the
 * allocation floor, i.e. ELFSYSV_TP_CARRIER_OFF below StackBase. That is below
 * the CYGTLS_PADSIZE (0x3200) _cygtls reservation at the top and far below the
 * thread's working rsp, which is the measured safe region (measure/README.md).
 *
 * The size is a runtime constant so the offset below StackBase is constant and
 * the read is a load, a subtract of an immediate, and a load. 512 KiB matches
 * a conventional thread stack and leaves the carrier 0x7fff0 below the working
 * region, which no acceptance-test thread comes near.
 */
#define ELFSYSV_TP_STACK_SIZE ((size_t)512 * 1024)
#define ELFSYSV_TP_CARRIER_OFF (ELFSYSV_TP_STACK_SIZE - 16)

/*
 * The glibc x86-64 TCB head, psABI variant II. The thread pointer points here;
 * tcb and self both read back as the thread pointer, the static TLS block sits
 * below at negative offsets, and stack_guard sits at the fixed 0x28 the
 * compiler emits __stack_chk_guard accesses against. Offsets are asserted in
 * tp.c against this layout so a field reorder cannot pass silently.
 */
typedef struct elfsysv_tcbhead {
	void *tcb;			/* 0x00 points to this struct (== TP)  */
	void *dtv;			/* 0x08 dynamic thread vector          */
	void *self;			/* 0x10 also == TP                     */
	int multiple_threads;		/* 0x18                                */
	int gscope_flag;		/* 0x1c                                */
	uintptr_t sysinfo;		/* 0x20                                */
	uintptr_t stack_guard;		/* 0x28 __stack_chk_guard              */
	uintptr_t pointer_guard;	/* 0x30 __pointer_chk_guard            */
	void *sigtls;			/* 0x38 the thread's signal record
					 * (elfsysv_sigtls_t), hung here by
					 * whoever creates the thread; NULL
					 * until then, and NULL means the
					 * signal package's fallback serves */
	uintptr_t reserved[2];		/* 0x40.. to a 0x50 head, glibc-shaped */
} elfsysv_tcbhead_t;

/*
 * A per-thread TCB this runtime allocates. The static TLS block precedes the
 * head in memory; `tp` is the thread pointer handed to the carrier and to the
 * compiler, and points at `head`. `block`/`block_size` are what teardown frees.
 */
typedef struct elfsysv_tcb {
	void *block;			/* base of the whole allocation        */
	size_t block_size;
	size_t static_size;		/* bytes of static TLS below the head  */
	void *tp;			/* the thread pointer == &head         */
	elfsysv_tcbhead_t *head;	/* == tp, typed                        */
} elfsysv_tcb_t;

/* ---- the carrier: gs:[StackBase] then a fixed offset (carrier C3) ------- */

static inline uint64_t elfsysv_gs_read(unsigned off)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:(%1), %0" : "=r"(v) : "r"((uint64_t)off));
	return v;
}

/* Read this thread's pointer. The hot path: fetch StackBase from %gs, step to
 * the carrier word, load it. Two loads and a subtract, the cost DR-0003 priced
 * at about 5.5 cycles against emulated TLS's 33.7. */
static inline void *elfsysv_tp_get(void)
{
	uint64_t base = elfsysv_gs_read(ELFSYSV_TEB_STACKBASE);
	return *(void *const *)(uintptr_t)(base - ELFSYSV_TP_CARRIER_OFF);
}

/* Write `tp` into the calling thread's carrier word. Used at establishment and
 * re-establishment; not on the read path. */
static inline void elfsysv_tp_set(void *tp)
{
	uint64_t base = elfsysv_gs_read(ELFSYSV_TEB_STACKBASE);
	*(void **)(uintptr_t)(base - ELFSYSV_TP_CARRIER_OFF) = tp;
}

/* ---- lifecycle --------------------------------------------------------- */

/* Cache CYGTLS_PADSIZE from the running Cygwin and assert the carrier offset
 * clears the real _cygtls reservation. Returns 0, or -1 with the reason in the
 * pointed-to string if the running _cygtls is larger than the safe region.
 * Idempotent. */
int elfsysv_tp_runtime_init(const char **why);

/* The cached CYGTLS_PADSIZE, for tests and diagnostics; 0 before init. */
uint64_t elfsysv_tp_padsize(void);

/* Allocate a TCB with `static_size` bytes of static TLS below the head, set the
 * self-pointers and stack guard, and return it. NULL on allocation failure. */
elfsysv_tcb_t *elfsysv_tp_alloc(size_t static_size);

/* Free a TCB allocated by elfsysv_tp_alloc. */
void elfsysv_tp_free(elfsysv_tcb_t *tcb);

/*
 * A managed thread: the runtime owns its stack and its TCB, and holds the
 * handles it must free after the thread is joined.
 */
typedef struct elfsysv_tp_thread {
	pthread_t th;
	void *stack;			/* the runtime-allocated stack     */
	size_t stack_size;
	elfsysv_tcb_t *tcb;
} elfsysv_tp_thread_t;

/*
 * Create a managed thread: allocate and fully commit its stack, allocate its
 * TCB with `static_size` static bytes, establish the thread pointer as the
 * first thing the thread does, then run `start(arg)`. The thread pointer is
 * readable through elfsysv_tp_get() for the whole of `start`. Returns 0 or an
 * errno-style code; on success `mt` holds the handles for elfsysv_tp_thread_join.
 */
int elfsysv_tp_thread_create(elfsysv_tp_thread_t *mt, size_t static_size,
			     void *(*start)(void *), void *arg);

/* Join a managed thread and release its stack and TCB. */
int elfsysv_tp_thread_join(elfsysv_tp_thread_t *mt, void **retval);

/* Re-establish the calling thread's pointer from its known TCB. The post-fork
 * child path calls this; a signal does not need it, because the carrier is
 * keyed to NtTib.StackBase, which Cygwin leaves unmoved on the alternate signal
 * stack (measure/README.md). */
void elfsysv_tp_reestablish(elfsysv_tcb_t *tcb);

#endif /* ELFSYSV_RUNTIME_TLS_H */
