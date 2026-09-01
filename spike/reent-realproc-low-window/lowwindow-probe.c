/* lowwindow-probe -- can a real process of the faced runtime place a MAP_FIXED
 * low region through its own mmap, on the _dll_crt0-brought-up main thread?
 *
 * The shape is fault.c's / realproc-probe.c's: -nostdlib against the WP-26
 * crt0 and -lcygwin, so startup runs the _dll_crt0 protocol and the faced
 * elfsysv1.dll is the process's sole Cygwin runtime, its cygheap and reent
 * brought up before main. Every faced call crosses by sysv_abi pointer.
 *
 * DR (this branch, faced-runtime hosting) resolved that the acceptance
 * crossing hosts the faced runtime as its OWN process rather than handing a
 * low window from a foreign parent to a suspended cygwin child (the handover
 * spike/reent-stub-realproc-window-reconcile measured refused). The decision's
 * named next measurement -- the one the resolved shape turns on -- is whether
 * such a process, on its own main thread, can map the fixed low ELF window
 * (0x400000) through its own mmap where the parent handover was refused.
 *
 * The measurement, in layers, each a reproducible key=word:
 *   - realproc_mmap_anon: a plain MAP_ANONYMOUS|MAP_PRIVATE mmap (runtime
 *     chooses the address) returns a live page -- faced mmap works at all on
 *     this thread (the concern that a bare native thread has no cygtls does
 *     not apply on the _dll_crt0 main thread);
 *   - realproc_low_window_occupant: a VirtualQuery walk of [0x400000,0x600000)
 *     classifies what the faced runtime itself laid in the low window at its
 *     own startup (reserved / committed / reserved+committed / free), directly
 *     comparable to the cygwin-child finding of the reconcile spike;
 *   - realproc_mmap_fixed_free: a MAP_FIXED mmap at a known-free low address
 *     (0x10000000) succeeds -- MAP_FIXED itself works through the faced mmap;
 *   - realproc_mmap_fixed_window: a MAP_FIXED mmap at ELF_WINDOW_BASE 0x400000
 *     for bzip2's span -- the fixed low region the DR-0008 mapping needs.
 *
 * Reports through kernel32 only; run detached via cmd (the faced runtime's
 * console wedges on a host pty). Addresses print as VirtualQuery-style context
 * the t3 runner strips; the reproducible findings are the words.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

/* Cygwin sys/mman.h values (the faced runtime's own). */
#define ELFSYSV_PROT_READ    1
#define ELFSYSV_PROT_WRITE   2
#define ELFSYSV_MAP_PRIVATE  0x2
#define ELFSYSV_MAP_FIXED    0x10
#define ELFSYSV_MAP_ANON     0x20

/* loader/exec/reserve.h: the non-PIE ELF image's fixed base and window. */
#define ELF_WINDOW_BASE  UINT64_C(0x00400000)
#define BZIP2_SPAN       UINT64_C(0x00060000)   /* 4 PT_LOAD at 0x400000, 0x50000 rounded up a granule */
#define FREE_LOW_ADDR    UINT64_C(0x10000000)   /* a low address the runtime leaves free */
#define PROBE_LEN        UINT64_C(0x00010000)   /* one 64K granule for the free-address control */

void *memset(void *s, int c, size_t n)
{ unsigned char *p = s; while (n--) *p++ = (unsigned char)c; return s; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *dp = d; const unsigned char *sp = s; while (n--) *dp++ = *sp++; return d; }

/* crt0's _cygwin_crt0_common calls cygwin_internal (CW_USER_DATA) MS-style;
 * the export is a System V veneer now, so interpose the crossing. */
unsigned long long cygwin_internal(unsigned int t, ...)
{
	typedef unsigned long long (__attribute__((sysv_abi)) *cw_fn)(unsigned int, ...);
	static cw_fn p;
	if (!p)
		p = (cw_fn)(void *)GetProcAddress(
			GetModuleHandleA("elfsysv1.dll"), "cygwin_internal");
	return p ? p(t) : 0;
}

static void outs(const char *s)
{
	DWORD n = 0, len = 0;
	while (s[len])
		len++;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, len, &n, NULL);
}
static void outhex(unsigned long long v)
{
	char b[19];
	int i = sizeof b;
	b[--i] = 0;
	do {
		int d = (int)(v & 0xf);
		b[--i] = d < 10 ? '0' + d : 'a' + d - 10;
		v >>= 4;
	} while (v);
	outs("0x");
	outs(b + i);
}

/* faced mmap: void *mmap(void *, size_t, int prot, int flags, int fd, off_t). */
typedef void *(__attribute__((sysv_abi)) *mmap_fn)(void *, size_t, int, int, int, long long);
typedef int   (__attribute__((sysv_abi)) *munmap_fn)(void *, size_t);
typedef int  *(__attribute__((sysv_abi)) *errno_fn)(void);

#define MAP_FAILED_PTR ((void *)-1)

/* Classify the low window [0x400000, 0x600000) as the faced runtime left it. */
static void walk_low_window(void)
{
	uint64_t a = ELF_WINDOW_BASE, end = ELF_WINDOW_BASE + 0x200000;
	int any_reserve = 0, any_commit = 0, any_free = 0;
	MEMORY_BASIC_INFORMATION mbi;

	while (a < end && VirtualQuery((void *)(uintptr_t)a, &mbi, sizeof mbi)) {
		uint64_t base = (uint64_t)(uintptr_t)mbi.BaseAddress;
		uint64_t rsz  = (uint64_t)mbi.RegionSize;
		if (mbi.State == MEM_COMMIT) any_commit = 1;
		else if (mbi.State == MEM_RESERVE) any_reserve = 1;
		else any_free = 1;
		outs("  region base="); outhex(base);
		outs(" size="); outhex(rsz);
		outs(mbi.State == MEM_COMMIT ? " committed" :
		     mbi.State == MEM_RESERVE ? " reserved" : " free");
		outs("\n");
		a = base + rsz;
		if (rsz == 0) break;
	}
	outs("realproc_low_window_occupant=");
	if (any_commit && any_reserve) outs("reserved+committed");
	else if (any_commit)           outs("committed");
	else if (any_reserve)          outs("reserved");
	else if (any_free)             outs("free");
	else                           outs("unknown");
	outs("\n");
}

int main(void)
{
	HMODULE h = GetModuleHandleA("elfsysv1.dll");
	mmap_fn   f_mmap   = (mmap_fn)(void *)GetProcAddress(h, "mmap");
	munmap_fn f_munmap = (munmap_fn)(void *)GetProcAddress(h, "munmap");
	errno_fn  f_errno  = (errno_fn)(void *)GetProcAddress(h, "__errno");
	void *r;
	int flags_anon  = ELFSYSV_MAP_PRIVATE | ELFSYSV_MAP_ANON;
	int flags_fixed = flags_anon | ELFSYSV_MAP_FIXED;
	int prot_rw     = ELFSYSV_PROT_READ | ELFSYSV_PROT_WRITE;

	outs("realproc_mmap_export="); outs(f_mmap ? "found" : "missing"); outs("\n");
	if (!f_mmap) { outs("verdict=no\n"); return 1; }

	/* (1) plain anonymous mmap -- faced mmap works on this thread at all. */
	*f_errno() = 0;
	r = f_mmap((void *)0, (size_t)PROBE_LEN, prot_rw, flags_anon, -1, 0);
	outs("realproc_mmap_anon=");
	if (r != MAP_FAILED_PTR && r != (void *)0) {
		*(volatile unsigned char *)r = 0x5a;   /* touch it: really live */
		outs("live at "); outhex((uint64_t)(uintptr_t)r);
		if (f_munmap) f_munmap(r, (size_t)PROBE_LEN);
	} else {
		outs("failed errno="); outhex((uint64_t)(unsigned)*f_errno());
	}
	outs("\n");

	/* (2) what the faced runtime laid in the low window at its own startup. */
	walk_low_window();

	/* (3) MAP_FIXED at a known-free low address -- MAP_FIXED itself works. */
	*f_errno() = 0;
	r = f_mmap((void *)(uintptr_t)FREE_LOW_ADDR, (size_t)PROBE_LEN, prot_rw,
		   flags_fixed, -1, 0);
	outs("realproc_mmap_fixed_free=");
	if (r == (void *)(uintptr_t)FREE_LOW_ADDR) {
		*(volatile unsigned char *)r = 0x5a;
		outs("ok at "); outhex((uint64_t)(uintptr_t)r);
		if (f_munmap) f_munmap(r, (size_t)PROBE_LEN);
	} else {
		outs("failed ret="); outhex((uint64_t)(uintptr_t)r);
		outs(" errno="); outhex((uint64_t)(unsigned)*f_errno());
	}
	outs("\n");

	/* (4) THE measurement: MAP_FIXED at ELF_WINDOW_BASE for bzip2's span --
	 * the fixed low region the DR-0008 image mapping needs, on the sole
	 * runtime's own main thread, where the foreign-child handover was refused. */
	*f_errno() = 0;
	r = f_mmap((void *)(uintptr_t)ELF_WINDOW_BASE, (size_t)BZIP2_SPAN, prot_rw,
		   flags_fixed, -1, 0);
	outs("realproc_mmap_fixed_window=");
	if (r == (void *)(uintptr_t)ELF_WINDOW_BASE) {
		*(volatile unsigned char *)r = 0x5a;
		outs("ok at "); outhex((uint64_t)(uintptr_t)r);
		if (f_munmap) f_munmap(r, (size_t)BZIP2_SPAN);
		outs("\n");
		outs("verdict=cleared\n");
	} else {
		outs("failed ret="); outhex((uint64_t)(uintptr_t)r);
		outs(" errno="); outhex((uint64_t)(unsigned)*f_errno());
		outs("\n");
		outs("verdict=blocked-by-runtime-low-occupant\n");
	}
	return 0;
}
