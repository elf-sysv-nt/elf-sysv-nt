/*
 * carrier -- WP-27 milestone 8: thread creation establishes the DR-0021
 * carrier, against the real DLL.
 *
 * The harness is the fault.c shape: a real process of the faced runtime,
 * vendor crt0 into the asis `_dll_crt0`, linked -nostdlib so every
 * crossing is System V by function pointer, reporting through kernel32
 * alone.  What it certifies:
 *
 *   - the DLL hands out the calling thread's carrier address through
 *     cygwin_internal (CW_ELFSYSV_CARRIER), and the offset below that
 *     thread's StackBase is one constant, inside the CW_CYGTLS_PADSIZE
 *     reservation, on the main thread and on a created thread alike;
 *   - a thread the runtime creates finds its carrier zero (the vendor's
 *     init_thread memset) and elfsysv_carrier_thread_create establishes
 *     its thread pointer there before the body runs;
 *   - the carriers are per thread: the child's establishment does not
 *     move the main thread's word, and each %gs-chain read returns the
 *     word its own thread carries;
 *   - the refusal arms hold: an unaligned probe and a probe disagreeing
 *     with the latched offset are both turned away, so a wrong offset
 *     cannot latch silently.
 *
 * The CW code numbers are derived from the vendor header at build time
 * by carrier.sh, not copied here.
 *
 * Built and driven by t/carrier.sh.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#include "../carrier.h"

#ifndef ELFSYSV_CW_CYGTLS_PADSIZE
#error carrier.sh derives ELFSYSV_CW_CYGTLS_PADSIZE from the vendor header
#endif
#ifndef ELFSYSV_CW_ELFSYSV_CARRIER
#error carrier.sh derives ELFSYSV_CW_ELFSYSV_CARRIER from the vendor header
#endif
#define ELFSYSV_CW_USER_DATA 4	/* crt0's own call, as in fault.c */

/* ---- the compiler's and the link's own references ------------------- */

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = s;
	while (n--)
		*p++ = (unsigned char)c;
	return s;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *dp = d;
	const unsigned char *sp = s;
	while (n--)
		*dp++ = *sp++;
	return d;
}

/* crt0 calls cygwin_internal the Microsoft way; the export is the System
 * V veneer.  Interpose the crossing, and use the same forwarder for this
 * test's own getinfo probes. */
unsigned long long cygwin_internal(unsigned int t, ...)
{
	typedef unsigned long long
		(__attribute__((sysv_abi)) *cw_fn)(unsigned int, ...);
	static cw_fn p;
	if (!p)
		p = (cw_fn)(void *)GetProcAddress(
			GetModuleHandleA("elfsysv1.dll"), "cygwin_internal");
	return p ? p(t) : 0;	/* only zero-argument codes reach this */
}

/* ---- reporting, kernel32 only --------------------------------------- */

static void outs(const char *s)
{
	DWORD n = 0, len = 0;
	while (s[len])
		len++;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, len, &n, NULL);
}

static void outn(long v)
{
	char b[24];
	int i = sizeof b;
	unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
	b[--i] = 0;
	do {
		b[--i] = '0' + u % 10;
		u /= 10;
	} while (u);
	if (v < 0)
		b[--i] = '-';
	outs(b + i);
}

static unsigned checks, failures;
static void want(int ok, const char *what, long got)
{
	checks++;
	if (ok)
		return;
	failures++;
	outs("FAIL: ");
	outs(what);
	outs(" (got ");
	outn(got);
	outs(")\n");
}

/* ---- the faced exports this test calls ------------------------------ */

typedef int (__attribute__((sysv_abi)) *pthread_join_fn)(void *, void **);

/* ---- the created thread's body -------------------------------------- */

/* Stand-in thread pointers: distinct addresses are all the carrier needs. */
static uint64_t tcb_main, tcb_child;

static struct {
	void *got;		/* carrier read inside the body        */
	uint64_t addr;		/* CW_ELFSYSV_CARRIER on this thread   */
	uint64_t base;		/* this thread's StackBase             */
} child;

static void *child_main(void *arg)
{
	(void)arg;
	child.got = elfsysv_carrier_get();
	child.addr = (uint64_t)cygwin_internal(ELFSYSV_CW_ELFSYSV_CARRIER);
	child.base = elfsysv_carrier_gs_read(ELFSYSV_TEB_STACKBASE);
	return (void *)0xC0DE;
}

int main(void)
{
	HMODULE dll = GetModuleHandleA("elfsysv1.dll");
	elfsysv_carrier_create_fn f_pthread_create;
	pthread_join_fn f_pthread_join;
	elfsysv_carrier_launch_t launch;
	const char *why = "";
	uint64_t padsize, addr, off, base;
	void *thread = NULL, *joined = NULL;
	int rc;

	if (!dll) {
		outs("FAIL: elfsysv1.dll is not in the process\n");
		return 1;
	}
	f_pthread_create = (elfsysv_carrier_create_fn)(void *)
			   GetProcAddress(dll, "pthread_create");
	f_pthread_join	 = (pthread_join_fn)(void *)
			   GetProcAddress(dll, "pthread_join");
	if (!f_pthread_create || !f_pthread_join) {
		outs("FAIL: an export is missing\n");
		return 1;
	}

	/* The probe pair, on the main thread. */
	padsize = (uint64_t)cygwin_internal(ELFSYSV_CW_CYGTLS_PADSIZE);
	addr	= (uint64_t)cygwin_internal(ELFSYSV_CW_ELFSYSV_CARRIER);
	base	= elfsysv_carrier_gs_read(ELFSYSV_TEB_STACKBASE);
	want(padsize > 0 && padsize < 0x100000,
	     "CW_CYGTLS_PADSIZE is sane", (long)padsize);
	want(addr != 0, "CW_ELFSYSV_CARRIER answers", (long)(addr != 0));
	want(addr < base && base - addr <= padsize,
	     "the carrier sits inside the reservation",
	     (long)(base - addr));

	/* Before anything latches: creation must refuse. */
	rc = elfsysv_carrier_thread_create(f_pthread_create, &thread, NULL,
					   &launch);
	want(rc == -1, "thread_create refuses before init", rc);

	/* The refusal arms, then the real init. */
	rc = elfsysv_carrier_init(addr | 4, padsize, &why);
	want(rc == -1, "init refuses an unaligned probe", rc);
	rc = elfsysv_carrier_init(addr, padsize, &why);
	want(rc == 0, "init latches the probed offset", rc);
	off = elfsysv_carrier_offset();
	want(off == base - addr, "the offset is the probed distance",
	     (long)off);
	rc = elfsysv_carrier_init(addr - 64, padsize, &why);
	want(rc == -1, "init refuses a disagreeing probe", rc);
	want(elfsysv_carrier_offset() == off,
	     "the disagreeing probe did not move the latch",
	     (long)elfsysv_carrier_offset());

	/* The vendor zeroes _cygtls at thread init; establish over it. */
	want(elfsysv_carrier_get() == NULL,
	     "the main thread's carrier starts zero",
	     (long)(uintptr_t)elfsysv_carrier_get());
	elfsysv_carrier_set(&tcb_main);
	want(elfsysv_carrier_get() == &tcb_main,
	     "the main thread reads back its own pointer",
	     (long)(elfsysv_carrier_get() == &tcb_main));

	/* A thread the runtime creates, carrier established before the
	 * body -- the shape the veneer's pthread_create inherits. */
	launch.start = child_main;
	launch.arg = NULL;
	launch.tp = &tcb_child;
	rc = elfsysv_carrier_thread_create(f_pthread_create, &thread, NULL,
					   &launch);
	want(rc == 0, "pthread_create through the face", rc);
	if (rc == 0) {
		rc = f_pthread_join(thread, &joined);
		want(rc == 0, "pthread_join through the face", rc);
		want(joined == (void *)0xC0DE, "the body's return crossed",
		     (long)(uintptr_t)joined);
	}
	want(launch.established, "the shim established before the body",
	     launch.established);
	want(launch.found == NULL, "the child's carrier started zero",
	     (long)(uintptr_t)launch.found);
	want(child.got == &tcb_child,
	     "the child reads back its own pointer",
	     (long)(child.got == &tcb_child));
	want(child.addr != 0 && child.addr != addr,
	     "the child's carrier is its own word",
	     (long)(child.addr != addr));
	want(child.base - child.addr == off,
	     "the offset is one constant across threads",
	     (long)(child.base - child.addr));

	/* Per thread: the child's establishment left this word alone. */
	want(elfsysv_carrier_get() == &tcb_main,
	     "the main thread's carrier survived the child",
	     (long)(elfsysv_carrier_get() == &tcb_main));

	outs("verdict=");
	outs(failures ? "no" : "yes");
	outs("\nchecks=");
	outn(checks);
	outs("\nfailures=");
	outn(failures);
	outs("\n");
	return failures ? 1 : 0;
}
