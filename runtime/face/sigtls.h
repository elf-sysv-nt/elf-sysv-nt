/*
 * sigtls.h -- the per-thread signal record, reached through the carrier's
 * TCB (WP-27 milestone 8).
 *
 * The signal package split its blocked mask and alternate stack per thread
 * and left the resolution to a provider (runtime/signal/signal.h,
 * elf_sig_set_tls_provider).  This unit is the faced runtime's provider:
 * the calling thread's record hangs on the sigtls slot of its TCB head,
 * and the TCB is found through the DR-0021 carrier -- gs:[StackBase],
 * subtract the latched offset, load.  A thread with no carrier or no
 * record answers NULL and the package's embedded fallback serves, which
 * is how the main thread runs before any of this is established.
 *
 * Establishment rides the carrier's own launch: sigtls_thread_create
 * layers over elfsysv_carrier_thread_create, so the new thread first
 * writes its thread pointer into its carrier (the carrier shim), then
 * hangs its record on the TCB it now carries (this unit's shim), then
 * runs the body.  The record starts with what POSIX gives a new thread:
 * the creator's mask, taken on the creating thread at create time, and
 * no alternate stack.
 */

#ifndef ELFSYSV_FACE_SIGTLS_H
#define ELFSYSV_FACE_SIGTLS_H

#include "../signal/signal.h"
#include "../tls/tls.h"
#include "carrier.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install the provider. Idempotent. */
void elfsysv_face_sigtls_install(void);

/* Hang `rec` on the calling thread's TCB, initialized to `blocked` with
 * the unblockable bits dropped and no alternate stack.  The record is the
 * caller's and must outlive the thread's use of it.  Refuses (-1) when
 * the thread carries no TCB. */
int elfsysv_face_sigtls_adopt(elfsysv_sigtls_t *rec, uint64_t blocked);

/* One launch in flight, layered over the carrier's.  The caller fills
 * `start`, `arg` and `carrier.tp`; everything else is this unit's. */
typedef struct elfsysv_face_sigtls_launch {
	elfsysv_carrier_launch_t carrier;
	elfsysv_sigtls_t record;
	void *(*start)(void *);
	void *arg;
} elfsysv_face_sigtls_launch_t;

/* Create a thread whose carrier and signal record are both established
 * before `launch->start` runs.  `st` is the process state; the creator's
 * mask is read through it on the calling thread.  Returns what `create`
 * returns, or -1 when the carrier is not initialized. */
int elfsysv_face_sigtls_thread_create(elfsysv_carrier_create_fn create,
				      void **thread, const void *attr,
				      elfsysv_face_sigtls_launch_t *launch,
				      const elfsysv_sigstate_t *st);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_FACE_SIGTLS_H */
