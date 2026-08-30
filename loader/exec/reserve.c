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
	win_err rc;

	if (!w || !size)
		return win_err_arg;
	if (w->held)
		return win_ok;

	lo = base & ~(g - 1);
	hi = (base + size + g - 1) & ~(g - 1);
	if (hi <= lo)
		return win_err_arg;

	if ((rc = reserve_at(lo, hi - lo)) != win_ok)
		return rc;
	w->base = lo;
	w->size = hi - lo;
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

	if (!VirtualAllocEx((HANDLE) proc, (void *)(UINT_PTR) lo,
			    (SIZE_T)(hi - lo), MEM_RESERVE, PAGE_NOACCESS))
		return win_err_refused;

	w->base = lo;
	w->size = hi - lo;
	w->held = 1;
	return win_ok;
}

win_err elf_window_adopt(elf_window *w, uint64_t base, uint64_t size)
{
	MEMORY_BASIC_INFORMATION m;
	uint64_t g = granule(), lo, hi;

	if (!w || !size)
		return win_err_arg;

	lo = base & ~(g - 1);
	hi = (base + size + g - 1) & ~(g - 1);

	if (!VirtualQuery((void *)(UINT_PTR) lo, &m, sizeof m))
		return win_err_refused;
	if (m.State != MEM_RESERVE)
		return win_err_refused;
	if ((uint64_t)(UINT_PTR) m.AllocationBase != lo)
		return win_err_refused;
	if ((uint64_t)(UINT_PTR) m.BaseAddress + m.RegionSize < hi)
		return win_err_refused;

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
	uint64_t base, size, took_lo = 0, took_hi = 0, g;

	if (!w || !place)
		return win_err_arg;
	if (!w->held)
		return win_err_arg;

	base = w->base;
	size = w->size;
	g = granule();

	if (!VirtualFree((void *)(UINT_PTR) base, 0, MEM_RELEASE)) {
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
