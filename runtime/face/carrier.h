/*
 * carrier.h -- the hosted DR-0021 carrier (WP-27 milestone 8).
 *
 * WP-30 established the carrier on managed threads: the word is the floor
 * of a stack the runtime mmaps, ELFSYSV_TP_CARRIER_OFF below StackBase
 * (runtime/tls/tls.h).  On a thread the faced runtime itself creates there
 * is no mmapped stack, and DR-0021 reserved the other backing for exactly
 * this case: a field inside the forked runtime's own _cygtls, the last
 * data member, so its distance below NtTib.StackBase is one build constant
 * of the DLL.  This unit is that backing's client side.
 *
 * The offset is not baked here.  The DLL knows it (__CYGTLS_PADSIZE__ -
 * sizeof (_cygtls) + 8) and hands out the calling thread's carrier address
 * through cygwin_internal (CW_ELFSYSV_CARRIER); init takes one probed
 * address, derives the offset against this thread's StackBase, and every
 * later read is the C3 chain -- gs:[StackBase], subtract, load -- the same
 * shape and cost as the managed carrier.  Trusting the probe rather than a
 * second copy of the constant means a DLL whose _cygtls grew cannot be
 * read at a stale offset: init on that DLL yields that DLL's offset.
 *
 * The crossing is the caller's.  This unit never calls the DLL; the test
 * (or the veneer above it) makes the cygwin_internal crossing and passes
 * the result in, so the unit compiles without the face, links -nostdlib,
 * and can be certified with fabricated addresses.
 *
 * Thread creation establishes the carrier for every thread the runtime
 * creates -- the shape the veneer's pthread_create inherits (F8).
 * elfsysv_carrier_thread_create wraps a start routine so the new thread
 * writes its own thread pointer into its own carrier before the body
 * runs; the launch record is the caller's and must outlive thread start.
 */

#ifndef ELFSYSV_FACE_CARRIER_H
#define ELFSYSV_FACE_CARRIER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NtTib.StackBase through %gs, as runtime/tls/tls.h reads it. */
#ifndef ELFSYSV_TEB_STACKBASE
#define ELFSYSV_TEB_STACKBASE 0x08
#endif

static inline uint64_t elfsysv_carrier_gs_read(unsigned off)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:(%1), %0"
			     : "=r"(v) : "r"((uint64_t)off));
	return v;
}

/*
 * Derive and latch the carrier offset from one probed address --
 * cygwin_internal (CW_ELFSYSV_CARRIER) on the calling thread -- with
 * `padsize` the DLL's CW_CYGTLS_PADSIZE.  The offset must land inside
 * (0, padsize], aligned to the word, or init refuses with the reason in
 * *why.  Idempotent; a second probe that disagrees with the latched
 * offset also refuses, because two offsets means two _cygtls layouts in
 * one process.  Returns 0 on success, -1 on refusal.
 */
int elfsysv_carrier_init(uint64_t carrier_addr, uint64_t padsize,
			 const char **why);

/* The latched offset below StackBase; 0 before init. */
uint64_t elfsysv_carrier_offset(void);

/* Read the calling thread's pointer through the hosted carrier. */
static inline void *elfsysv_carrier_get_at(uint64_t off)
{
	uint64_t base = elfsysv_carrier_gs_read(ELFSYSV_TEB_STACKBASE);
	return *(void *const *)(uintptr_t)(base - off);
}

/* The established read and write, against the latched offset. */
void *elfsysv_carrier_get(void);
void elfsysv_carrier_set(void *tp);

/* ---- thread creation: the carrier established before the body ------- */

/* The faced pthread_create, as an ELF caller reaches it: System V,
 * straight at the export. */
typedef int (__attribute__((sysv_abi)) *elfsysv_carrier_create_fn)(
	void **thread, const void *attr, void *(*start)(void *), void *arg);

/* One launch in flight.  Caller-owned; must outlive the created thread's
 * start.  `tp` is the thread pointer the new thread will carry. */
typedef struct elfsysv_carrier_launch {
	void *(*start)(void *);
	void *arg;
	void *tp;
	/* what the shim observed, for the certification: the carrier word
	 * as the new thread found it, before establishment. */
	void *found;
	int established;
} elfsysv_carrier_launch_t;

/*
 * Create a thread through `create`, establishing `launch->tp` in the new
 * thread's own carrier before `launch->start` runs.  Requires init.
 * Returns what `create` returns, or -1 if the carrier is not initialized.
 */
int elfsysv_carrier_thread_create(elfsysv_carrier_create_fn create,
				  void **thread, const void *attr,
				  elfsysv_carrier_launch_t *launch);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_FACE_CARRIER_H */
