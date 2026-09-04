/* Lazy commit through a vectored exception handler, and what one first touch
 * costs when the handler is the thing that commits the page.
 *
 * Proposal 0011 asks anonymous memory to stay uncommitted until it is touched,
 * so a 64 GB MAP_NORESERVE does not charge 64 GB against the commit limit. The
 * mechanism it names is this one: reserve, fault, commit in the handler,
 * continue. Two things have to be true for that to be a mechanism rather than
 * a hope. The faulting instruction has to re-execute after the handler commits
 * under it, which is q6. And the round trip has to be cheap enough that the
 * kernel can afford it per page, which is q7 -- a number, and context only.
 */
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>

#include "arena-probe.h"

typedef LONG (NTAPI *nt_alloc_fn)(HANDLE, PVOID *, ULONG_PTR, PSIZE_T, ULONG, ULONG);

static nt_alloc_fn nt_allocate;
static volatile uintptr_t span_base;
static volatile uintptr_t span_end;
static volatile LONG      commits;
static volatile LONG      declines;

/* The handler. It commits exactly the 4 KB page under the faulting address and
 * hands the instruction back its chance to run. Anything it does not recognise
 * goes on down the chain, which is what keeps a real bug in this probe from
 * being swallowed by the thing that is supposed to be catching page faults.
 */
static LONG CALLBACK commit_on_fault(EXCEPTION_POINTERS *ep)
{
	if (ep->ExceptionRecord->ExceptionCode != (DWORD) EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;
	if (ep->ExceptionRecord->NumberParameters < 2)
		return EXCEPTION_CONTINUE_SEARCH;

	uintptr_t where = (uintptr_t) ep->ExceptionRecord->ExceptionInformation[1];
	if (where < span_base || where >= span_end)
		return EXCEPTION_CONTINUE_SEARCH;

	PVOID  page = (PVOID) (where & ~(uintptr_t) 0xfff);
	SIZE_T len  = 0x1000;
	LONG   st   = nt_allocate(GetCurrentProcess(), &page, 0, &len,
	                          MEM_COMMIT, PAGE_READWRITE);
	if (st < 0) {
		InterlockedIncrement(&declines);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	InterlockedIncrement(&commits);
	return EXCEPTION_CONTINUE_EXECUTION;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;
	return (x > y) - (x < y);
}

/* Sorts in place, then reads the requested percentile out. The callers want two
 * of them from one sweep: the median, which is the answer, and the ninetieth,
 * because NT's demand-zero path fills more than the page that faulted and a
 * median alone would hide how uneven the walk is. */
static double pct_ns(uint64_t *v, unsigned long n, int pct, double hz)
{
	if (pct == 50)
		qsort(v, n, sizeof *v, cmp_u64);
	unsigned long k = (unsigned long) ((double) n * pct / 100.0);
	if (k >= n)
		k = n - 1;
	return (double) v[k] / hz * 1e9;
}

/* Ties the cycle counter to wall time. Coarse on purpose: the numbers this
 * calibrates are hundreds of nanoseconds, and a 30 ms window is plenty for
 * three significant figures.
 */
static double calibrate(void)
{
	LARGE_INTEGER f, a, b;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&a);
	uint64_t c0 = __rdtsc();
	Sleep(30);
	uint64_t c1 = __rdtsc();
	QueryPerformanceCounter(&b);
	double secs = (double) (b.QuadPart - a.QuadPart) / (double) f.QuadPart;
	return (double) (c1 - c0) / secs;
}

int measure_lazy_commit(struct fault_result *out, unsigned long pages, int verbose)
{
	memset(out, 0, sizeof *out);
	out->pages = pages;

	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	nt_allocate = (nt_alloc_fn) (void *) GetProcAddress(nt, "NtAllocateVirtualMemory");
	if (!nt_allocate) {
		out->status = 0xC0000225UL;   /* nothing to commit with */
		return -1;
	}

	SIZE_T bytes = (SIZE_T) (pages + 1) * 0x1000;
	PVOID  base  = NULL;
	SIZE_T rlen  = bytes;
	LONG st = nt_allocate(GetCurrentProcess(), &base, 0, &rlen,
	                      MEM_RESERVE, PAGE_READWRITE);
	if (st < 0) {
		out->status = (unsigned long) st;
		return -1;
	}
	out->reserved = 1;

	span_base = (uintptr_t) base;
	span_end  = span_base + bytes;
	commits = declines = 0;

	PVOID h = AddVectoredExceptionHandler(1, commit_on_fault);
	if (!h) {
		out->status = 0xC0000017UL;
		return -1;
	}

	/* q6. One page, one fault, one store, and then ask the page what it holds.
	 * If the instruction did not re-execute we never get here at all -- the
	 * unhandled fault kills the process -- so the readback is the interesting
	 * half, not the survival. */
	volatile unsigned long long *probe = (volatile unsigned long long *) base;
	*probe = 0x5359535641524e41ULL;
	out->veh_worked  = (commits >= 1);
	out->readback_ok = (*probe == 0x5359535641524e41ULL);
	if (verbose)
		fprintf(stderr, "arena-probe: first touch committed, %ld fault(s)\n", (long) commits);

	double hz = calibrate();

	uint64_t *lazy = malloc(pages * sizeof *lazy);
	uint64_t *ctrl = malloc(pages * sizeof *ctrl);
	if (!lazy || !ctrl) {
		free(lazy); free(ctrl);
		RemoveVectoredExceptionHandler(h);
		out->status = 0xC0000017UL;
		return -1;
	}

	/* q7, the measured half. Each iteration stores into a page nothing has
	 * touched, so each one takes the fault, runs the handler, commits, and
	 * re-executes. */
	for (unsigned long i = 0; i < pages; i++) {
		volatile char *p = (volatile char *) (span_base + (i + 1) * 0x1000);
		uint64_t t0 = __rdtsc();
		*p = (char) i;
		uint64_t t1 = __rdtsc();
		lazy[i] = t1 - t0;
	}
	out->faults = (unsigned long) commits;

	/* The control. Same walk, same store, over pages that were committed up
	 * front, so what it prices is the demand-zero fault NT takes anyway. The
	 * difference between the two is what the handler costs. */
	PVOID cbase = NULL;
	SIZE_T clen = (SIZE_T) pages * 0x1000;
	st = nt_allocate(GetCurrentProcess(), &cbase, 0, &clen,
	                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (st >= 0) {
		for (unsigned long i = 0; i < pages; i++) {
			volatile char *p = (volatile char *) ((uintptr_t) cbase + i * 0x1000);
			uint64_t t0 = __rdtsc();
			*p = (char) i;
			uint64_t t1 = __rdtsc();
			ctrl[i] = t1 - t0;
		}
		out->control_ns     = pct_ns(ctrl, pages, 50, hz);
		out->control_ns_p90 = pct_ns(ctrl, pages, 90, hz);
	}

	out->fault_ns     = pct_ns(lazy, pages, 50, hz);
	out->fault_ns_p90 = pct_ns(lazy, pages, 90, hz);

	free(lazy);
	free(ctrl);
	RemoveVectoredExceptionHandler(h);
	span_base = span_end = 0;
	return (out->veh_worked && out->readback_ok) ? 0 : -1;
}
