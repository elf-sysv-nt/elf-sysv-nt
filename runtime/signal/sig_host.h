/*
 * sig_host.h -- the hijack, declared without the host's headers.
 *
 * Kept separate from signal.h so that everything above the delivery can be
 * compiled, tested and fuzzed without windows.h in the translation unit. The
 * thread handle travels as void * for the same reason.
 */

#ifndef ELFSYSV_RUNTIME_SIG_HOST_H
#define ELFSYSV_RUNTIME_SIG_HOST_H

#include "signal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Suspend `hthread`, hand it a pending delivery for `signo`, and resume it into
 * the trampoline that builds the frame. Returns 0 when the host calls all
 * succeeded and -1 when one of them did not.
 *
 * The disposition is not known when this returns: the target decides it. `p`
 * must outlive the delivery -- it is read by the other thread -- and its
 * `disposition` field goes from -1 to an elf_sig_disposition_t when the target
 * has decided. The caller owns the record and one is needed per delivery in
 * flight. */
int elfsysv_sig_hijack(void *hthread, elfsysv_sigstate_t *st, int signo,
		       const elfsysv_siginfo_t *info,
		       elfsysv_sig_pending_t *p);

/* The stack pointer of a suspended thread, for a caller that wants to know
 * where the red zone is before it delivers. Zero on failure. */
uintptr_t elfsysv_sig_thread_sp(void *hthread);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_RUNTIME_SIG_HOST_H */
