/*
 * tp.c -- thread pointer establishment and the TCB (WP-30).
 *
 * Carrier C3 of DR-0003: the thread pointer is a word this runtime owns,
 * reached through gs:[NtTib.StackBase] then a fixed offset. The runtime owns
 * the word by owning the thread's stack -- a fully committed allocation whose
 * top NtTib.StackBase reports -- and placing the word at the stack's floor,
 * below the CYGTLS_PADSIZE _cygtls reservation and far below the working rsp.
 * The reasoning and the re-measurement behind that placement are in README.md,
 * DR-0021, and measure/.
 *
 * The TCB is the psABI variant II shape: tcbhead_t at the thread pointer, the
 * static TLS block below it at negative offsets, tcb and self reading back as
 * the pointer, stack_guard at the fixed 0x28.
 */

#define _GNU_SOURCE
#include "tls.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/cygwin.h>

/* The variant II head must match the offsets the compiler and the loader
 * assume. A reorder here would corrupt __stack_chk_guard silently. */
_Static_assert(offsetof(elfsysv_tcbhead_t, tcb) == 0x00, "tcb at TP+0");
_Static_assert(offsetof(elfsysv_tcbhead_t, self) == 0x10, "self at TP+0x10");
_Static_assert(offsetof(elfsysv_tcbhead_t, stack_guard) == 0x28, "guard at TP+0x28");
_Static_assert(offsetof(elfsysv_tcbhead_t, pointer_guard) == 0x30, "pguard at TP+0x30");

/* Cached CYGTLS_PADSIZE, the real _cygtls reservation this runtime must clear.
 * Zero until elfsysv_tp_runtime_init runs. */
static uint64_t g_padsize;

int elfsysv_tp_runtime_init(const char **why)
{
	uint64_t pad = (uint64_t)cygwin_internal(CW_CYGTLS_PADSIZE);

	if (pad == 0 || pad == (uint64_t)-1) {
		if (why) *why = "cygwin_internal(CW_CYGTLS_PADSIZE) gave no answer";
		return -1;
	}
	/* The carrier sits ELFSYSV_TP_CARRIER_OFF below StackBase; it must be
	 * below the _cygtls reservation [StackBase-pad, StackBase) and inside the
	 * owned stack. Both hold with a 512 KiB stack and pad = 0x3200, but the
	 * runtime checks rather than trusts, since a future Cygwin could grow the
	 * reservation. */
	if (ELFSYSV_TP_CARRIER_OFF <= pad) {
		if (why) *why = "CYGTLS_PADSIZE grew past the carrier offset; "
			"widen ELFSYSV_TP_STACK_SIZE or move the carrier";
		return -1;
	}
	if (ELFSYSV_TP_CARRIER_OFF >= ELFSYSV_TP_STACK_SIZE) {
		if (why) *why = "carrier offset does not fit the managed stack";
		return -1;
	}
	g_padsize = pad;
	return 0;
}

uint64_t elfsysv_tp_padsize(void) { return g_padsize; }

/* ---- the TCB ----------------------------------------------------------- */

#define TCB_ALIGN 64u

elfsysv_tcb_t *elfsysv_tp_alloc(size_t static_size)
{
	elfsysv_tcb_t *tcb;
	size_t stat = (static_size + (TCB_ALIGN - 1)) & ~(size_t)(TCB_ALIGN - 1);
	size_t total = stat + sizeof(elfsysv_tcbhead_t) + TCB_ALIGN;
	unsigned char *raw;
	uintptr_t tp;

	tcb = calloc(1, sizeof *tcb);
	if (!tcb)
		return NULL;
	raw = calloc(1, total);
	if (!raw) {
		free(tcb);
		return NULL;
	}
	/* Place the head so the thread pointer is TCB_ALIGN-aligned with `stat`
	 * bytes of static block below it. */
	tp = ((uintptr_t)raw + stat + (TCB_ALIGN - 1)) & ~(uintptr_t)(TCB_ALIGN - 1);
	tcb->block = raw;
	tcb->block_size = total;
	tcb->static_size = stat;
	tcb->tp = (void *)tp;
	tcb->head = (elfsysv_tcbhead_t *)tp;

	tcb->head->tcb = tcb->tp;	/* self-pointer at TP+0    */
	tcb->head->self = tcb->tp;	/* and at TP+0x10          */
	tcb->head->dtv = NULL;		/* the loader (WP-37) fills */
	tcb->head->stack_guard = 0x0;	/* set per thread below     */
	tcb->head->pointer_guard = 0x0;
	return tcb;
}

void elfsysv_tp_free(elfsysv_tcb_t *tcb)
{
	if (!tcb)
		return;
	free(tcb->block);
	free(tcb);
}

/* ---- establishment ----------------------------------------------------- */

void elfsysv_tp_reestablish(elfsysv_tcb_t *tcb)
{
	elfsysv_tp_set(tcb->tp);
}

/* What the trampoline needs to become a thread: its TCB and the real body. */
struct trampoline {
	elfsysv_tcb_t *tcb;
	void *(*start)(void *);
	void *arg;
};

/*
 * The managed thread's first frame. It establishes the thread pointer before
 * anything else runs, so elfsysv_tp_get() is valid for the whole of start().
 * A distinct per-thread stack_guard is stamped here so a corrupted TCB is
 * visible as a changed guard.
 */
static void *trampoline(void *p)
{
	struct trampoline tr = *(struct trampoline *)p;
	free(p);

	elfsysv_tp_set(tr.tcb->tp);
	tr.tcb->head->stack_guard = (uintptr_t)tr.tcb->tp ^ 0x5a5a5a5a5a5a5a5aull;
	tr.tcb->head->pointer_guard = (uintptr_t)tr.tcb->tp ^ 0xa5a5a5a5a5a5a5a5ull;
	return tr.start(tr.arg);
}

int elfsysv_tp_thread_create(elfsysv_tp_thread_t *mt, size_t static_size,
			     void *(*start)(void *), void *arg)
{
	pthread_attr_t attr;
	struct trampoline *tr;
	void *stack;
	int rc;

	if (g_padsize == 0)
		return EINVAL;		/* runtime_init has not run */

	/* A fully committed stack: MAP_ANONYMOUS pages are zero-filled and
	 * present, so the floor where the carrier lives is writable without the
	 * guard-page growth a reserve-only stack would need. */
	stack = mmap(NULL, ELFSYSV_TP_STACK_SIZE, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (stack == MAP_FAILED)
		return errno ? errno : ENOMEM;

	mt->tcb = elfsysv_tp_alloc(static_size);
	if (!mt->tcb) {
		munmap(stack, ELFSYSV_TP_STACK_SIZE);
		return ENOMEM;
	}
	tr = malloc(sizeof *tr);
	if (!tr) {
		elfsysv_tp_free(mt->tcb);
		munmap(stack, ELFSYSV_TP_STACK_SIZE);
		return ENOMEM;
	}
	tr->tcb = mt->tcb;
	tr->start = start;
	tr->arg = arg;

	pthread_attr_init(&attr);
	rc = pthread_attr_setstack(&attr, stack, ELFSYSV_TP_STACK_SIZE);
	if (rc == 0)
		rc = pthread_create(&mt->th, &attr, trampoline, tr);
	pthread_attr_destroy(&attr);

	if (rc != 0) {
		free(tr);
		elfsysv_tp_free(mt->tcb);
		munmap(stack, ELFSYSV_TP_STACK_SIZE);
		return rc;
	}
	mt->stack = stack;
	mt->stack_size = ELFSYSV_TP_STACK_SIZE;
	return 0;
}

int elfsysv_tp_thread_join(elfsysv_tp_thread_t *mt, void **retval)
{
	int rc = pthread_join(mt->th, retval);
	if (mt->stack)
		munmap(mt->stack, mt->stack_size);
	elfsysv_tp_free(mt->tcb);
	mt->stack = NULL;
	mt->tcb = NULL;
	return rc;
}
