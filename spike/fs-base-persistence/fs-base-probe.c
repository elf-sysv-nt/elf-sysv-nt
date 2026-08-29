/*
 * fs-base-probe -- does a user-written FS base survive the Windows kernel?
 *
 * The System V psABI reaches TLS through %fs. On x86-64 Windows the TEB is in
 * %gs and %fs is unused, so the register is free; whether the base written
 * into it is preserved is a different question and this asks it. Each case
 * writes a sentinel, provokes one event, then reads the base back through
 * RDFSBASE and a magic word back through %fs:0. Reading back is not enough on
 * its own: a base the kernel remembers but does not use for address
 * translation would pass the first check and corrupt under the second.
 *
 * Built and driven by measure-fs-base.sh. See README.md for the method.
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

#define PROBE_VERSION "fs-base-probe 1.0"

#ifndef PF_RDWRFSGSBASE_AVAILABLE
#define PF_RDWRFSGSBASE_AVAILABLE 22
#endif

/* Byte encodings rather than mnemonics. The 2019 root's assembler knows
 * these, but only with -mfsgsbase, and a spike that stops compiling on the
 * next toolchain it meets has failed at its one job. */
static inline uint64_t rdfsbase(void)
{
	uint64_t v;
	__asm__ __volatile__(".byte 0xf3,0x48,0x0f,0xae,0xc0" : "=a"(v));
	return v;
}

static inline void wrfsbase(uint64_t v)
{
	__asm__ __volatile__(".byte 0xf3,0x48,0x0f,0xae,0xd0" : : "a"(v));
}

static inline uint64_t fs_word0(void)
{
	uint64_t v;
	__asm__ __volatile__("movq %%fs:0, %0" : "=r"(v));
	return v;
}

#define MAX_CASES 16

struct outcome {
	const char *name;
	const char *note;		/* set when the case could not run at all */
	unsigned long long checks;
	unsigned long long failures;
	uint64_t want;
	uint64_t got;			/* first base that came back wrong */
	uint64_t got_word;		/* and the word %fs:0 gave at the time */
	unsigned long long first_fail_at;	/* which check it was */
	int addressed;			/* %fs:0 was exercised at least once */
	int ran;
};

static struct outcome cases[MAX_CASES];
static int ncases;
static int debug;

static void trace(const char *what)
{
	if (debug)
		fprintf(stderr, "fs-base-probe: %s\n", what);
}

static struct outcome *case_open(const char *name)
{
	struct outcome *c = &cases[ncases++];
	c->name = name;
	c->ran = 1;
	trace(name);
	return c;
}

static void fail(struct outcome *c, uint64_t want, uint64_t got, uint64_t word)
{
	if (!c->failures) {
		c->want = want;
		c->got = got;
		c->got_word = word;
		c->first_fail_at = c->checks;
	}
	c->failures++;
}

/*
 * The persistence check, and it reads the base and nothing else.
 *
 * Reading the TCB self-pointer through %fs:0 in the same breath was the first
 * shape of this and it does not survive contact: between the RDFSBASE and the
 * dereference the base can go, and then the dereference is a read of address
 * zero on a thread with no handler. That crash is a result rather than a bug,
 * but it is not a result this function can report, so addressing is settled
 * once in the round-trip case under a guard and every other case asks the
 * cheaper question.
 *
 * Each case keeps its first failure and goes on counting. The count is what
 * makes the load case mean anything; the first value is what says whether the
 * base was cleared or crossed with a neighbouring thread's.
 */
static void check(struct outcome *c, uint64_t want)
{
	uint64_t got = rdfsbase();

	c->checks++;
	if (got != want)
		fail(c, want, got, 0);
}

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

/* Returns the base to write: a mapped page carrying its own address at offset
 * zero. Nothing frees these. The process is short-lived and a base pointing
 * at unmapped memory is a defect the probe would then have to distinguish
 * from the one it is looking for. */
static uint64_t sentinel_page(void)
{
	void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (p == MAP_FAILED) {
		fprintf(stderr, "fs-base-probe: mmap: %s\n", strerror(errno));
		exit(1);
	}
	*(uint64_t *)p = (uint64_t)p;
	return (uint64_t)p;
}

struct capability {
	int cpuid_bit;
	int win_feature;
	int executes;			/* the instruction ran without #UD */
	int signo;			/* what it raised when it did not */
};

static struct capability cap;

static void probe_capability(void)
{
	unsigned eax, ebx, ecx, edx;

	if (__get_cpuid_max(0, NULL) >= 7) {
		__cpuid_count(7, 0, eax, ebx, ecx, edx);
		cap.cpuid_bit = (ebx & 1u) != 0;
	}
	cap.win_feature = IsProcessorFeaturePresent(PF_RDWRFSGSBASE_AVAILABLE) != 0;

	/* Both of the above report what the processor and the loader believe.
	 * CR4.FSGSBASE is the kernel's to set and only the instruction knows. */
	install(SIGILL, catch_fault);
	fault_signo = 0;
	if (sigsetjmp(fault_return, 1) == 0) {
		(void)rdfsbase();
		cap.executes = 1;
	}
	cap.signo = fault_signo;
	install(SIGILL, SIG_DFL);
}

/*
 * Context switches performed by this process, summed over its threads.
 *
 * Read by byte offset into SystemProcessInformation rather than through a
 * declared structure. The layout is stable and documented but it is not ours,
 * and an offset that is wrong is easier to see written down than a padding
 * rule that is wrong. The walk validates itself by finding our own pid; if it
 * does not, the caller is told the count is unavailable rather than handed a
 * number nobody should trust.
 */
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

/* Write, read back, and address through the base, with the write repeated
 * every round so that the window under test is as short as it can be made.
 * A fault on the dereference counts as a failure like any other; it means the
 * base went between the two instructions. */
static void case_round_trip(unsigned long rounds)
{
	struct outcome *c = case_open("round trip");
	uint64_t base = sentinel_page();
	volatile uint64_t got, word;
	volatile unsigned long i;
	volatile int faulted;

	install(SIGSEGV, catch_fault);
	for (i = 0; i < rounds; i++) {
		word = 0;
		faulted = 0;
		wrfsbase(base);
		got = rdfsbase();
		if (sigsetjmp(fault_return, 1) == 0)
			word = fs_word0();
		else
			faulted = 1;
		c->checks++;
		if (got == base && !faulted && word == base) {
			c->addressed = 1;
			continue;
		}
		fail(c, base, got, word);
	}
	install(SIGSEGV, SIG_DFL);
}

static void case_yields(unsigned long rounds)
{
	struct outcome *c = case_open("yields");
	uint64_t base = sentinel_page();
	unsigned long i;

	wrfsbase(base);
	for (i = 0; i < rounds; i++) {
		if (i % 512 == 3)
			Sleep(1);	/* the only one that reliably deschedules */
		else if (i & 1)
			SwitchToThread();
		else
			Sleep(0);
		check(c, base);
	}
}

/*
 * A system call that enters the kernel and comes straight back out without
 * blocking. This separates the two ways the base could be lost, and the
 * difference decides whether a runtime could paper over the answer: a base
 * cleared on the way back from a call can be re-established at the call site,
 * and a base cleared by the scheduler cannot, because there is no call site.
 */
static void case_syscall(unsigned long rounds)
{
	struct outcome *c = case_open("syscall");
	uint64_t base = sentinel_page();
	FILETIME a, b, d, e;
	unsigned long i;

	wrfsbase(base);
	for (i = 0; i < rounds; i++) {
		GetProcessTimes(GetCurrentProcess(), &a, &b, &d, &e);
		check(c, base);
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

static void case_blocking_wait(unsigned long rounds)
{
	struct outcome *c = case_open("blocking wait");
	uint64_t base = sentinel_page();
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
	wrfsbase(base);
	for (i = 0; i < rounds; i++) {
		SetEvent(p.call);
		WaitForSingleObject(p.back, INFINITE);
		check(c, base);
	}
	WaitForSingleObject(peer, 5000);
	CloseHandle(peer);
	CloseHandle(p.call);
	CloseHandle(p.back);
}

/* Migration is the case the question was really about. A reschedule onto the
 * same processor could plausibly leave the MSR alone by accident; landing on
 * a different one cannot. */
static void case_migration(unsigned long rounds, int ncpu)
{
	struct outcome *c = case_open("migration");
	uint64_t base = sentinel_page();
	DWORD_PTR original;
	unsigned long i;

	original = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1);
	if (!original) {
		c->note = "affinity is not ours to set";
		return;
	}
	wrfsbase(base);
	for (i = 0; i < rounds; i++) {
		DWORD_PTR mask = (DWORD_PTR)1 << (i % (unsigned)ncpu);

		SetThreadAffinityMask(GetCurrentThread(), mask);
		SwitchToThread();
		check(c, base);
	}
	SetThreadAffinityMask(GetCurrentThread(), original);
}

static struct outcome *apc_case;
static uint64_t apc_base;

static VOID CALLBACK apc_body(ULONG_PTR arg)
{
	(void)arg;
	check(apc_case, apc_base);
}

static void case_apc(unsigned long rounds)
{
	struct outcome *c = case_open("apc");
	unsigned long i;

	apc_case = c;
	apc_base = sentinel_page();
	wrfsbase(apc_base);
	for (i = 0; i < rounds; i++) {
		if (!QueueUserAPC(apc_body, GetCurrentThread(), 0)) {
			c->note = "QueueUserAPC refused";
			return;
		}
		SleepEx(0, TRUE);
		check(c, apc_base);
	}
}

struct spinner {
	struct outcome *c;
	uint64_t base;
	volatile LONG ready;
	volatile LONG stop;
};

static DWORD WINAPI spinner_body(LPVOID arg)
{
	struct spinner *s = arg;

	s->base = sentinel_page();
	wrfsbase(s->base);
	InterlockedExchange((LONG *)&s->ready, 1);
	while (!s->stop) {
		check(s->c, s->base);
		SwitchToThread();
	}
	return 0;
}

/*
 * Suspend a running thread, read its context back, write the same context
 * returned, resume it. This is not an exotic case: it is exactly how Cygwin
 * delivers a signal, and CONTEXT_SEGMENTS carries SegFs, so a kernel that
 * reloads the base from the descriptor on the way out would clear it here and
 * nowhere else.
 */
static void case_hijack(unsigned long rounds)
{
	struct outcome *c = case_open("hijack");
	struct spinner s;
	CONTEXT ctx __attribute__((aligned(16)));
	HANDLE t;
	unsigned long i;

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
static uint64_t sig_base;

static void sig_check(int signo)
{
	(void)signo;
	check(sig_case, sig_base);
}

static void case_signal_sync(unsigned long rounds)
{
	struct outcome *c = case_open("signal, sync");
	unsigned long i;

	sig_case = c;
	sig_base = sentinel_page();
	install(SIGUSR1, sig_check);
	wrfsbase(sig_base);
	for (i = 0; i < rounds; i++) {
		raise(SIGUSR1);
		check(c, sig_base);
	}
	install(SIGUSR1, SIG_DFL);
}

static void fault_check(int signo)
{
	fault_signo = signo;
	check(sig_case, sig_base);
	siglongjmp(fault_return, 1);
}

/* A real hardware fault rather than a raise(). Cygwin turns the exception into
 * a signal through its vectored handler, which is a longer path through the
 * kernel than anything above and the one a TLS access itself would take when
 * it went wrong. */
static void case_signal_fault(unsigned long rounds)
{
	struct outcome *c = case_open("signal, fault");
	unsigned long i;

	sig_case = c;
	sig_base = sentinel_page();
	install(SIGSEGV, fault_check);
	wrfsbase(sig_base);
	for (i = 0; i < rounds; i++) {
		if (sigsetjmp(fault_return, 1) == 0)
			*(volatile int *)0 = 1;
		check(c, sig_base);
	}
	install(SIGSEGV, SIG_DFL);
}

static struct {
	struct outcome *c;
	uint64_t base;
	volatile int ready;
	volatile int stop;
} async;

static void async_handler(int signo)
{
	(void)signo;
	check(async.c, async.base);
}

static void *async_body(void *arg)
{
	(void)arg;
	async.base = sentinel_page();
	wrfsbase(async.base);
	async.ready = 1;
	while (!async.stop)
		check(async.c, async.base);
	return NULL;
}

static void case_signal_async(unsigned long rounds)
{
	struct outcome *c = case_open("signal, async");
	pthread_t tid;
	unsigned long i;

	async.c = c;
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
static double preempt_rate;		/* checks per second, for the ms below */

static void *burner_body(void *arg)
{
	volatile unsigned long long *sink = arg;

	while (!preempt_stop)
		(*sink)++;
	return NULL;
}

/*
 * The one case that makes no system call at all. Its checker thread spins on
 * RDFSBASE while a burner sits on every processor, so the only thing that can
 * happen to it is being taken off a processor and put back. If the base
 * survives here and dies elsewhere, what clears it is a kernel transition and
 * a runtime could in principle re-establish it. If it dies here, nothing can,
 * because preemption has no return address to hook.
 *
 * time() is a call, so it is consulted once per two hundred thousand checks
 * and the first failure's index says which side of that line it fell on.
 */
static void case_preemption(int seconds, int ncpu)
{
	struct outcome *c = case_open("preemption");
	pthread_t *burner = calloc((size_t)ncpu, sizeof *burner);
	unsigned long long sink = 0;
	uint64_t base = sentinel_page();
	LARGE_INTEGER hz, t0, t1;
	time_t end;
	int started = 0, i;

	if (!burner) {
		c->note = "out of memory";
		return;
	}
	for (i = 0; i < ncpu; i++)
		if (pthread_create(&burner[i], NULL, burner_body, &sink) == 0)
			started++;

	QueryPerformanceFrequency(&hz);
	QueryPerformanceCounter(&t0);
	wrfsbase(base);
	end = time(NULL) + seconds;
	for (;;) {
		for (i = 0; i < 200000; i++)
			check(c, base);
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

static void *load_body(void *arg)
{
	struct outcome *o = arg;
	uint64_t base = sentinel_page();

	wrfsbase(base);
	while (!load_stop) {
		int i;

		for (i = 0; i < 64; i++)
			check(o, base);
		SwitchToThread();
	}
	return NULL;
}

/* Every worker holds a base no other worker holds, so a failure here says
 * which of the two bad outcomes happened: a base cleared to zero, or a base
 * carrying a neighbour's pointer. Those want different repairs. */
static void case_load(int nthreads, int seconds, unsigned long long *switches)
{
	struct outcome *c = case_open("load");
	struct outcome *own = calloc((size_t)nthreads, sizeof *own);
	pthread_t *tid = calloc((size_t)nthreads, sizeof *tid);
	unsigned long long before = 0, after = 0;
	int started = 0, i;

	if (!own || !tid) {
		c->note = "out of memory";
		return;
	}
	if (context_switches(&before) != 0)
		before = 0;
	for (i = 0; i < nthreads; i++) {
		if (pthread_create(&tid[i], NULL, load_body, &own[i]) == 0)
			started++;
		else
			break;
	}
	if (!started) {
		c->note = "no worker thread would start";
		return;
	}
	Sleep((DWORD)seconds * 1000);
	/* Before the stop, not after it. A thread that has exited is gone from
	 * the per-thread list and takes its count with it, which is how this
	 * first reported eleven switches for eight threads over three
	 * seconds. */
	if (context_switches(&after) == 0 && before && after > before)
		*switches = after - before;
	load_stop = 1;
	for (i = 0; i < started; i++)
		pthread_join(tid[i], NULL);

	for (i = 0; i < started; i++) {
		c->checks += own[i].checks;
		c->addressed |= own[i].addressed;
		if (own[i].failures && !c->failures) {
			c->want = own[i].want;
			c->got = own[i].got;
			c->got_word = own[i].got_word;
			c->first_fail_at = own[i].first_fail_at;
		}
		c->failures += own[i].failures;
	}
	free(own);
	free(tid);
}

static const char *note_name[6];
static char note_text[6][160];
static int nnotes;

static void note_add(const char *name, const char *fmt, ...)
{
	va_list ap;

	if (nnotes == 6)
		return;
	note_name[nnotes] = name;
	va_start(ap, fmt);
	vsnprintf(note_text[nnotes], sizeof note_text[0], fmt, ap);
	va_end(ap);
	nnotes++;
}

static volatile uint64_t fresh_base;

static DWORD WINAPI fresh_body(LPVOID arg)
{
	(void)arg;
	fresh_base = rdfsbase();
	return 0;
}

/* Not a pass or a fail. Whichever way this comes out the runtime sets the
 * base at thread start anyway; what would hurt is if it were not the same
 * answer every time. */
static void observe_thread_start(void)
{
	uint64_t base = sentinel_page();
	HANDLE t;

	wrfsbase(base);
	fresh_base = ~(uint64_t)0;
	t = CreateThread(NULL, 0, fresh_body, NULL, 0, NULL);
	if (!t) {
		note_add("thread start", "could not create the thread");
		return;
	}
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
	if (fresh_base == base)
		note_add("thread start", "inherits the creating thread's base");
	else if (fresh_base == 0)
		note_add("thread start", "starts at zero");
	else
		note_add("thread start", "starts at 0x%llx",
			 (unsigned long long)fresh_base);
}

static void observe_fork(void)
{
	uint64_t base = sentinel_page();
	uint64_t seen[2] = { 0, 0 };
	int fd[2];
	pid_t pid;

	if (pipe(fd) != 0) {
		note_add("fork", "pipe: %s", strerror(errno));
		return;
	}
	wrfsbase(base);
	pid = fork();
	if (pid == 0) {
		uint64_t mine[2];

		mine[0] = rdfsbase();
		mine[1] = 0;
		if (write(fd[1], mine, sizeof mine) != (ssize_t)sizeof mine)
			_exit(2);
		_exit(0);
	}
	close(fd[1]);
	if (pid < 0) {
		note_add("fork", "fork: %s", strerror(errno));
		close(fd[0]);
		return;
	}
	if (read(fd[0], seen, sizeof seen) != (ssize_t)sizeof seen)
		note_add("fork", "the child said nothing");
	else if (seen[0] == base)
		note_add("fork", "the child keeps the base it was forked with");
	else
		note_add("fork", "the child's base is 0x%llx",
			 (unsigned long long)seen[0]);
	close(fd[0]);
	waitpid(pid, NULL, 0);
}

static const char *yesno(int b)
{
	return b ? "yes" : "no";
}

static void report(FILE *out, int terse, int nthreads, int seconds,
		   unsigned long long switches)
{
	unsigned long long checks = 0, failures = 0;
	int failed = 0, incomplete = 0, i;
	const char *verdict;

	for (i = 0; i < ncases; i++) {
		checks += cases[i].checks;
		failures += cases[i].failures;
		if (cases[i].note)
			incomplete++;
		else if (cases[i].failures)
			failed++;
	}
	if (!cap.executes)
		verdict = "no";
	else if (incomplete)
		verdict = "incomplete";
	else
		verdict = failed ? "no" : "yes";

	if (!terse) {
		fprintf(out, "== capability\n\n");
		fprintf(out, "    cpuid leaf 7 FSGSBASE      %s\n", yesno(cap.cpuid_bit));
		fprintf(out, "    IsProcessorFeaturePresent  %s\n", yesno(cap.win_feature));
		fprintf(out, "    the instruction executes   %s%s\n", yesno(cap.executes),
			cap.executes ? "" : " (raised a signal)");
		fprintf(out, "    %%fs:0 addresses through it %s\n",
			ncases ? yesno(cases[0].addressed) : "-");
		fprintf(out, "\n    The first two report what the processor and the loader\n"
			     "    believe. CR4.FSGSBASE is the kernel's to set, so the third\n"
			     "    one is the evidence and the fourth says the base is used\n"
			     "    for address translation rather than merely remembered.\n\n");

		fprintf(out, "== cases\n\n");
		fprintf(out, "    %-16s %14s %14s\n", "", "checks", "failures");
		for (i = 0; i < ncases; i++) {
			struct outcome *c = &cases[i];

			if (c->note) {
				fprintf(out, "    %-16s %14s  %s\n", c->name, "-", c->note);
				continue;
			}
			fprintf(out, "    %-16s %14llu %14llu\n", c->name,
				c->checks, c->failures);
		}
		fprintf(out, "\n    A check writes a base once, provokes the case's event, and\n"
			     "    reads the base back. Only the round trip dereferences %%fs:0,\n"
			     "    under a guard, because a base that goes between the read and\n"
			     "    the dereference takes the thread with it.\n\n");

		if (failed) {
			fprintf(out, "== what came back instead\n\n");
			for (i = 0; i < ncases; i++) {
				struct outcome *c = &cases[i];

				if (c->note || !c->failures)
					continue;
				fprintf(out, "    %-16s wanted 0x%llx  got 0x%llx  at check %llu\n",
					c->name, (unsigned long long)c->want,
					(unsigned long long)c->got,
					c->first_fail_at);
			}
			fputc('\n', out);
		}

		fprintf(out, "== observations\n\n");
		for (i = 0; i < nnotes; i++)
			fprintf(out, "    %-16s %s\n", note_name[i], note_text[i]);
		fprintf(out, "\n    Neither of these is a pass or a fail. The loader sets the\n"
			     "    base at thread start and can set it again after a fork; what\n"
			     "    matters is that the answer is the same every time.\n\n");

		fprintf(out, "== summary\n\n");
	}
	fprintf(out, "%sverdict=%s\n", terse ? "" : "    ", verdict);
	fprintf(out, "%scases=%d\n", terse ? "" : "    ", ncases);
	fprintf(out, "%scases_failed=%d\n", terse ? "" : "    ", failed);
	fprintf(out, "%scases_incomplete=%d\n", terse ? "" : "    ", incomplete);
	/* The counts here are a property of the machine and the minute. This
	 * line is the part a rerun can be diffed against, so it is printed in
	 * a shape a diff can read. */
	fprintf(out, "%sshape=", terse ? "" : "    ");
	for (i = 0; i < ncases; i++)
		fprintf(out, "%s%s:%s", i ? "," : "", cases[i].name,
			cases[i].note ? "unrun" : cases[i].failures ? "fail" : "pass");
	fputc('\n', out);
	fprintf(out, "%schecks=%llu\n", terse ? "" : "    ", checks);
	fprintf(out, "%sfailures=%llu\n", terse ? "" : "    ", failures);
	if (switches)
		fprintf(out, "%sswitches_under_load=%llu\n", terse ? "" : "    ", switches);
	else
		fprintf(out, "%sswitches_under_load=unavailable\n", terse ? "" : "    ");
	/* The one number worth carrying out of here. A base that dies only on a
	 * kernel transition could be re-established on the way back; one that
	 * dies to the scheduler cannot, and this says how long it lasts when
	 * nothing but the scheduler is touching the thread. */
	for (i = 0; i < ncases; i++) {
		struct outcome *c = &cases[i];

		if (strcmp(c->name, "preemption") || c->note || preempt_rate <= 0)
			continue;
		fprintf(out, "%spreemption_checks_per_second=%.0f\n",
			terse ? "" : "    ", preempt_rate);
		if (c->failures)
			fprintf(out, "%spreemption_survived_ms=%.1f\n",
				terse ? "" : "    ",
				1000.0 * (double)c->first_fail_at / preempt_rate);
	}
	fprintf(out, "%sload_threads=%d\n", terse ? "" : "    ", nthreads);
	fprintf(out, "%sload_seconds=%d\n", terse ? "" : "    ", seconds);
	fprintf(out, "%sprobe=%s\n", terse ? "" : "    ", PROBE_VERSION);
}

static void usage(FILE *out)
{
	fputs("Usage:\n"
	      "  fs-base-probe [options]\n"
	      "\n"
	      "Options:\n"
	      "  -r N, --rounds=N   Rounds for the cheap cases. [default: 20000]\n"
	      "  -j N, --threads=N  Threads in the load case. [default: two per cpu]\n"
	      "  -s N, --seconds=N  Length of the load case. [default: 10]\n"
	      "  -t, --terse        The summary block alone, one key=value per line.\n"
	      "  -d, --debug        Name each case on stderr as it starts.\n"
	      "  -V, --version      Print the version and exit.\n"
	      "  -h, --help         Print this message and exit.\n", out);
}

/* Expensive cases get a fraction of the round count. A fault costs Cygwin a
 * vectored handler and a longjmp, and twenty thousand of those is a minute of
 * nothing new. */
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
		fprintf(stderr, "fs-base-probe: %s wants a positive number, not %s\n",
			what, s);
		exit(2);
	}
	return v;
}

int main(int argc, char **argv)
{
	unsigned long long switches = 0;
	unsigned long rounds = 20000;
	int seconds = 10, nthreads = 0, terse = 0;
	int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
	int i;

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
		} else {
			fprintf(stderr, "fs-base-probe: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
	}
	if (!nthreads)
		nthreads = ncpu * 2;

	setvbuf(stdout, NULL, _IOLBF, 0);
	probe_capability();
	if (!cap.executes) {
		report(stdout, terse, nthreads, seconds, 0);
		return 3;
	}

	case_round_trip(share(rounds, 10, 100));
	case_syscall(rounds);
	case_yields(rounds);
	case_blocking_wait(rounds);
	case_migration(share(rounds, 10, 100), ncpu);
	case_apc(share(rounds, 10, 100));
	case_hijack(share(rounds, 5, 200));
	case_signal_sync(share(rounds, 10, 100));
	case_signal_fault(share(rounds, 100, 50));
	case_signal_async(share(rounds, 50, 50));
	case_preemption(seconds, ncpu);
	trace("thread start");
	observe_thread_start();
	trace("fork");
	observe_fork();
	case_load(nthreads, seconds, &switches);

	report(stdout, terse, nthreads, seconds, switches);

	for (i = 0; i < ncases; i++)
		if (cases[i].note)
			return 4;
	for (i = 0; i < ncases; i++)
		if (cases[i].failures)
			return 3;
	return 0;
}
