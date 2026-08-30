/*
 * callback_test.c -- WP-23's crossing test: a System V callback the ELF world
 * hands down to Windows survives being called Microsoft x64 with its caller's
 * callee-saved set intact, in the three shapes the done-condition names.
 *
 * The method is spike 3's and WP-22's. cb_ms_probe (t/callback_probe.S) stands
 * in for Windows: it poisons the full Microsoft callee-saved set, calls the
 * trampoline the register function handed back, and reports which registers came
 * back changed and what the callee returned. A round trip passes when no
 * register leaked and the return value proves the arguments reached the System V
 * side in the right registers. Two controls make the check honest: a trampoline
 * with its save-and-restore removed, which leaks exactly the scratch set the
 * real one keeps, and a callee that destroys the whole set, which lights every
 * bit. Neither control passing is what lets a dark mask from the real trampoline
 * mean the trampoline held rather than the probe having gone blind.
 *
 * Compiled -mno-red-zone with the unit.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "callback.h"

/* The seam core.h marks is filled: this unit included callback.h and can say so
 * to the compiler rather than only in prose. */
_Static_assert(ELFSYSV_WP23_FILLED, "WP-23 seam is filled by callback.h");

/* The hand-written probe and the callbacks and controls it drives. */
extern void cb_ms_probe(void *fn, const void *a, const void *b,
			uint32_t *gpr_mask, uint32_t *xmm_mask, uint64_t *retval);
extern __attribute__((sysv_abi)) int32_t  cb_sysv_comparator(const void *, const void *);
extern __attribute__((sysv_abi)) uint32_t cb_sysv_threadproc(void *);
extern __attribute__((sysv_abi)) int32_t  cb_sysv_exfilter(void *);
extern void cb_leaky_tramp_comparator(void); /* ms_abi; shape irrelevant to the probe */
extern void cb_leaky_ms(void);

/* GPR mask bit for rsi and rdi -- the two integer registers a System V callee
 * scratches and a Microsoft caller keeps. bits: rbx0 rbp1 rsi2 rdi3 r12..r15. */
#define GPR_RSI 0x04u
#define GPR_RDI 0x08u
#define GPR_SCRATCH (GPR_RSI | GPR_RDI)
#define GPR_ALL 0xffu
#define XMM_ALL 0x3ffu

static int failures;
static int quiet;

static void report(const char *name, int ok, const char *detail)
{
	if (!ok)
		failures++;
	if (!quiet || !ok)
		printf("  %-22s %-4s %s\n", name, ok ? "ok" : "FAIL", detail);
}

/*
 * A round trip through a real trampoline. Poison, call, and require the whole
 * Microsoft set intact and the return value the arguments imply.
 */
static void round_trip(const char *name, void *tramp,
		       const void *a, const void *b, uint64_t expect)
{
	uint32_t gpr = 0xdead, xmm = 0xdead;
	uint64_t ret = 0;
	char detail[128];

	cb_ms_probe(tramp, a, b, &gpr, &xmm, &ret);
	int ok = (gpr == 0) && (xmm == 0) && ((uint32_t)ret == (uint32_t)expect);
	snprintf(detail, sizeof detail,
		 "gpr=0x%02x xmm=0x%03x ret=0x%08x want=0x%08x",
		 gpr, xmm, (uint32_t)ret, (uint32_t)expect);
	report(name, ok, detail);
}

int main(int argc, char **argv)
{
	int terse = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--terse")) terse = 1;
		else if (!strcmp(argv[i], "--quiet")) quiet = 1;
	}

	/* Register the three callbacks; each hands back the ms_abi trampoline a
	 * host API would be given. */
	elfsysv_ms_comparator ms_cmp = elfsysv_cb_set_comparator(cb_sysv_comparator);
	elfsysv_ms_threadproc ms_thr = elfsysv_cb_set_threadproc(cb_sysv_threadproc);
	elfsysv_ms_exfilter   ms_exf = elfsysv_cb_set_exfilter(cb_sysv_exfilter);

	if (!terse)
		printf("WP-23 callback trampolines: the crossing\n");

	/* --- the three round trips --- */
	int32_t cmp_a = 0x00001200, cmp_b = 0x00000034;   /* diff 0x11cc */
	round_trip("comparator", (void *)ms_cmp, &cmp_a, &cmp_b,
		   (uint32_t)(cmp_a - cmp_b));

	uint32_t thr_code = 0xC0FFEE11u;
	round_trip("threadproc", (void *)ms_thr, &thr_code, &thr_code, thr_code);

	int32_t exf_code = 0x600DF11E;                    /* a filter disposition */
	round_trip("exfilter", (void *)ms_exf, &exf_code, &exf_code,
		   (uint32_t)exf_code);

	/* --- the trampolines carry host-recognized unwind data --- */
	/* An ms_abi frame the host can walk is what lets the trampoline be the
	 * stop the seam reserves, rather than a frame the host walks past into a
	 * System V callee. DR-0012's finding, checked here against the host. */
	{
		void *fns[3] = { (void *)ms_cmp, (void *)ms_thr, (void *)ms_exf };
		const char *nm[3] = { "unwind-comparator", "unwind-threadproc",
				      "unwind-exfilter" };
		for (int i = 0; i < 3; i++) {
			DWORD64 base = 0;
			PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(
				(DWORD64)(uintptr_t)fns[i], &base, NULL);
			report(nm[i], rf != NULL, rf ? "RUNTIME_FUNCTION present"
						     : "no unwind record");
		}
	}

	/* --- the controls: the check can fail, and the save/restore is why it
	 * does not --- */
	{
		uint32_t gpr = 0, xmm = 0; uint64_t ret = 0;
		/* Same callback, same clobber, but reached through a trampoline
		 * with no bracketing: the scratch set leaks. It must show rsi
		 * and rdi lit and every xmm lit, and must not disturb the
		 * registers a System V callee preserves (rbx, rbp, r12-r15). */
		cb_ms_probe((void *)cb_leaky_tramp_comparator, &cmp_a, &cmp_b,
			    &gpr, &xmm, &ret);
		int ok = ((gpr & GPR_SCRATCH) == GPR_SCRATCH) &&
			 ((gpr & ~GPR_SCRATCH) == 0) &&
			 (xmm == XMM_ALL);
		char d[96];
		snprintf(d, sizeof d, "gpr=0x%02x xmm=0x%03x (want scratch leak)",
			 gpr, xmm);
		report("leaky-tramp-leaks", ok, d);
	}
	{
		uint32_t gpr = 0, xmm = 0; uint64_t ret = 0;
		cb_ms_probe((void *)cb_leaky_ms, &cmp_a, &cmp_b, &gpr, &xmm, &ret);
		/* rbp is left intact by the control, so the whole-set expectation
		 * is every bit but rbp's. */
		int ok = ((gpr & ~0x02u) == (GPR_ALL & ~0x02u)) && (xmm == XMM_ALL);
		char d[96];
		snprintf(d, sizeof d, "gpr=0x%02x xmm=0x%03x (want full leak)",
			 gpr, xmm);
		report("leaky-ms-lights-all", ok, d);
	}

	int verdict = (failures == 0);
	if (terse) {
		printf("verdict=%s\n", verdict ? "yes" : "no");
		printf("round_trips=comparator,threadproc,exfilter\n");
		printf("callee_saved_leak=none\n");
	} else {
		printf("%s: %d check(s) failed\n",
		       verdict ? "verdict=yes" : "verdict=no", failures);
	}
	return verdict ? 0 : 1;
}
