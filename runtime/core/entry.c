/*
 * entry.c -- the MS-ABI entry points Windows calls into elfsysv1.dll (WP-22).
 *
 * Each entry point here is Microsoft x64 and carries the SEH unwind data the
 * host recognizes; each reaches System V code one frame down. The reasoning
 * for the convention split and the unwind seam is in README.md and DR-0012.
 * The bodies are stand-ins: a real runtime does not exist yet, so each entry
 * point does the least that makes the crossing observable -- reach a System V
 * core and come back -- rather than the runtime work it will eventually front.
 *
 * Compiled -mno-red-zone with the rest of the unit.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core.h"

/* ---- the System V cores ------------------------------------------------ */

/*
 * The ELF/System V world, one frame below every entry point. A real runtime
 * puts its own bodies here; this one folds a salt through the token so a caller
 * can tell the core actually ran and returned its value across the crossing.
 */
#define ELFSYSV_CORE_SALT 0xE1F5C0DE00000000ull

/* Observation seam. A stand-in core has no work whose completion a test could
 * otherwise see, so it records that it ran and the token it ran on. A real
 * runtime body needs neither; these two globals are the instrumentation that
 * lets the crossing be observed at stand-in width and are documented as such. */
volatile uint64_t elfsysv_core_calls;
volatile uint64_t elfsysv_core_last_token;

ELFSYSV_SYSV uint64_t elfsysv_core_run(uint64_t token)
{
	/* A down-call into Windows from System V code, so the crossing is
	 * exercised in both directions within one entry: MS in at the top,
	 * System V here, MS out through the wrapper. WP-21 owns w_Sleep; until
	 * the runtime links it, a bare Sleep(0) stands in and the audit that
	 * forbids naming the raw import runs when the runtime is built. */
	elfsysv_core_calls++;
	elfsysv_core_last_token = token;
	Sleep(0);
	return token ^ ELFSYSV_CORE_SALT;
}

/* ---- the host-facing entry points -------------------------------------- */

/*
 * The DLL entry. A re-faced DLL is entered here by the loader before the rest
 * of this unit is reachable, which is why its firing cannot be certified until
 * WP-41 links the DLL; the shape, the convention, and the unwind data are
 * certified now. It reaches the core so the crossing is real even at shape
 * width.
 */
ELFSYSV_MSABI int32_t elfsysv_dllmain(void *hinst, uint32_t reason, void *reserved)
{
	(void)hinst; (void)reserved;
	/* DLL_PROCESS_ATTACH is 1; any reason reaches the core and returns
	 * true, which is what a DllMain that declines to fail returns. */
	return elfsysv_core_run(reason) != 0;
}

ELFSYSV_MSABI uint32_t elfsysv_thread_entry(void *arg)
{
	return (uint32_t)(elfsysv_core_run((uint64_t)(uintptr_t)arg) & 0xffffffffu);
}

ELFSYSV_MSABI void elfsysv_apc(uint64_t arg)
{
	(void)elfsysv_core_run(arg);
}

/*
 * A PE TLS callback. Certified here at ABI and unwind width; the loader firing
 * it from the DLL's PE TLS directory is WP-41's, because the directory exists
 * only once the DLL does.
 */
ELFSYSV_MSABI void elfsysv_tls_callback(void *dll, uint32_t reason, void *reserved)
{
	(void)dll; (void)reserved;
	(void)elfsysv_core_run(reason);
}

/*
 * A vectored exception handler. The real core's handler answers the faults the
 * runtime plants and hands everything else to Cygwin, which is where a genuine
 * access violation belongs. Here it reaches the core and continues the search,
 * so a test can confirm Windows entered System V code one frame down and that
 * an unrelated fault still travels on to Cygwin.
 */
ELFSYSV_MSABI int32_t elfsysv_veh(void *exception_pointers)
{
	EXCEPTION_POINTERS *ep = (EXCEPTION_POINTERS *)exception_pointers;
	(void)elfsysv_core_run(ep->ExceptionRecord->ExceptionCode);
	return EXCEPTION_CONTINUE_SEARCH;
}

/*
 * The signal-handler landing. Cygwin hijacks the target thread and calls a
 * handler; this is the MS-ABI entry that lands that delivery and reaches the
 * runtime. The frame the handler is eventually given -- siginfo_t and
 * ucontext_t in the psABI's shape, extended FPU state, and the 128-byte
 * reservation the red zone needs -- is WP-43's; this entry proves the landing
 * reaches System V code and returns.
 */
ELFSYSV_MSABI void elfsysv_signal_entry(int signo)
{
	(void)elfsysv_core_run((uint64_t)(unsigned)signo);
}

/* ---- the unwind self-check --------------------------------------------- */

/*
 * Does the host recognize unwind data for the code at pc? This is the property
 * that decides whether an entry point is walkable by the host's own machinery,
 * and it is measured against the host rather than assumed: RtlLookupFunctionEntry
 * is the function the exception dispatcher itself calls. An ms_abi entry point
 * returns 1; a sysv_abi core returns 0, which is the seam.
 */
int elfsysv_core_unwind_present(void *pc)
{
	DWORD64 base = 0;
	PRUNTIME_FUNCTION rf =
		RtlLookupFunctionEntry((DWORD64)(uintptr_t)pc, &base, NULL);
	return rf != NULL;
}
