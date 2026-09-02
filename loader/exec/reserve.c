/* WP-41: the low window. See reserve.h for what it is for and DR-0028 for why
 * handing it over looks like this.
 *
 * This is a Win32 translation unit, kept apart from the POSIX-facing code the
 * way WP-32 keeps host_mem.c apart, so <windows.h> does not have to be
 * included beside anything that has its own opinion about the word BOOL.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "reserve.h"

static elf_window low_window;

elf_window *elf_window_low(void)
{
	return &low_window;
}

static uint64_t granule(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwAllocationGranularity ? si.dwAllocationGranularity : 0x10000;
}

static win_err reserve_at(uint64_t base, uint64_t size)
{
	return VirtualAlloc((void *)(UINT_PTR) base, (SIZE_T) size,
			    MEM_RESERVE, PAGE_NOACCESS)
	       ? win_ok : win_err_refused;
}

win_err elf_window_reserve(elf_window *w, uint64_t base, uint64_t size)
{
	uint64_t g = granule(), lo, hi;

	if (!w || !size)
		return win_err_arg;
	if (w->held)
		return win_ok;

	lo = base & ~(g - 1);
	hi = (base + size + g - 1) & ~(g - 1);
	if (hi <= lo)
		return win_err_arg;

#ifndef ELFSYSV_REALPROC
	{
		win_err rc = reserve_at(lo, hi - lo);
		if (rc != win_ok)
			return rc;
	}
#else
	/* DR-0071: a real process of the faced runtime lays its own address
	 * space, so there is no parent handover to reserve for. The fixed low
	 * window reads free at _dll_crt0 startup where the host VirtualAlloc is
	 * refused (spike/reent-realproc-low-window), and the image is placed
	 * into it directly by the faced mmap (elf_map's hint). The window is
	 * claimed as bookkeeping here without a host reservation; the placement
	 * maps into the free low region rather than releasing a reservation, so
	 * elf_window_yield's realproc branch skips the release. */
#endif
	w->base = lo;
	w->size = hi - lo;
	w->held = 1;
	return win_ok;
}

/* Emit a gap, merging it into the previous one when they abut so the child
 * sees the fewest reservations. Returns the new count, or -1 if gaps is full. */
static int plan_emit(elf_span *gaps, int n, int maxgaps,
                     uint64_t base, uint64_t size)
{
	if (size == 0)
		return n;
	if (n > 0 && gaps[n - 1].base + gaps[n - 1].size == base) {
		gaps[n - 1].size += size;
		return n;
	}
	if (n >= maxgaps)
		return -1;
	gaps[n].base = base;
	gaps[n].size = size;
	return n + 1;
}

int elf_window_plan(uint64_t base, uint64_t size,
                    const elf_region *regs, int nreg,
                    elf_span *gaps, int maxgaps)
{
	uint64_t end = base + size, cur = base;
	int i, n = 0;

	if (!regs || nreg < 0 || !gaps || maxgaps <= 0 || end <= base)
		return -1;

	for (i = 0; i < nreg && cur < end; i++) {
		uint64_t rb = regs[i].base, re = regs[i].base + regs[i].size, chi;

		if (re <= cur || rb >= end)
			continue;              /* wholly outside the window */
		if (rb > cur) {                /* a hole the query skipped: free */
			if ((n = plan_emit(gaps, n, maxgaps, cur, rb - cur)) < 0)
				return -1;
			cur = rb;
		}
		chi = re < end ? re : end;
		if (regs[i].state == elf_region_committed)
			return -1;             /* an occupant, not a bare reservation */
		if (regs[i].state == elf_region_free &&
		    (n = plan_emit(gaps, n, maxgaps, cur, chi - cur)) < 0)
			return -1;
		/* reserved: recognized as the child's own, left in place */
		cur = chi;
	}
	if (cur < end && (n = plan_emit(gaps, n, maxgaps, cur, end - cur)) < 0)
		return -1;
	return n;
}

/* Decide which of the reservations composing the window must be released
 * before the placer can bare its span. Since DR-0068 the window is not always
 * a single reservation: a cygwin-linked child's own low region stands beside
 * the parent's reservations around it, and each is its own MEM_RELEASE unit.
 * regs[0..nreg) describe the window as VirtualQuery reports it; this writes the
 * base of each reserved region within [base,base+size) into rel[0..maxrel) and
 * returns the count, or -1 if a committed region overlaps the window -- a real
 * occupant, not a bare reservation -- if a reservation reaches outside the
 * window and so is not the window's to release, or if rel does not fit. A free
 * region contributes nothing. Distinct reservations are never merged: the
 * caller releases each base on its own. This is the placement-time companion
 * to elf_window_plan and, like it, a pure decision. */
int elf_window_release_plan(uint64_t base, uint64_t size,
                            const elf_region *regs, int nreg,
                            uint64_t *rel, int maxrel)
{
	uint64_t end = base + size;
	int i, n = 0;

	if (!regs || nreg < 0 || !rel || maxrel <= 0 || end <= base)
		return -1;

	for (i = 0; i < nreg; i++) {
		uint64_t rb = regs[i].base, re = regs[i].base + regs[i].size;

		if (re <= base || rb >= end)
			continue;              /* wholly outside the window */
		if (regs[i].state == elf_region_committed)
			return -1;             /* an occupant, not a bare reservation */
		if (regs[i].state == elf_region_free)
			continue;              /* nothing there to release */
		/* reserved, and releasable only as a whole allocation, so it must
		 * lie within the window; one that overruns either edge is foreign
		 * and cannot be released to bare the span. */
		if (rb < base || re > end)
			return -1;
		if (n >= maxrel)
			return -1;
		rel[n++] = rb;
	}
	return n;
}

/* The reconciling reservation. VirtualQueryEx walks the child's window, the
 * planner decides which sub-spans are still the parent's to take, and each is
 * reserved on its own. This is the path a cygwin-linked child needs: it holds
 * its low reservation before any user code runs, so the window that starts on
 * it cannot be taken in one call. DR-0068. */
static win_err reserve_in_around(HANDLE proc, elf_window *w,
                                 uint64_t lo, uint64_t size)
{
	MEMORY_BASIC_INFORMATION m;
	elf_region regs[64];
	elf_span gaps[64];
	uint64_t at = lo, end = lo + size;
	int nreg = 0, ngap, i;

	while (at < end && nreg < (int)(sizeof regs / sizeof regs[0])) {
		if (!VirtualQueryEx(proc, (void *)(UINT_PTR) at, &m, sizeof m))
			return win_err_refused;
		regs[nreg].base  = (uint64_t)(UINT_PTR) m.BaseAddress;
		regs[nreg].size  = (uint64_t) m.RegionSize;
		regs[nreg].state = m.State == MEM_COMMIT  ? elf_region_committed :
		                   m.State == MEM_RESERVE ? elf_region_reserved  :
		                                            elf_region_free;
		at = (uint64_t)(UINT_PTR) m.BaseAddress + m.RegionSize;
		nreg++;
	}
	if (at < end)
		return win_err_refused;        /* more fragments than we can plan */

	ngap = elf_window_plan(lo, size, regs, nreg, gaps,
	                       (int)(sizeof gaps / sizeof gaps[0]));
	if (ngap < 0)
		return win_err_refused;

	for (i = 0; i < ngap; i++)
		if (!VirtualAllocEx(proc, (void *)(UINT_PTR) gaps[i].base,
		                    (SIZE_T) gaps[i].size, MEM_RESERVE,
		                    PAGE_NOACCESS))
			return win_err_refused;

	w->base = lo;
	w->size = size;
	w->held = 1;
	return win_ok;
}

win_err elf_window_reserve_in(void *proc, elf_window *w,
                              uint64_t base, uint64_t size)
{
	uint64_t g = granule(), lo, hi;

	if (!proc || !w || !size)
		return win_err_arg;

	lo = base & ~(g - 1);
	hi = (base + size + g - 1) & ~(g - 1);
	if (hi <= lo)
		return win_err_arg;

	if (VirtualAllocEx((HANDLE) proc, (void *)(UINT_PTR) lo,
			   (SIZE_T)(hi - lo), MEM_RESERVE, PAGE_NOACCESS)) {
		w->base = lo;
		w->size = hi - lo;
		w->held = 1;
		return win_ok;
	}

	/* The whole-window reservation was refused. A cygwin-linked child
	 * already holds its own low reservation before its first instruction,
	 * so the span that starts on it cannot be taken in one call. Recognize
	 * what the child holds and reserve only the free remainder. DR-0068. */
	return reserve_in_around((HANDLE) proc, w, lo, hi - lo);
}

win_err elf_window_adopt(elf_window *w, uint64_t base, uint64_t size)
{
	MEMORY_BASIC_INFORMATION m;
	uint64_t g = granule(), lo, hi, at;

	if (!w || !size)
		return win_err_arg;

	lo = base & ~(g - 1);
	hi = (base + size + g - 1) & ~(g - 1);
	if (hi <= lo)
		return win_err_arg;

	/* The window may be one reservation or, since DR-0068, several: a
	 * cygwin-linked child's own low region with the parent's reservations
	 * around it. Either way every byte of the window must stand reserved,
	 * with no committed occupant and no free hole, for the stub to adopt
	 * it. A stub started with no parent to arm it finds the span free and
	 * is refused. */
	for (at = lo; at < hi; ) {
		uint64_t rb, re;
		if (!VirtualQuery((void *)(UINT_PTR) at, &m, sizeof m))
			return win_err_refused;
		if (m.State != MEM_RESERVE)
			return win_err_refused;
		rb = (uint64_t)(UINT_PTR) m.BaseAddress;
		re = rb + (uint64_t) m.RegionSize;
		if (re <= at)
			return win_err_refused;
		at = re;
	}

	w->base = lo;
	w->size = hi - lo;
	w->held = 1;
	return win_ok;
}


void elf_window_release(elf_window *w)
{
	if (!w)
		return;
	if (w->held)
		VirtualFree((void *)(UINT_PTR) w->base, 0, MEM_RELEASE);
	w->base = 0;
	w->size = 0;
	w->held = 0;
}

/* The handover. The release and the placement are adjacent on purpose and
 * nothing may come between them: the moment the window is released it is the
 * lowest free region in the process, which is precisely what an allocation
 * with no requested base is given. */
win_err elf_window_yield(elf_window *w, win_place_fn place, void *ctx)
{
	MEMORY_BASIC_INFORMATION m;
	elf_region regs[64];
	uint64_t rel[64];
	uint64_t base, size, took_lo = 0, took_hi = 0, g, at, end;
	int nreg = 0, nrel, i;

	if (!w || !place)
		return win_err_arg;
	if (!w->held)
		return win_err_arg;

#ifdef ELFSYSV_REALPROC
	/* DR-0071: the realproc reserve claimed the window without a host
	 * reservation -- the faced runtime's own mmap places the image directly
	 * into the free low window (elf_map's hint) -- so there is nothing to
	 * release. Hand the bare, already-free window to the placer and keep it
	 * held. No survey, no MEM_RELEASE, no re-reservation: the parent-handover
	 * dance below has no work to do when the process laid its own space. */
	{
		uint64_t rlo = 0, rhi = 0;
		if (place(ctx, w->base, w->size, &rlo, &rhi) != 0)
			return win_err_place;
		return win_ok;
	}
#endif

	base = w->base;
	size = w->size;
	g = granule();
	end = base + size;

	/* Survey the window before releasing it. Since DR-0068 it may be one
	 * reservation -- the plain-PE child, whose parent took the whole span
	 * in a single call -- or several, when the parent reserved around a
	 * cygwin-linked child's own low region. Each reservation is its own
	 * MEM_RELEASE unit, so the release below is per-constituent rather than
	 * one VirtualFree of the base. */
	for (at = base; at < end && nreg < (int)(sizeof regs / sizeof regs[0]); ) {
		if (!VirtualQuery((void *)(UINT_PTR) at, &m, sizeof m)) {
			w->held = 0;
			return win_err_release;
		}
		regs[nreg].base  = (uint64_t)(UINT_PTR) m.BaseAddress;
		regs[nreg].size  = (uint64_t) m.RegionSize;
		regs[nreg].state = m.State == MEM_COMMIT  ? elf_region_committed :
		                   m.State == MEM_RESERVE ? elf_region_reserved  :
		                                            elf_region_free;
		at = (uint64_t)(UINT_PTR) m.BaseAddress + m.RegionSize;
		nreg++;
	}
	if (at < end) {                        /* more fragments than we can survey */
		w->held = 0;
		return win_err_release;
	}

	nrel = elf_window_release_plan(base, size, regs, nreg, rel,
	                               (int)(sizeof rel / sizeof rel[0]));
	if (nrel < 0) {
		w->held = 0;
		return win_err_release;
	}

	for (i = 0; i < nrel; i++)
		if (!VirtualFree((void *)(UINT_PTR) rel[i], 0, MEM_RELEASE)) {
			w->held = 0;
			return win_err_release;
		}
	w->held = 0;

	if (place(ctx, base, size, &took_lo, &took_hi) != 0)
		return win_err_place;

	/* Re-reserve the two remainders. A failure here is not fatal to the
	 * image that was just placed -- it is already mapped -- so it is
	 * reported by leaving held zero rather than by unwinding the
	 * placement, and the caller decides whether a process with an
	 * unprotected brk region is one it wants to continue. */
	took_lo &= ~(g - 1);
	took_hi = (took_hi + g - 1) & ~(g - 1);
	if (took_lo < base)
		took_lo = base;
	if (took_hi > base + size)
		took_hi = base + size;

	if (took_lo > base && reserve_at(base, took_lo - base) != win_ok)
		return win_ok;
	if (took_hi < base + size &&
	    reserve_at(took_hi, base + size - took_hi) != win_ok)
		return win_ok;

	w->held = 1;
	return win_ok;
}


void elf_window_arm(void)
{
	elf_window_reserve(&low_window, ELF_WINDOW_BASE, ELF_WINDOW_SIZE);
}

const char *win_err_name(win_err code)
{
	switch (code) {
	case win_ok:          return "win_ok";
	case win_err_arg:     return "win_err_arg";
	case win_err_refused: return "win_err_refused";
	case win_err_release: return "win_err_release";
	case win_err_place:   return "win_err_place";
	}
	return "win_err_?";
}
