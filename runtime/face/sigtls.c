/*
 * sigtls.c -- the per-thread signal record, reached through the carrier's
 * TCB (WP-27 milestone 8).
 *
 * See sigtls.h for the model.  Like carrier.c this keeps no dependency
 * beyond the compiler and the signal package it serves, so it links into
 * -nostdlib processes of the faced runtime.
 */

#include "sigtls.h"

/* The TCB head's slot is load-bearing: the provider reads it at a fixed
 * offset through the carrier, so a reorder must fail here, not at run
 * time. */
_Static_assert(__builtin_offsetof(elfsysv_tcbhead_t, sigtls) == 0x38,
	       "the sigtls slot sits at 0x38 in the TCB head");

static elfsysv_sigtls_t *face_sigtls(void)
{
	elfsysv_tcbhead_t *head = elfsysv_carrier_get();

	if (!head)
		return 0;
	return (elfsysv_sigtls_t *)head->sigtls;
}

void elfsysv_face_sigtls_install(void)
{
	elf_sig_set_tls_provider(face_sigtls);
}

int elfsysv_face_sigtls_adopt(elfsysv_sigtls_t *rec, uint64_t blocked)
{
	elfsysv_tcbhead_t *head = elfsysv_carrier_get();

	if (!head)
		return -1;
	elf_sig_tls_init(rec, blocked);
	head->sigtls = rec;
	return 0;
}

/* Runs on the new thread, after the carrier shim has established the
 * thread pointer: the TCB the carrier now answers is the launch's own
 * tp, and the record goes on it before the body runs. */
static void *sigtls_shim(void *arg)
{
	elfsysv_face_sigtls_launch_t *launch = arg;
	elfsysv_tcbhead_t *head = launch->carrier.tp;

	head->sigtls = &launch->record;
	return launch->start(launch->arg);
}

int elfsysv_face_sigtls_thread_create(elfsysv_carrier_create_fn create,
				      void **thread, const void *attr,
				      elfsysv_face_sigtls_launch_t *launch,
				      const elfsysv_sigstate_t *st)
{
	/* The creator's mask, read on the creating thread: POSIX
	 * inheritance is of the mask in force at create time. */
	elf_sig_tls_init(&launch->record, elf_sig_tls(st)->blocked);
	launch->carrier.start = sigtls_shim;
	launch->carrier.arg = launch;
	return elfsysv_carrier_thread_create(create, thread, attr,
					     &launch->carrier);
}
