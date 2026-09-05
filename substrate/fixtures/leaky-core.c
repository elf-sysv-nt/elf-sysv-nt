/*
 * leaky-core.c -- a fixture, not a build input.  It is shaped like a core
 * source that has reached below the substrate line, and it carries one instance
 * of each thing check-substrate-line forbids, so the check has something real
 * to fail on.  It must never compile into anything: it exists to make the
 * check's teeth visible.  check-substrate-line finds every leak and exits 1.
 */
#include "substrate.h"

/* Leak 1 and 3: an NT HANDLE for the thread, and an NT CONTEXT the core has no
 * business assembling. */
int stop_thread(int tid, HANDLE thread)
{
	CONTEXT regs;

	/* Leak 2: an NT call, straight from the core. */
	NtSuspendThread(thread, 0);
	NtGetContextThread(thread, &regs);
	(void)tid;
	return 0;
}

/* Leak 4: a WHP call, the other substrate's primitive. */
int run_vcpu(void *partition)
{
	return WHvRunVirtualProcessor(partition, 0, 0, 0);
}

/* Leak 5: a raw dereference of a user pointer, the thing user_copy_* exists to
 * prevent. */
int read_user_word(uint64_t *uaddr)
{
	return (int)*uaddr;
}
