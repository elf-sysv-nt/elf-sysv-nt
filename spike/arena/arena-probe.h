/* Shared between the two halves of the arena probe. The placeholder and
 * section-view questions live in arena-probe.c; the lazy-commit handler and
 * its timing live in arena-fault.c, kept apart because a vectored handler that
 * commits pages under the probe's own faults is the one piece that has to stay
 * readable on its own.
 */
#ifndef ARENA_PROBE_H
#define ARENA_PROBE_H

#include <stdint.h>

struct fault_result {
	int   reserved;       /* the plain reservation the handler covers went up */
	int   veh_worked;     /* the faulting instruction re-executed and stored */
	int   readback_ok;    /* what it stored is what came back */
	unsigned long faults; /* faults the handler committed for */
	unsigned long pages;  /* pages walked in the timed sweep */
	double fault_ns;      /* median fault-and-commit round trip */
	double fault_ns_p90;
	double control_ns;    /* median first touch on already-committed pages */
	double control_ns_p90;
	unsigned long status; /* nonzero when the reservation itself was refused */
};

/* Runs q6 and q7 together, since they want the same reservation and the same
 * handler. `pages` is the sweep length for q7; q6 needs only the first one.
 * Installs and removes its own vectored handler. Returns 0 when the handler
 * path worked end to end.
 */
int measure_lazy_commit(struct fault_result *out, unsigned long pages, int verbose);

#endif
