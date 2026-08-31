/*
 * carrier.c -- the hosted DR-0021 carrier (WP-27 milestone 8).
 *
 * See carrier.h for the model.  This file keeps no dependency beyond the
 * compiler: it is linked into -nostdlib processes of the faced runtime,
 * so nothing here may reach libc either convention.
 */

#include "carrier.h"

/* The latched offset.  Written once under init's own check; read on the
 * hot path.  A plain word: init happens before threads that use it. */
static uint64_t carrier_off;

int elfsysv_carrier_init(uint64_t carrier_addr, uint64_t padsize,
			 const char **why)
{
	uint64_t base = elfsysv_carrier_gs_read(ELFSYSV_TEB_STACKBASE);
	uint64_t off;

	if (!carrier_addr || (carrier_addr & 7)) {
		if (why)
			*why = "carrier address absent or unaligned";
		return -1;
	}
	if (carrier_addr >= base) {
		if (why)
			*why = "carrier address is not below StackBase";
		return -1;
	}
	off = base - carrier_addr;
	if (!padsize || off > padsize) {
		if (why)
			*why = "carrier offset falls outside the _cygtls "
			       "reservation";
		return -1;
	}
	if (carrier_off && carrier_off != off) {
		if (why)
			*why = "a second probe disagrees with the latched "
			       "offset";
		return -1;
	}
	carrier_off = off;
	return 0;
}

uint64_t elfsysv_carrier_offset(void)
{
	return carrier_off;
}

void *elfsysv_carrier_get(void)
{
	return carrier_off ? elfsysv_carrier_get_at(carrier_off) : (void *)0;
}

void elfsysv_carrier_set(void *tp)
{
	uint64_t base;

	if (!carrier_off)
		return;
	base = elfsysv_carrier_gs_read(ELFSYSV_TEB_STACKBASE);
	*(void **)(uintptr_t)(base - carrier_off) = tp;
}

/* Runs on the new thread, called by the runtime's own thread start in the
 * platform's default convention, exactly as any pthread body is.  The
 * carrier write happens before the body: that is the establishment the
 * milestone names. */
static void *carrier_shim(void *arg)
{
	elfsysv_carrier_launch_t *launch = arg;

	launch->found = elfsysv_carrier_get();
	elfsysv_carrier_set(launch->tp);
	launch->established = 1;
	return launch->start(launch->arg);
}

int elfsysv_carrier_thread_create(elfsysv_carrier_create_fn create,
				  void **thread, const void *attr,
				  elfsysv_carrier_launch_t *launch)
{
	if (!carrier_off)
		return -1;
	launch->found = (void *)0;
	launch->established = 0;
	return create(thread, attr, carrier_shim, launch);
}
