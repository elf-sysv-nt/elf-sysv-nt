/*
 * gs-tp-probe -- a thread pointer this runtime owns, reached through %gs,
 * measured for persistence, addressing, lifecycle and cost against four
 * carriers side by side.
 *
 * Spike 1 killed a user-written %fs base at the scheduler. %gs is different in
 * kind: NT owns that base for the TEB and never clears it, so the thread
 * pointer cannot be the base -- it has to be a word fetched out of a structure
 * NT maintains, and the fetch is a chain with a hop for every party that can
 * break it. This measures the chain rather than a register.
 *
 * Four carriers:
 *   C1  a fixed TlsSlots index      gs:[0x1480 + 8*k]
 *   C2  the PE TLS directory        gs:[0x58], then _tls_index
 *   C3  a word below the stack base  gs:[0x08], then a fixed offset (C3 stand-in)
 *   C4  a named undocumented field   gs:[0x28]  (NtTib.ArbitraryUserPointer)
 *
 * Each carrier gets spike 1's twelve persistence cases unchanged, plus five
 * cases this spike exists for: address, contention, thread start, fork, cost.
 * The deliverable is a per-carrier table with costs and hazards, and no
 * recommended column: under AGENTS.md the TLS model is the operator's.
 *
 * Built and driven by measure-gs-tp.sh. See README.md for the method and the
 * verdict rule, both written before the run.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cpuid.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROBE_VERSION "gs-tp-probe 1.0"

/* Negative control. Every carrier here passes, and from outside a probe that
 * saw nothing and a probe that cannot see are the same file. Built with
 * -DSPIKE_BREAK_CARRIER the fetch is biased by one, which no carrier survives:
 * the persistence checks mismatch and the addressing deref lands off the
 * self-word. The test harness watches this fail before it believes the pass. */
#ifdef SPIKE_BREAK_CARRIER
#define BREAK_BIAS 1
#else
#define BREAK_BIAS 0
#endif

/* TEB offsets, every one confirmed against the running kernel before this
 * probe was written rather than taken from a header. */
#define TEB_STACKBASE   0x08	/* NtTib.StackBase                       */
#define TEB_STACKLIMIT  0x10	/* NtTib.StackLimit                      */
#define TEB_ARBITRARY   0x28	/* NtTib.ArbitraryUserPointer  (carrier C4) */
#define TEB_SELF        0x30	/* NtTib.Self == NtCurrentTeb            */
#define TEB_TLSPTR      0x58	/* ThreadLocalStoragePointer   (carrier C2) */
#define TEB_TLSSLOTS    0x1480	/* TlsSlots[0]                 (carrier C1) */

/* The one hardcoded index carrier C1 is. High in the fixed 64 so the sweep in
 * the contention case has somewhere to report room from; the point of the case
 * is that this constant and TlsAlloc draw from the same well. */
#define C1_SLOT 63

/* Carrier C3's stand-in sits one page below the stack base, which is where
 * Cygwin already carves _my_tls out of every thread's stack. A page down from
 * StackBase is committed and unused this early on both the main thread and a
 * fresh CreateThread stack. */
#define C3_PAD 0x1000

/* The glibc TCB the address case builds behind whichever carrier holds its
 * pointer. tcbhead_t sits at TP and above; the static TLS block sits below at
 * negative offsets. These two are the ones a compiler actually emits. */
#define TCB_SELF_OFF   0x00	/* self-pointer, reads back as TP        */
#define TCB_GUARD_OFF  0x28	/* stack-guard word                      */
#define TLS_NEG1_OFF   8	/* a sentinel eight bytes below TP       */
#define TLS_NEG2_OFF   4096	/* a sentinel a page below TP            */

/* ---- raw segment and timing primitives ---------------------------------- */

static inline uint64_t gs_read(unsigned off)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:(%1), %0" : "=r"(v) : "r"((uint64_t)off));
	return v;
}

static inline void gs_write(unsigned off, uint64_t v)
{
	__asm__ __volatile__("movq %0, %%gs:(%1)" : : "r"(v), "r"((uint64_t)off));
}

/* Byte encodings rather than mnemonics, as in the sibling: the 2019 root's
 * assembler needs -mfsgsbase for the names and a spike that stops compiling on
 * the next toolchain has failed at its job. Used only for the %fs cost
 * baseline. */
static inline uint64_t rdfsbase(void)
{
	uint64_t v;
	__asm__ __volatile__(".byte 0xf3,0x48,0x0f,0xae,0xc0" : "=a"(v));
	return v;
}

static inline uint64_t rdtsc(void)
{
	unsigned lo, hi;
	__asm__ __volatile__("lfence; rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

/* ---- carriers ----------------------------------------------------------- */

/*
 * A carrier stores and re-reads a thread pointer for whatever thread calls it,
 * so the same abstraction serves the main thread and every helper. establish()
 * writes a pointer into this thread's carrier; fetch() reads it back through
 * the chain. The value carried is a mapped page holding its own address, the
 * same sentinel the addressing half depends on.
 */
struct carrier {
	const char *id;			/* "C1".."C4"                       */
	const char *name;
	const char *chain;
	int available;
	const char *why;		/* set when available == 0          */
	void (*establish)(uint64_t tp);
	uint64_t (*fetch)(void);
};

/* C1 -- a fixed TlsSlots index. */
static void c1_establish(uint64_t tp) { gs_write(TEB_TLSSLOTS + 8 * C1_SLOT, tp); }
static uint64_t c1_fetch(void) { return gs_read(TEB_TLSSLOTS + 8 * C1_SLOT); }

/* C2 -- the PE TLS directory. The chain is gs:[0x58] then this image's
 * _tls_index, and it exists only if the image carries a TLS directory. The
 * index and the block are discovered from our own PE headers so that no
 * link-time symbol has to resolve; c2_index is -1 when there is no directory,
 * which is what an emulated-TLS toolchain leaves behind. */
static long c2_index = -1;
static unsigned c2_block_off;		/* where in the module block the TP lives */

static void c2_establish(uint64_t tp)
{
	uint64_t tlsptr = gs_read(TEB_TLSPTR);
	uint64_t block = ((uint64_t *)tlsptr)[c2_index];
	*(uint64_t *)(block + c2_block_off) = tp;
}

static uint64_t c2_fetch(void)
{
	uint64_t tlsptr = gs_read(TEB_TLSPTR);
	uint64_t block = ((uint64_t *)tlsptr)[c2_index];
	return *(uint64_t *)(block + c2_block_off);
}

/* C3 -- a word a fixed distance below the stack base, the shape Cygwin's
 * _my_tls already uses. A stand-in: its own word at StackBase-C3_PAD, reached
 * by the chain the real thing would use. */
static void c3_establish(uint64_t tp)
{
	uint64_t base = gs_read(TEB_STACKBASE);
	*(uint64_t *)(base - C3_PAD) = tp;
}

static uint64_t c3_fetch(void)
{
	uint64_t base = gs_read(TEB_STACKBASE);
	return *(uint64_t *)(base - C3_PAD);
}

/* C4 -- NtTib.ArbitraryUserPointer, a named undocumented TEB field. */
static void c4_establish(uint64_t tp) { gs_write(TEB_ARBITRARY, tp); }
static uint64_t c4_fetch(void) { return gs_read(TEB_ARBITRARY); }

static struct carrier carriers[] = {
	{ "C1", "fixed TlsSlots index", "gs:[0x1480+8k]", 1, NULL,
	  c1_establish, c1_fetch },
	{ "C2", "PE TLS directory", "gs:[0x58], then _tls_index", 0, NULL,
	  c2_establish, c2_fetch },
	{ "C3", "word below stack base", "gs:[0x08], then a fixed offset", 1, NULL,
	  c3_establish, c3_fetch },
	{ "C4", "ArbitraryUserPointer", "gs:[0x28]", 1, NULL,
	  c4_establish, c4_fetch },
};
#define NCARRIER ((int)(sizeof carriers / sizeof carriers[0]))

/* Discover this image's PE TLS directory. Data directory entry 9 is the TLS
 * directory; a zero size means the loader gave us no slot, which is the whole
 * finding for C2 under a toolchain that emits emulated TLS. */
static void detect_c2(void)
{
	struct carrier *cr = &carriers[1];
	unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
	IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
	IMAGE_NT_HEADERS64 *nt;
	IMAGE_DATA_DIRECTORY *dd;
	IMAGE_TLS_DIRECTORY64 *tls;

	if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) {
		cr->why = "cannot read our own PE header";
		return;
	}
	nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		cr->why = "cannot read our own PE header";
		return;
	}
	dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
	if (dd->Size == 0 || dd->VirtualAddress == 0) {
		cr->why = "image carries no PE TLS directory "
			  "(this toolchain emits emulated TLS)";
		return;
	}
	tls = (IMAGE_TLS_DIRECTORY64 *)(base + dd->VirtualAddress);
	c2_index = (long)*(ULONG *)tls->AddressOfIndex;
	/* Park the carried pointer past the module's own zero-fill so nothing
	 * the CRT keeps there is trampled. */
	c2_block_off = (unsigned)(tls->EndAddressOfRawData - tls->StartAddressOfRawData) + 64;
	cr->available = 1;
}

/* ---- outcome bookkeeping ------------------------------------------------ */

#define NCASE 13	/* twelve persistence cases plus address */

struct outcome {
	const char *name;
	const char *note;
	unsigned long long checks;
	unsigned long long failures;
	uint64_t want;
	uint64_t got;
	unsigned long long first_fail_at;
	int addressed;
	int ran;
};

/* NCASE tabular cases plus three parked slots for thread start, fork and cost,
 * which carry values rather than a checks/failures pair. */
static struct outcome grid[NCARRIER][NCASE + 3];
static int ncase_for[NCARRIER];
static int debug;

static void trace(const char *what)
{
	if (debug)
		fprintf(stderr, "gs-tp-probe: %s\n", what);
}

static struct outcome *case_open(int carrier, const char *name)
{
	struct outcome *c = &grid[carrier][ncase_for[carrier]++];
	c->name = name;
	c->ran = 1;
	if (debug)
		fprintf(stderr, "gs-tp-probe: %s/%s\n", carriers[carrier].id, name);
	return c;
}

static void fail(struct outcome *c, uint64_t want, uint64_t got)
{
	if (!c->failures) {
		c->want = want;
		c->got = got;
		c->first_fail_at = c->checks;
	}
	c->failures++;
}

/* The persistence check: read the carried pointer back and compare. Cheaper
 * than dereferencing, and the addressing question is settled once in its own
 * case under a guard, exactly as the sibling settled it for %fs:0. */
static void check(const struct carrier *cr, struct outcome *c, uint64_t want)
{
	uint64_t got = cr->fetch() + BREAK_BIAS;

	c->checks++;
	if (got != want)
		fail(c, want, got);
}

/* ---- fault handling and sentinels --------------------------------------- */

static sigjmp_buf fault_return;
static volatile sig_atomic_t fault_signo;

static void catch_fault(int signo)
{
	fault_signo = signo;
	siglongjmp(fault_return, 1);
}

static void install(int signo, void (*handler)(int))
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NODEFER;
	sigaction(signo, &sa, NULL);
}

/* A mapped page carrying its own address at offset zero. Nothing frees these;
 * the process is short-lived and a pointer into unmapped memory is a defect the
 * probe would then have to tell apart from the one it is hunting. */
static uint64_t sentinel_page(void)
{
	void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (p == MAP_FAILED) {
		fprintf(stderr, "gs-tp-probe: mmap: %s\n", strerror(errno));
		exit(1);
	}
	*(uint64_t *)p = (uint64_t)p;
	return (uint64_t)p;
}

/* ---- context switch accounting (borrowed unchanged from spike 1) -------- */

#define SPI_NEXT_ENTRY	0
#define SPI_NTHREADS	4
#define SPI_PID		80
#define SPI_THREADS	256
#define STI_STRIDE	80
#define STI_SWITCHES	64

typedef LONG (WINAPI *nqsi_fn)(ULONG, PVOID, ULONG, PULONG);

static int context_switches(unsigned long long *out)
{
	static nqsi_fn nqsi;
	unsigned char *buf;
	ULONG len = 512 * 1024;
	LONG st;
	int tries;

	if (!nqsi) {
		HMODULE h = GetModuleHandleA("ntdll.dll");
		if (!h)
			return -1;
		nqsi = (nqsi_fn)(void *)GetProcAddress(h, "NtQuerySystemInformation");
		if (!nqsi)
			return -1;
	}
	for (tries = 0; tries < 8; tries++) {
		ULONG need = 0;

		buf = malloc(len);
		if (!buf)
			return -1;
		st = nqsi(5, buf, len, &need);
		if (st >= 0)
			break;
		free(buf);
		if (st != (LONG)0xC0000004)
			return -1;
		len = need ? need + 64 * 1024 : len * 2;
	}
	if (tries == 8)
		return -1;
	{
		unsigned char *p = buf;
		DWORD me = GetCurrentProcessId();

		for (;;) {
			ULONG next = *(ULONG *)(p + SPI_NEXT_ENTRY);
			ULONG n = *(ULONG *)(p + SPI_NTHREADS);
			uintptr_t pid = *(uintptr_t *)(p + SPI_PID);

			if ((DWORD)pid == me && n && n < 4096) {
				unsigned long long sum = 0;
				ULONG i;

				for (i = 0; i < n; i++)
					sum += *(ULONG *)(p + SPI_THREADS +
							  (size_t)i * STI_STRIDE +
							  STI_SWITCHES);
				free(buf);
				*out = sum;
				return 0;
			}
			if (!next)
				break;
			p += next;
		}
	}
	free(buf);
	return -1;
}

/* ---- the twelve persistence cases, each parameterised by carrier -------- */

/* The active carrier for the helper-thread cases, which cannot take it as an
 * argument through a Windows thread start routine. Set before each carrier's
 * pass and read by the spinner, apc, signal and load bodies. */
static const struct carrier *active;

static void case_round_trip(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "round trip");
	uint64_t tp = sentinel_page();
	unsigned long i;

	for (i = 0; i < rounds; i++) {
		cr->establish(tp);
		check(cr, c, tp);
	}
	c->addressed = 0;	/* addressing is its own case here */
}

static void case_syscall(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "syscall");
	uint64_t tp = sentinel_page();
	FILETIME a, b, d, e;
	unsigned long i;

	cr->establish(tp);
	for (i = 0; i < rounds; i++) {
		GetProcessTimes(GetCurrentProcess(), &a, &b, &d, &e);
		check(cr, c, tp);
	}
}

static void case_yields(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "yields");
	uint64_t tp = sentinel_page();
	unsigned long i;

	cr->establish(tp);
	for (i = 0; i < rounds; i++) {
		if (i % 512 == 3)
			Sleep(1);
		else if (i & 1)
			SwitchToThread();
		else
			Sleep(0);
		check(cr, c, tp);
	}
}

struct pingpong {
	HANDLE call, back;
	unsigned long rounds;
};

static DWORD WINAPI pingpong_peer(LPVOID arg)
{
	struct pingpong *p = arg;
	unsigned long i;

	for (i = 0; i < p->rounds; i++) {
		WaitForSingleObject(p->call, INFINITE);
		SetEvent(p->back);
	}
	return 0;
}

static void case_blocking_wait(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "blocking wait");
	uint64_t tp = sentinel_page();
	struct pingpong p;
	HANDLE peer;
	unsigned long i;

	p.call = CreateEvent(NULL, FALSE, FALSE, NULL);
	p.back = CreateEvent(NULL, FALSE, FALSE, NULL);
	p.rounds = rounds;
	peer = CreateThread(NULL, 0, pingpong_peer, &p, 0, NULL);
	if (!p.call || !p.back || !peer) {
		c->note = "could not create the event pair";
		return;
	}
	cr->establish(tp);
	for (i = 0; i < rounds; i++) {
		SetEvent(p.call);
		WaitForSingleObject(p.back, INFINITE);
		check(cr, c, tp);
	}
	WaitForSingleObject(peer, 5000);
	CloseHandle(peer);
	CloseHandle(p.call);
	CloseHandle(p.back);
}

static void case_migration(const struct carrier *cr, int ci, unsigned long rounds, int ncpu)
{
	struct outcome *c = case_open(ci, "migration");
	uint64_t tp = sentinel_page();
	DWORD_PTR original;
	unsigned long i;

	original = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1);
	if (!original) {
		c->note = "affinity is not ours to set";
		return;
	}
	cr->establish(tp);
	for (i = 0; i < rounds; i++) {
		DWORD_PTR mask = (DWORD_PTR)1 << (i % (unsigned)ncpu);

		SetThreadAffinityMask(GetCurrentThread(), mask);
		SwitchToThread();
		check(cr, c, tp);
	}
	SetThreadAffinityMask(GetCurrentThread(), original);
}

static struct outcome *apc_case;
static uint64_t apc_tp;

static VOID CALLBACK apc_body(ULONG_PTR arg)
{
	(void)arg;
	check(active, apc_case, apc_tp);
}

static void case_apc(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "apc");
	unsigned long i;

	apc_case = c;
	apc_tp = sentinel_page();
	cr->establish(apc_tp);
	for (i = 0; i < rounds; i++) {
		if (!QueueUserAPC(apc_body, GetCurrentThread(), 0)) {
			c->note = "QueueUserAPC refused";
			return;
		}
		SleepEx(0, TRUE);
		check(cr, c, apc_tp);
	}
}

struct spinner {
	struct outcome *c;
	uint64_t tp;
	volatile LONG ready;
	volatile LONG stop;
};

static DWORD WINAPI spinner_body(LPVOID arg)
{
	struct spinner *s = arg;

	s->tp = sentinel_page();
	active->establish(s->tp);
	InterlockedExchange((LONG *)&s->ready, 1);
	while (!s->stop) {
		check(active, s->c, s->tp);
		SwitchToThread();
	}
	return 0;
}

/* Suspend a running thread, read its context back, write it back, resume. This
 * is how Cygwin delivers a signal, and it is where a kernel that rewrote
 * segment-derived state on the way out would show it. */
static void case_hijack(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "hijack");
	struct spinner s;
	CONTEXT ctx __attribute__((aligned(16)));
	HANDLE t;
	unsigned long i;

	(void)cr;	/* the spinner establishes through `active` on its thread */
	memset(&s, 0, sizeof s);
	s.c = c;
	t = CreateThread(NULL, 0, spinner_body, &s, 0, NULL);
	if (!t) {
		c->note = "could not create the thread";
		return;
	}
	while (!s.ready)
		Sleep(0);
	for (i = 0; i < rounds; i++) {
		if (SuspendThread(t) == (DWORD)-1) {
			c->note = "SuspendThread refused";
			break;
		}
		memset(&ctx, 0, sizeof ctx);
		ctx.ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS;
		if (GetThreadContext(t, &ctx))
			SetThreadContext(t, &ctx);
		ResumeThread(t);
		SwitchToThread();
	}
	InterlockedExchange((LONG *)&s.stop, 1);
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
}

static struct outcome *sig_case;
static uint64_t sig_tp;

static void sig_check(int signo)
{
	(void)signo;
	check(active, sig_case, sig_tp);
}

static void case_signal_sync(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "signal, sync");
	unsigned long i;

	sig_case = c;
	sig_tp = sentinel_page();
	install(SIGUSR1, sig_check);
	cr->establish(sig_tp);
	for (i = 0; i < rounds; i++) {
		raise(SIGUSR1);
		check(cr, c, sig_tp);
	}
	install(SIGUSR1, SIG_DFL);
}

static void fault_check(int signo)
{
	fault_signo = signo;
	check(active, sig_case, sig_tp);
	siglongjmp(fault_return, 1);
}

/* A real hardware fault, turned into a signal through Cygwin's vectored
 * handler, which is the longest path through the kernel here and the one a TLS
 * access itself would take when it went wrong. */
static void case_signal_fault(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "signal, fault");
	unsigned long i;

	sig_case = c;
	sig_tp = sentinel_page();
	install(SIGSEGV, fault_check);
	cr->establish(sig_tp);
	for (i = 0; i < rounds; i++) {
		if (sigsetjmp(fault_return, 1) == 0)
			*(volatile int *)0 = 1;
		check(cr, c, sig_tp);
	}
	install(SIGSEGV, SIG_DFL);
}

static struct {
	struct outcome *c;
	uint64_t tp;
	volatile int ready;
	volatile int stop;
} async;

static void async_handler(int signo)
{
	(void)signo;
	check(active, async.c, async.tp);
}

static void *async_body(void *arg)
{
	(void)arg;
	async.tp = sentinel_page();
	active->establish(async.tp);
	async.ready = 1;
	while (!async.stop)
		check(active, async.c, async.tp);
	return NULL;
}

static void case_signal_async(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "signal, async");
	pthread_t tid;
	unsigned long i;

	(void)cr;
	async.c = c;
	async.ready = 0;
	async.stop = 0;
	install(SIGUSR2, async_handler);
	if (pthread_create(&tid, NULL, async_body, NULL) != 0) {
		c->note = "could not create the thread";
		return;
	}
	while (!async.ready)
		sched_yield();
	for (i = 0; i < rounds; i++) {
		pthread_kill(tid, SIGUSR2);
		SwitchToThread();
	}
	async.stop = 1;
	pthread_join(tid, NULL);
	install(SIGUSR2, SIG_DFL);
}

static volatile int preempt_stop;
static double preempt_rate;

static void *burner_body(void *arg)
{
	volatile unsigned long long *sink = arg;

	while (!preempt_stop)
		(*sink)++;
	return NULL;
}

/* The one case that makes no system call. Its thread spins on the fetch while a
 * burner sits on every processor, so the only thing that can befall it is being
 * taken off a processor and put back. On %fs this is where the base died with
 * no call site to blame; on %gs the carrier is backed by memory the kernel does
 * not own, so this is the case that should hold if any does. */
static void case_preemption(const struct carrier *cr, int ci, int seconds, int ncpu)
{
	struct outcome *c = case_open(ci, "preemption");
	pthread_t *burner = calloc((size_t)ncpu, sizeof *burner);
	unsigned long long sink = 0;
	uint64_t tp = sentinel_page();
	LARGE_INTEGER hz, t0, t1;
	time_t end;
	int started = 0, i;

	if (!burner) {
		c->note = "out of memory";
		return;
	}
	preempt_stop = 0;
	for (i = 0; i < ncpu; i++)
		if (pthread_create(&burner[i], NULL, burner_body, &sink) == 0)
			started++;
	QueryPerformanceFrequency(&hz);
	QueryPerformanceCounter(&t0);
	cr->establish(tp);
	end = time(NULL) + seconds;
	for (;;) {
		for (i = 0; i < 200000; i++)
			check(cr, c, tp);
		if (time(NULL) >= end)
			break;
	}
	QueryPerformanceCounter(&t1);
	if (t1.QuadPart > t0.QuadPart && hz.QuadPart)
		preempt_rate = (double)c->checks * (double)hz.QuadPart /
			       (double)(t1.QuadPart - t0.QuadPart);
	preempt_stop = 1;
	for (i = 0; i < started; i++)
		pthread_join(burner[i], NULL);
	free(burner);
}

static volatile int load_stop;

struct load_slot {
	struct outcome out;
	const struct carrier *cr;
};

static void *load_body(void *arg)
{
	struct load_slot *ls = arg;
	uint64_t tp = sentinel_page();

	ls->cr->establish(tp);
	while (!load_stop) {
		int i;

		for (i = 0; i < 64; i++)
			check(ls->cr, &ls->out, tp);
		SwitchToThread();
	}
	return NULL;
}

/* Every worker holds a pointer no other worker holds, so a mismatch says which
 * of the two bad outcomes happened: a carrier cleared, or a carrier crossed
 * with a neighbour's. Those want different repairs. */
static void case_load(const struct carrier *cr, int ci, int nthreads, int seconds,
		      unsigned long long *switches)
{
	struct outcome *c = case_open(ci, "load");
	struct load_slot *slot = calloc((size_t)nthreads, sizeof *slot);
	pthread_t *tid = calloc((size_t)nthreads, sizeof *tid);
	unsigned long long before = 0, after = 0;
	int started = 0, i;

	if (!slot || !tid) {
		c->note = "out of memory";
		return;
	}
	load_stop = 0;
	for (i = 0; i < nthreads; i++)
		slot[i].cr = cr;
	if (context_switches(&before) != 0)
		before = 0;
	for (i = 0; i < nthreads; i++) {
		if (pthread_create(&tid[i], NULL, load_body, &slot[i]) == 0)
			started++;
		else
			break;
	}
	if (!started) {
		c->note = "no worker thread would start";
		return;
	}
	Sleep((DWORD)seconds * 1000);
	if (context_switches(&after) == 0 && before && after > before)
		*switches = after - before;
	load_stop = 1;
	for (i = 0; i < started; i++)
		pthread_join(tid[i], NULL);
	for (i = 0; i < started; i++) {
		c->checks += slot[i].out.checks;
		if (slot[i].out.failures && !c->failures) {
			c->want = slot[i].out.want;
			c->got = slot[i].out.got;
			c->first_fail_at = slot[i].out.first_fail_at;
		}
		c->failures += slot[i].out.failures;
	}
	free(slot);
	free(tid);
}

/* ---- the addressing case ------------------------------------------------ */

/* Build a real glibc-shaped block behind the carrier and read four places
 * through it under a guard: the self-pointer at TP+0, the guard word at
 * TP+0x28, a sentinel eight bytes below TP, and one a page below. A chain that
 * goes between the fetch and the dereference takes the thread with it, so the
 * whole read runs inside sigsetjmp. */
static void case_address(const struct carrier *cr, int ci, unsigned long rounds)
{
	struct outcome *c = case_open(ci, "address");
	unsigned char *region;
	uint64_t tp, guard = 0x5555AAAA5555AAAAULL;
	uint64_t s1 = 0x1111111111111111ULL, s2 = 0x2222222222222222ULL;
	unsigned long i;

	/* Two pages: TP is the second page's start, so TP-8 and TP-4096 sit in
	 * the first page and TP..TP+0x28 in the second. */
	region = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region == MAP_FAILED) {
		c->note = "could not map the TLS block";
		return;
	}
	tp = (uint64_t)(region + 4096);
	*(uint64_t *)(tp + TCB_SELF_OFF)  = tp;
	*(uint64_t *)(tp + TCB_GUARD_OFF) = guard;
	*(uint64_t *)(tp - TLS_NEG1_OFF)  = s1;
	*(uint64_t *)(tp - TLS_NEG2_OFF)  = s2;

	install(SIGSEGV, catch_fault);
	for (i = 0; i < rounds; i++) {
		volatile int bad = 0;

		cr->establish(tp);
		if (sigsetjmp(fault_return, 1) == 0) {
			uint64_t p = cr->fetch() + BREAK_BIAS;

			if (*(uint64_t *)(p + TCB_SELF_OFF)  != p)     bad = 1;
			if (*(uint64_t *)(p + TCB_GUARD_OFF) != guard) bad = 1;
			if (*(uint64_t *)(p - TLS_NEG1_OFF)  != s1)    bad = 1;
			if (*(uint64_t *)(p - TLS_NEG2_OFF)  != s2)    bad = 1;
		} else {
			bad = 1;
		}
		c->checks++;
		if (bad)
			fail(c, tp, 0);
		else
			c->addressed = 1;
	}
	install(SIGSEGV, SIG_DFL);
}

/* ---- observations: contention, thread start, fork, cost ----------------- */

static const char *note_name[24];
static char note_text[24][200];
static int nnotes;

static void note_add(const char *name, const char *fmt, ...)
{
	va_list ap;

	if (nnotes == 24)
		return;
	note_name[nnotes] = name;
	va_start(ap, fmt);
	vsnprintf(note_text[nnotes], sizeof note_text[0], fmt, ap);
	va_end(ap);
	nnotes++;
}

/* Contention is C1's alone: who else wants a TlsSlots index. Sweep TlsAlloc at
 * process start, load DLLs the box actually has, sweep again, and report the
 * lowest index still free with any index under 64 consumed between the sweeps.
 * No pass or fail -- a number the operator sizes C1 against. */
static int sweep(unsigned char seen[64])
{
	DWORD got[64];
	int n = 0, lowest = -1, i;

	memset(seen, 0, 64);
	while (n < 64) {
		DWORD idx = TlsAlloc();

		if (idx == TLS_OUT_OF_INDEXES)
			break;
		got[n++] = idx;
		if (idx < 64) {
			seen[idx] = 1;
			if (lowest < 0)
				lowest = (int)idx;
		}
	}
	for (i = 0; i < n; i++)
		TlsFree(got[i]);
	return lowest;
}

static void observe_contention(void)
{
	static const char *dlls[] = {
		"crypt32.dll", "wininet.dll", "ws2_32.dll", "bcrypt.dll",
		"ole32.dll", "oleaut32.dll", "shell32.dll", "winhttp.dll",
		"dbghelp.dll", "userenv.dll",
	};
	unsigned char before[64], after[64];
	int lo_before, lo_after, loaded = 0, consumed = 0, i;
	char list[160];
	size_t used = 0;

	lo_before = sweep(before);
	for (i = 0; i < (int)(sizeof dlls / sizeof dlls[0]); i++)
		if (LoadLibraryA(dlls[i]))
			loaded++;
	lo_after = sweep(after);

	list[0] = '\0';
	for (i = 0; i < 64; i++)
		if (after[i] && !before[i]) {
			int k = snprintf(list + used, sizeof list - used,
					 "%s%d", used ? "," : "", i);
			if (k > 0 && used + (size_t)k < sizeof list)
				used += (size_t)k;
			consumed++;
		}
	note_add("contention", "lowest free index %d at start, %d after loading "
		 "%d DLLs; %d fixed-range indices consumed%s%s",
		 lo_before, lo_after, loaded, consumed,
		 consumed ? " at " : "", consumed ? list : "");
	note_add("contention", "C1's constant is index %d; TlsAlloc hands out from "
		 "the same 64 and any injected DLL draws first", C1_SLOT);
}

static volatile uint64_t start_seen;
static volatile uint64_t start_cycles;
static const struct carrier *start_cr;

static DWORD WINAPI thread_start_body(LPVOID arg)
{
	uint64_t t0 = rdtsc();
	uint64_t tp;

	(void)arg;
	start_seen = start_cr->fetch();	/* what the carrier holds at entry */
	tp = sentinel_page();
	start_cr->establish(tp);		/* the hook this project owns */
	(void)start_cr->fetch();
	start_cycles = rdtsc() - t0;
	return 0;
}

/* Lifecycle, thread start. Record what the carrier holds at the first
 * instruction of a fresh thread, then re-establish and price the window. A
 * carrier with a reachable hook passes; the value at entry says whether a TLS
 * access could have happened before the hook ran. */
static void observe_thread_start(const struct carrier *cr, int ci)
{
	struct outcome *c = &grid[ci][NCASE];	/* parked past the case grid */
	HANDLE t;

	c->name = "thread start";
	c->ran = 1;
	start_cr = cr;
	start_seen = ~(uint64_t)0;
	start_cycles = 0;
	t = CreateThread(NULL, 0, thread_start_body, NULL, 0, NULL);
	if (!t) {
		c->note = "could not create the thread";
		return;
	}
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
	c->want = start_seen;
	c->first_fail_at = start_cycles;
	note_add("thread start",
		 "%s: entry holds 0x%llx, hook re-established in %llu cycles "
		 "(hook: thread entry, reachable)",
		 cr->id, (unsigned long long)start_seen,
		 (unsigned long long)start_cycles);
}

static volatile uint64_t fork_seen;
static volatile uint64_t fork_cycles;

/* Lifecycle, fork. The child gets a fresh TEB, so every carrier that lives in
 * the TEB or on the stack starts the child empty; the case records that and
 * prices re-establishing it in the child, which is a call site this project
 * owns through pthread_atfork. */
static void observe_fork(const struct carrier *cr, int ci)
{
	struct outcome *c = &grid[ci][NCASE + 1];
	uint64_t parent_tp = sentinel_page();
	uint64_t msg[2];
	int fd[2];
	pid_t pid;

	c->name = "fork";
	c->ran = 1;
	if (pipe(fd) != 0) {
		c->note = "pipe failed";
		return;
	}
	cr->establish(parent_tp);
	pid = fork();
	if (pid == 0) {
		uint64_t t0 = rdtsc(), tp;

		msg[0] = cr->fetch();		/* what the child inherited */
		tp = sentinel_page();
		cr->establish(tp);
		(void)cr->fetch();
		msg[1] = rdtsc() - t0;
		if (write(fd[1], msg, sizeof msg) != (ssize_t)sizeof msg)
			_exit(2);
		_exit(0);
	}
	close(fd[1]);
	if (pid < 0) {
		c->note = "fork failed";
		close(fd[0]);
		return;
	}
	if (read(fd[0], msg, sizeof msg) != (ssize_t)sizeof msg) {
		c->note = "the child said nothing";
	} else {
		fork_seen = msg[0];
		fork_cycles = msg[1];
		c->want = fork_seen;
		c->first_fail_at = fork_cycles;
		note_add("fork",
			 "%s: child inherits 0x%llx, hook re-established in %llu "
			 "cycles (hook: post-fork, reachable)",
			 cr->id, (unsigned long long)fork_seen,
			 (unsigned long long)fork_cycles);
	}
	close(fd[0]);
	waitpid(pid, NULL, 0);
}

/* ---- cost --------------------------------------------------------------- */

static volatile uint64_t cost_global;
static __thread uint64_t cost_emutls;	/* a real emulated-TLS thread-local */

static double time_reads(uint64_t (*read1)(void), unsigned long iters)
{
	uint64_t t0, t1, acc = 0;
	unsigned long i;

	t0 = rdtsc();
	for (i = 0; i < iters; i++)
		acc += read1();
	t1 = rdtsc();
	__asm__ __volatile__("" : : "r"(acc));	/* keep the loop */
	return (double)(t1 - t0) / (double)iters;
}

static uint64_t read_global(void) { return cost_global; }
static uint64_t read_emutls(void) { return cost_emutls; }
static uint64_t read_fs(void)     { return rdfsbase(); }

static const struct carrier *cost_cr;
static uint64_t cost_tp;

/* One thread-local read through the active carrier's chain: fetch the pointer,
 * dereference the self-word. This is the sequence a compiled TLS access would
 * become if this carrier were the model. */
static uint64_t read_carrier(void)
{
	return *(uint64_t *)cost_cr->fetch();
}

static double cost_global_cps, cost_emutls_cps, cost_fs_cps;
static int cost_fs_ok;

static void measure_baselines(unsigned long iters, int fs_ok)
{
	cost_global = 0xC0FFEE;
	cost_emutls = 0xC0FFEE;
	cost_global_cps = time_reads(read_global, iters);
	cost_emutls_cps = time_reads(read_emutls, iters);
	cost_fs_ok = fs_ok;
	if (fs_ok)
		cost_fs_cps = time_reads(read_fs, iters);
}

static void observe_cost(const struct carrier *cr, int ci, unsigned long iters)
{
	double cps;

	(void)ci;
	cost_cr = cr;
	cost_tp = sentinel_page();
	cr->establish(cost_tp);
	cps = time_reads(read_carrier, iters);
	note_add("cost", "%s: %.1f cycles/access", cr->id, cps);
	grid[ci][NCASE + 2].name = "cost";
	grid[ci][NCASE + 2].ran = 1;
	grid[ci][NCASE + 2].first_fail_at = (unsigned long long)(cps * 10.0);
}

/* ---- verdict and report ------------------------------------------------- */

static const char *carrier_verdict(int ci, int *persist_fail, int *addr_fail,
				   int *life_fail)
{
	struct carrier *cr = &carriers[ci];
	int i;

	*persist_fail = *addr_fail = *life_fail = 0;
	if (!cr->available)
		return "unavailable";

	for (i = 0; i < ncase_for[ci]; i++) {
		struct outcome *c = &grid[ci][i];

		if (!strcmp(c->name, "address")) {
			if (c->note || c->failures)
				*addr_fail = 1;
		} else if (c->note || c->failures) {
			*persist_fail = 1;
		}
	}
	/* thread start and fork carry no failure count; a note means the hook
	 * was not reachable. */
	if (grid[ci][NCASE].note || grid[ci][NCASE + 1].note)
		*life_fail = 1;

	if (*persist_fail || *addr_fail || *life_fail)
		return "fail";
	return "pass";
}

static void report(FILE *out, int terse, int nthreads, int seconds,
		   unsigned long long switches)
{
	unsigned long long checks = 0, failures = 0;
	int passed = 0, failed = 0, unavail = 0, ci, i;

	if (!terse) {
		fprintf(out, "== carriers\n\n");
		fprintf(out, "    %-4s %-24s %-30s %s\n",
			"", "carrier", "chain", "available");
		for (ci = 0; ci < NCARRIER; ci++) {
			struct carrier *cr = &carriers[ci];

			fprintf(out, "    %-4s %-24s %-30s %s\n",
				cr->id, cr->name, cr->chain,
				cr->available ? "yes" : "no");
			if (!cr->available && cr->why)
				fprintf(out, "         %s\n", cr->why);
		}
		fprintf(out, "\n    Each chain was confirmed against the running kernel\n"
			     "    before the probe was written. C3 is a stand-in for the\n"
			     "    forked runtime's block, measured at the same distance\n"
			     "    below the stack base by the same chain.\n\n");

		for (ci = 0; ci < NCARRIER; ci++) {
			struct carrier *cr = &carriers[ci];

			fprintf(out, "== %s  %s\n\n", cr->id, cr->name);
			if (!cr->available) {
				fprintf(out, "    not measured: %s\n\n",
					cr->why ? cr->why : "unavailable");
				continue;
			}
			fprintf(out, "    %-16s %14s %14s\n", "", "checks", "failures");
			for (i = 0; i < ncase_for[ci]; i++) {
				struct outcome *c = &grid[ci][i];

				if (c->note)
					fprintf(out, "    %-16s %14s  %s\n",
						c->name, "-", c->note);
				else
					fprintf(out, "    %-16s %14llu %14llu\n",
						c->name, c->checks, c->failures);
			}
			fputc('\n', out);
			for (i = 0; i < ncase_for[ci]; i++) {
				struct outcome *c = &grid[ci][i];

				if (c->note || !c->failures)
					continue;
				fprintf(out, "    %-16s wanted 0x%llx  got 0x%llx  at check %llu\n",
					c->name, (unsigned long long)c->want,
					(unsigned long long)c->got, c->first_fail_at);
			}
			fputc('\n', out);
		}

		fprintf(out, "== observations\n\n");
		for (i = 0; i < nnotes; i++)
			fprintf(out, "    %-14s %s\n", note_name[i], note_text[i]);
		fprintf(out, "\n    Cost baselines: global %.1f, emulated-TLS %.1f",
			cost_global_cps, cost_emutls_cps);
		if (cost_fs_ok)
			fprintf(out, ", %%fs read %.1f", cost_fs_cps);
		fprintf(out, " cycles/access.\n");
		fprintf(out, "    Thread start and fork carry no pass or fail; the value at\n"
			     "    entry is what would be read before a hook runs, and every\n"
			     "    carrier here starts the new context empty.\n\n");

		fprintf(out, "== summary\n\n");
	}

	for (ci = 0; ci < NCARRIER; ci++) {
		int pf, af, lf;
		const char *v = carrier_verdict(ci, &pf, &af, &lf);

		if (!strcmp(v, "pass")) passed++;
		else if (!strcmp(v, "unavailable")) unavail++;
		else failed++;
		for (i = 0; i < ncase_for[ci]; i++) {
			checks += grid[ci][i].checks;
			failures += grid[ci][i].failures;
		}
	}

	for (ci = 0; ci < NCARRIER; ci++) {
		int pf, af, lf;
		const char *v = carrier_verdict(ci, &pf, &af, &lf);

		fprintf(out, "%s%s=%s\n", terse ? "" : "    ", carriers[ci].id, v);
	}
	fprintf(out, "%scarriers_pass=%d\n", terse ? "" : "    ", passed);
	fprintf(out, "%scarriers_fail=%d\n", terse ? "" : "    ", failed);
	fprintf(out, "%scarriers_unavailable=%d\n", terse ? "" : "    ", unavail);

	/* The shape line is the part a rerun diffs: one carrier/case:state per
	 * pair, in order, over the persistence cases and address. */
	fprintf(out, "%sshape=", terse ? "" : "    ");
	{
		int first = 1;

		for (ci = 0; ci < NCARRIER; ci++) {
			if (!carriers[ci].available) {
				fprintf(out, "%s%s/*:unavail", first ? "" : ",",
					carriers[ci].id);
				first = 0;
				continue;
			}
			for (i = 0; i < ncase_for[ci]; i++) {
				struct outcome *c = &grid[ci][i];

				fprintf(out, "%s%s/%s:%s", first ? "" : ",",
					carriers[ci].id, c->name,
					c->note ? "unrun" : c->failures ? "fail" : "pass");
				first = 0;
			}
		}
	}
	fputc('\n', out);

	fprintf(out, "%schecks=%llu\n", terse ? "" : "    ", checks);
	fprintf(out, "%sfailures=%llu\n", terse ? "" : "    ", failures);
	if (switches)
		fprintf(out, "%sswitches_under_load=%llu\n", terse ? "" : "    ", switches);
	else
		fprintf(out, "%sswitches_under_load=unavailable\n", terse ? "" : "    ");
	if (preempt_rate > 0)
		fprintf(out, "%spreemption_checks_per_second=%.0f\n",
			terse ? "" : "    ", preempt_rate);
	fprintf(out, "%scost_global_cycles=%.1f\n", terse ? "" : "    ", cost_global_cps);
	fprintf(out, "%scost_emutls_cycles=%.1f\n", terse ? "" : "    ", cost_emutls_cps);
	if (cost_fs_ok)
		fprintf(out, "%scost_fs_cycles=%.1f\n", terse ? "" : "    ", cost_fs_cps);
	for (ci = 0; ci < NCARRIER; ci++)
		if (carriers[ci].available)
			fprintf(out, "%scost_%s_cycles=%.1f\n", terse ? "" : "    ",
				carriers[ci].id,
				(double)grid[ci][NCASE + 2].first_fail_at / 10.0);
	fprintf(out, "%sload_threads=%d\n", terse ? "" : "    ", nthreads);
	fprintf(out, "%sload_seconds=%d\n", terse ? "" : "    ", seconds);
	fprintf(out, "%sprobe=%s\n", terse ? "" : "    ", PROBE_VERSION);
}

/* ---- capability gate for the %fs baseline ------------------------------- */

static int fs_executes(void)
{
	volatile int ok = 0;

	install(SIGILL, catch_fault);
	fault_signo = 0;
	if (sigsetjmp(fault_return, 1) == 0) {
		(void)rdfsbase();
		ok = 1;
	}
	install(SIGILL, SIG_DFL);
	return ok;
}

/* ---- driver ------------------------------------------------------------- */

static void usage(FILE *out)
{
	fputs("Usage:\n"
	      "  gs-tp-probe [options]\n"
	      "\n"
	      "Options:\n"
	      "  -r N, --rounds=N    Rounds for the cheap cases. [default: 20000]\n"
	      "  -j N, --threads=N   Threads in the load case. [default: two per cpu]\n"
	      "  -s N, --seconds=N   Length of the timed cases. [default: 10]\n"
	      "  -C ID, --carrier=ID Measure one carrier (C1..C4) instead of all.\n"
	      "  -t, --terse         The summary block alone, one key=value per line.\n"
	      "  -d, --debug         Name each case on stderr as it starts.\n"
	      "  -V, --version       Print the version and exit.\n"
	      "  -h, --help          Print this message and exit.\n", out);
}

static unsigned long share(unsigned long rounds, unsigned long divisor,
			   unsigned long least)
{
	unsigned long n = rounds / divisor;

	return n < least ? least : n;
}

static long numeric(const char *what, const char *s)
{
	char *end;
	long v;

	errno = 0;
	v = strtol(s, &end, 10);
	if (errno || !*s || *end || v <= 0) {
		fprintf(stderr, "gs-tp-probe: %s wants a positive number, not %s\n",
			what, s);
		exit(2);
	}
	return v;
}

static void run_carrier(int ci, unsigned long rounds, int seconds, int ncpu,
			int nthreads, unsigned long long *switches)
{
	struct carrier *cr = &carriers[ci];

	if (!cr->available)
		return;
	active = cr;

	case_round_trip(cr, ci, rounds);
	case_syscall(cr, ci, rounds);
	case_yields(cr, ci, rounds);
	case_blocking_wait(cr, ci, rounds);
	case_migration(cr, ci, share(rounds, 10, 100), ncpu);
	case_apc(cr, ci, share(rounds, 10, 100));
	case_hijack(cr, ci, share(rounds, 5, 200));
	case_signal_sync(cr, ci, share(rounds, 10, 100));
	case_signal_fault(cr, ci, share(rounds, 100, 50));
	case_signal_async(cr, ci, share(rounds, 50, 50));
	case_preemption(cr, ci, seconds, ncpu);
	case_load(cr, ci, nthreads, seconds, switches);
	case_address(cr, ci, share(rounds, 10, 100));

	if (ci == 0)			/* contention is C1's question alone */
		observe_contention();
	trace("thread start");
	observe_thread_start(cr, ci);
	trace("fork");
	observe_fork(cr, ci);
	trace("cost");
	observe_cost(cr, ci, 2000000);
}

int main(int argc, char **argv)
{
	unsigned long long switches = 0;
	unsigned long rounds = 20000;
	int seconds = 10, nthreads = 0, terse = 0, only = -1;
	int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
	int fs_ok, i, ci;

	if (ncpu < 1)
		ncpu = 1;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];

		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			puts(PROBE_VERSION);
			return 0;
		} else if (!strcmp(a, "-t") || !strcmp(a, "--terse")) {
			terse = 1;
		} else if (!strcmp(a, "-d") || !strcmp(a, "--debug")) {
			debug = 1;
		} else if (!strcmp(a, "-r") && i + 1 < argc) {
			rounds = (unsigned long)numeric("--rounds", argv[++i]);
		} else if (!strncmp(a, "--rounds=", 9)) {
			rounds = (unsigned long)numeric("--rounds", a + 9);
		} else if (!strcmp(a, "-j") && i + 1 < argc) {
			nthreads = (int)numeric("--threads", argv[++i]);
		} else if (!strncmp(a, "--threads=", 10)) {
			nthreads = (int)numeric("--threads", a + 10);
		} else if (!strcmp(a, "-s") && i + 1 < argc) {
			seconds = (int)numeric("--seconds", argv[++i]);
		} else if (!strncmp(a, "--seconds=", 10)) {
			seconds = (int)numeric("--seconds", a + 10);
		} else if ((!strcmp(a, "-C") || !strcmp(a, "--carrier")) && i + 1 < argc) {
			a = argv[++i];
			goto pick;
		} else if (!strncmp(a, "--carrier=", 10)) {
			a += 10;
pick:
			for (ci = 0; ci < NCARRIER; ci++)
				if (!strcasecmp(a, carriers[ci].id))
					only = ci;
			if (only < 0) {
				fprintf(stderr, "gs-tp-probe: no carrier named %s\n", a);
				return 2;
			}
		} else {
			fprintf(stderr, "gs-tp-probe: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
	}
	if (!nthreads)
		nthreads = ncpu * 2;

	setvbuf(stdout, NULL, _IOLBF, 0);

	detect_c2();
	if (only >= 0)
		for (ci = 0; ci < NCARRIER; ci++)
			if (ci != only)
				carriers[ci].available = 0,
				carriers[ci].why = "not selected";

	fs_ok = fs_executes();
	measure_baselines(2000000, fs_ok);

	for (ci = 0; ci < NCARRIER; ci++)
		run_carrier(ci, rounds, seconds, ncpu, nthreads, &switches);

	report(stdout, terse, nthreads, seconds, switches);

	/* Exit code: 3 if any available carrier failed, 4 if a case could not
	 * run, 0 if every available carrier passed. Unavailable is not a
	 * failure of the probe; it is a measured row. */
	{
		int any_fail = 0, any_unrun = 0;

		for (ci = 0; ci < NCARRIER; ci++) {
			int pf, af, lf;
			const char *v;

			if (!carriers[ci].available)
				continue;
			v = carrier_verdict(ci, &pf, &af, &lf);
			if (!strcmp(v, "fail"))
				any_fail = 1;
			for (i = 0; i < ncase_for[ci]; i++)
				if (grid[ci][i].note)
					any_unrun = 1;
		}
		if (any_unrun)
			return 4;
		if (any_fail)
			return 3;
	}
	return 0;
}
