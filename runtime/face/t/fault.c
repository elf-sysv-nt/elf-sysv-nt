/*
 * fault -- WP-27 milestone 7: a fault beneath a System V frame arrives as
 * SIGSEGV and leaves by siglongjmp, against the real DLL.
 *
 * DR-0012 measured this path at stand-in width: a fault taken in a
 * sysv_abi frame that carries no host unwind record still reaches Cygwin
 * as SIGSEGV, because delivery restores a saved context rather than
 * unwinding the System V frames.  This test asks the same of the faced
 * DLL's own runtime.
 *
 * The harness is a real process of the faced runtime: the vendor's own
 * crt0 into `_dll_crt0`, whose startup protocol the unlisted disposition
 * kept asis for exactly this entry, so main runs beneath `dll_crt0_1`'s
 * exception protection with the signal machinery fully live.  The
 * cygload shape -- LoadLibrary plus cygwin_dll_init from a foreign PE --
 * was tried first and cannot carry this milestone: the vendor leaves the
 * main thread marked in-cygwin when `dll_crt0_1` returns early for a
 * dynamically loaded DLL, and delivery in that process shape hangs even
 * for a plain raise().  A real process is also the shape WP-41's loader
 * gives every ELF frame, so it is the honest harness, not a workaround.
 *
 * The exe links -nostdlib: the faced exports are System V now, so
 * nothing here may call libc the Microsoft way.  Every libc call crosses
 * by sysv_abi function pointer, straight at the export, the way an ELF
 * caller will; reporting goes through kernel32 alone.  Two seams the
 * link itself makes are handled by hand: crt0's Microsoft-convention
 * call to the veneer-faced variadic `cygwin_internal` is interposed with
 * a local forwarder that crosses correctly, and memset/memcpy are
 * defined locally so the compiler's own emissions stay in this module.
 *
 * The handler is left at the platform default convention: the runtime
 * calls handlers the Cygwin way, and the ELF-shaped signal frame is
 * WP-43's work, not this milestone's.
 *
 * Three measurements.  The Done-when's own case, a System V fault on the
 * process's main thread, is asserted; so is the delivery machinery on a
 * runtime-created pthread, with Microsoft frames.  The third -- a System
 * V fault on a pthread -- is DR-0012's seam measured at real width, and
 * it currently fails there: the dispatcher's walk across recordless
 * System V frames recovers on the main thread's stack but not on a
 * pthread's.  That is the tripwire the WP-27 risk note names, reopened
 * on DR-0012's own stated terms by the fault-dispatch record; it belongs
 * to WP-43 and milestone 8, so fault.sh runs it as a separate-process
 * probe whose outcome is reported, not asserted.
 *
 * Built and driven by t/fault.sh.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define ELFSYSV_SIGSEGV	11	/* cygwin/signal.h */
#define ELFSYSV_CW_USER_DATA 4	/* cygwin_getinfo_types */

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

/* crt0's _cygwin_crt0_common calls cygwin_internal (CW_USER_DATA) in the
 * Microsoft convention, but the export is WP-24's System V veneer now.
 * Interpose the crossing: same name, correct convention on the far side. */
unsigned long long cygwin_internal(unsigned int t, ...)
{
	typedef unsigned long long
		(__attribute__((sysv_abi)) *cw_fn)(unsigned int, ...);
	static cw_fn p;
	if (!p)
		p = (cw_fn)(void *)GetProcAddress(
			GetModuleHandleA("elfsysv1.dll"), "cygwin_internal");
	return p ? p(t) : 0;	/* only CW_USER_DATA reaches this */
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

static int tracing;
static void mark(const char *what)
{
	if (!tracing)
		return;
	outs("mark: ");
	outs(what);
	outs("\n");
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

/* ---- the System V shapes of the exports this test calls ------------- */

typedef int  (__attribute__((sysv_abi)) *sigaction_fn)(int, const void *,
						       void *);
typedef int  (__attribute__((sysv_abi)) *sigsetjmp_fn)(uint64_t *, int);
typedef void (__attribute__((sysv_abi)) *siglongjmp_fn)(uint64_t *, int);
typedef int  (__attribute__((sysv_abi)) *pthread_create_fn)(void **,
	     const void *, void *(*)(void *), void *);
typedef int  (__attribute__((sysv_abi)) *pthread_join_fn)(void *, void **);

/* cygwin/signal.h's struct sigaction on x86_64. */
struct elfsysv_sigaction {
	void	*sa_handler;
	uint64_t sa_mask;
	int	 sa_flags;
	int	 pad;
};

static sigsetjmp_fn f_sigsetjmp;
static siglongjmp_fn f_siglongjmp;

static uint64_t env[40] __attribute__((aligned(16)));
static volatile int handler_sig = -1;
static volatile int handler_runs;
static volatile int reached_past_fault;

/* The runtime calls this the Cygwin way; no attribute, the platform
 * default.  Out by the faced siglongjmp, across the signal frame. */
static void on_sigsegv(int sig)
{
	handler_sig = sig;
	handler_runs++;
	mark("handler entered; leaving by siglongjmp");
	f_siglongjmp(env, 7);
}

/* Two System V frames, the fault in the deeper one, with locals so the
 * frames are real and carry no host unwind record (DR-0012). */
static void __attribute__((sysv_abi, noinline)) poke(volatile int *p)
{
	volatile uint64_t frame_local = 0xFAB11ED;
	*p = (int)frame_local;	/* p is NULL: the fault */
}

static int __attribute__((sysv_abi, noinline)) fault_beneath_sysv(void)
{
	volatile uint64_t sentinel = 0x5E97161EE1ULL;
	int r = f_sigsetjmp(env, 1);
	if (r == 0) {
		mark("sigsetjmp taken; faulting");
		poke(NULL);
		reached_past_fault = 1;
		return -1;
	}
	mark("resumed at the sigsetjmp site");
	if (sentinel != 0x5E97161EE1ULL)
		return -2;	/* the frame was scribbled */
	return r;
}

/* The same round trip with every frame Microsoft.  On a runtime-created
 * thread this is the asserted case: it certifies the delivery machinery
 * on such threads while staying out of the DR-0012 seam, which the probe
 * below measures separately. */
static int __attribute__((noinline)) fault_all_ms(void)
{
	volatile int *p = NULL;
	int r = f_sigsetjmp(env, 1);
	if (r == 0) {
		mark("sigsetjmp taken; faulting (ms frames)");
		*p = 1;
		reached_past_fault = 1;
		return -1;
	}
	mark("resumed at the sigsetjmp site (ms frames)");
	return r;
}

static int thread_sysv;
static void *thread_main(void *arg)
{
	if (thread_sysv)
		*(int *)arg = fault_beneath_sysv();
	else
		*(int *)arg = fault_all_ms();
	return NULL;
}

int main(void)
{
	HMODULE dll = GetModuleHandleA("elfsysv1.dll");
	sigaction_fn f_sigaction;
	pthread_create_fn f_pthread_create;
	pthread_join_fn f_pthread_join;
	struct elfsysv_sigaction sa;
	void *thread = NULL, *joined;
	int thread_result = -100, rc;

	tracing = GetEnvironmentVariableA("ELFSYSV_FAULT_TRACE", NULL, 0) != 0;
	mark("main is up beneath the runtime");

	if (!dll) {
		outs("FAIL: elfsysv1.dll is not in the process\n");
		return 1;
	}
	f_sigaction	 = (sigaction_fn)(void *)
			   GetProcAddress(dll, "sigaction");
	f_sigsetjmp	 = (sigsetjmp_fn)(void *)
			   GetProcAddress(dll, "sigsetjmp");
	f_siglongjmp	 = (siglongjmp_fn)(void *)
			   GetProcAddress(dll, "siglongjmp");
	f_pthread_create = (pthread_create_fn)(void *)
			   GetProcAddress(dll, "pthread_create");
	f_pthread_join	 = (pthread_join_fn)(void *)
			   GetProcAddress(dll, "pthread_join");
	if (!f_sigaction || !f_sigsetjmp || !f_siglongjmp
	    || !f_pthread_create || !f_pthread_join) {
		outs("FAIL: an export is missing\n");
		return 1;
	}

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = (void *)on_sigsegv;
	rc = f_sigaction(ELFSYSV_SIGSEGV, &sa, NULL);
	want(rc == 0, "sigaction(SIGSEGV) through the face", rc);

	if (GetEnvironmentVariableA("ELFSYSV_FAULT_PROBE", NULL, 0)) {
		/* The fault-dispatch record's probe, run in its own process
		 * by fault.sh because the failing outcome kills it: a
		 * System V fault on a runtime-created thread. */
		thread_sysv = 1;
		mark("probe: sysv fault on a pthread");
		rc = f_pthread_create(&thread, NULL, thread_main,
				      (void *)&thread_result);
		if (rc == 0)
			f_pthread_join(thread, &joined);
		outs(thread_result == 7 ? "probe=delivered\n"
					: "probe=lost\n");
		return thread_result == 7 ? 0 : 3;
	}

	/* On the main thread: the process's own frames, the Done-when. */
	mark("faulting on the main thread");
	rc = fault_beneath_sysv();
	want(rc == 7, "siglongjmp resumed at the sigsetjmp site", rc);
	want(handler_runs == 1, "the handler ran once", handler_runs);
	want(handler_sig == ELFSYSV_SIGSEGV, "the handler saw SIGSEGV",
	     handler_sig);
	want(!reached_past_fault, "execution stopped at the fault",
	     reached_past_fault);

	/* And on a thread the DLL's pthread_create made -- the shape the
	 * veneer's thread creation will stand on (milestone 8) -- with
	 * Microsoft frames, certifying the delivery machinery itself on
	 * runtime-created threads. */
	handler_sig = -1;
	handler_runs = 0;
	mark("creating the thread");
	rc = f_pthread_create(&thread, NULL, thread_main,
			      (void *)&thread_result);
	want(rc == 0, "pthread_create through the face", rc);
	if (rc == 0) {
		rc = f_pthread_join(thread, &joined);
		want(rc == 0, "pthread_join through the face", rc);
	}
	want(thread_result == 7, "the thread's siglongjmp resumed with 7",
	     thread_result);
	want(handler_runs == 1, "the handler ran once on the thread",
	     handler_runs);

	outs("verdict=");
	outs(failures ? "no" : "yes");
	outs("\nchecks=");
	outn(checks);
	outs("\nfailures=");
	outn(failures);
	outs("\n");
	return failures ? 1 : 0;
}
