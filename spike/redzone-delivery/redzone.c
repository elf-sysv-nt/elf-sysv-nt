/*
 * redzone -- can a delivery path reserve the red zone before it builds the
 * handler's frame?
 *
 * Spike 3 found the red zone dying at %rsp-8, and found our own layer killing
 * it: Cygwin's signal delivery hijacks the thread and lays the handler frame
 * at the interrupted stack pointer, taking %rsp-8 first, on every delivery.
 * This asks the sequel. If the delivery path reserves the 128 bytes the psABI
 * holds sacred before it builds that frame, does a sysv_abi leaf keep its red
 * zone across the delivery -- and does the handler still run, and the
 * interrupted computation still finish correctly on the far side.
 *
 * It models delivery with the same self-driven hijack spike 3 used: suspend
 * the watcher, read its context, choose the stack pointer the handler frame is
 * built on, redirect %rip to a stub, resume. The naive construction builds at
 * the interrupted %rsp and has to clobber; the reserved construction builds
 * 128 below it and must not. The driver plays the kernel on both sides -- it
 * saves the context on delivery and restores it whole when the handler is
 * done, which is this model's sigreturn. It is a model of delivery, not
 * Cygwin's delivery; WP-43 re-measures the real sigdelayed against what it
 * finds. See README.md for the mechanism and the cases.
 *
 * Built and driven by redzone-delivery.sh.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PROBE_VERSION "redzone 1.0"

#define MAX_CASES 16

/*
 * The watcher's block. Every offset is written out again in redzone.S, which
 * cannot include this file, so the static assertions below hold the two copies
 * to each other rather than anyone remembering.
 */
struct rz {
	volatile uint64_t stop;
	uint64_t rounds;	/* zero means until stop */
	uint64_t depth;		/* bytes below %rsp to paint, multiple of 8 */
	uint64_t checks;	/* scan or fold rounds completed */
	uint64_t failures;	/* words found changed */
	uint64_t first_bad;	/* offset below %rsp of the first word lost */
	uint64_t shallowest;
	uint64_t deepest;
	uint64_t got;
	uint64_t rsp;		/* the %rsp under watch */
	volatile uint64_t running;
	uint64_t acc;		/* redzone_accum: the value it finished holding */
};

_Static_assert(offsetof(struct rz, stop) == 0, "rz.stop");
_Static_assert(offsetof(struct rz, rounds) == 8, "rz.rounds");
_Static_assert(offsetof(struct rz, depth) == 16, "rz.depth");
_Static_assert(offsetof(struct rz, checks) == 24, "rz.checks");
_Static_assert(offsetof(struct rz, failures) == 32, "rz.failures");
_Static_assert(offsetof(struct rz, first_bad) == 40, "rz.first_bad");
_Static_assert(offsetof(struct rz, shallowest) == 48, "rz.shallowest");
_Static_assert(offsetof(struct rz, deepest) == 56, "rz.deepest");
_Static_assert(offsetof(struct rz, got) == 64, "rz.got");
_Static_assert(offsetof(struct rz, rsp) == 72, "rz.rsp");
_Static_assert(offsetof(struct rz, running) == 80, "rz.running");
_Static_assert(offsetof(struct rz, acc) == 88, "rz.acc");

extern void redzone_watch(struct rz *w);
extern void redzone_accum(struct rz *w);
extern void deliver_stub(void);
extern char redzone_accum_win_lo[];	/* the accum's store-to-read window, */
extern char redzone_accum_win_hi[];	/* where the red zone is its sole carrier */
extern volatile uint64_t handler_calls;
extern volatile uint64_t handler_done;

/* Each round redzone_accum commits value = round*K + C to its red-zone word,
 * reads it back after a window, and folds it into a running XOR checksum. This
 * mirror lets the driver say what checksum an uncorrupted run must reach. */
#define ACC_MIXK 0x2545f4914f6cdd1dull
#define ACC_MIXC 0x123456789abcdef1ull

static uint64_t accum_expected(uint64_t rounds)
{
	uint64_t cs = 0, i;

	for (i = 0; i < rounds; i++)
		cs ^= i * ACC_MIXK + ACC_MIXC;
	return cs;
}

/*
 * The handler the stub calls. It bumps a counter, so a delivery that ran can
 * be told from one that silently did nothing, and it writes a block of stack
 * so a naive delivery clobbers a realistic span rather than one word. It is
 * ms_abi by default on this target, which is what the stub's shadow-space call
 * expects.
 */
__attribute__((noinline))
void handler_c(void)
{
	volatile unsigned char buf[512];
	unsigned i;

	handler_calls++;
	for (i = 0; i < sizeof buf; i++)
		buf[i] = (unsigned char)(i ^ 0x5a);
	if (buf[(unsigned)(handler_calls & 511)] == 0xffu)
		handler_done += 0;	/* unreachable; anchors buf */
}

/* ---- outcomes ---------------------------------------------------------- */

struct outcome {
	const char *name;
	const char *kind;	/* control or measure */
	const char *note;	/* set when the case could not run at all */
	char detail[192];
	uint64_t checks;
	uint64_t failures;	/* judged failures; zero is a pass */
	int ran;
	uint64_t deliveries;
	uint64_t handled;	/* handler_c calls attributable to this case */
	uint64_t rz_shallow;	/* nearest offset below %rsp that moved */
	uint64_t rz_deep;
	uint64_t rz_words;
};

static struct outcome cases[MAX_CASES];
static int ncases;
static int debug;
static uint64_t depth = 1024;

static void trace(const char *what)
{
	if (debug)
		fprintf(stderr, "redzone: %s\n", what);
}

static struct outcome *case_open(const char *name, const char *kind)
{
	struct outcome *c = &cases[ncases++];

	c->name = name;
	c->kind = kind;
	c->ran = 1;
	trace(name);
	return c;
}

static void detail(struct outcome *c, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(c->detail, sizeof c->detail, fmt, ap);
	va_end(ap);
}

/* One check, and the message kept when it is the first to fail. */
static void want(struct outcome *c, int ok, const char *fmt, ...)
{
	va_list ap;

	c->checks++;
	if (ok)
		return;
	if (!c->failures) {
		va_start(ap, fmt);
		vsnprintf(c->detail, sizeof c->detail, fmt, ap);
		va_end(ap);
	}
	c->failures++;
}

/* ---- the delivery engine ---------------------------------------------- */

static struct rz shared_w __attribute__((aligned(64)));

static DWORD WINAPI watch_proc(LPVOID p)
{
	(void)p;
	redzone_watch(&shared_w);
	return 0;
}

static DWORD WINAPI accum_proc(LPVOID p)
{
	(void)p;
	redzone_accum(&shared_w);
	return 0;
}

/*
 * Catch the watcher inside its loop, where %rsp equals the %rsp it recorded,
 * so the offsets the driver computes are measured from the watched stack
 * pointer and not from some transient point in the prologue. Neither watcher
 * moves %rsp once running is set, so that part is exact rather than
 * probabilistic. When rip_lo is given the catch is narrowed further, to a
 * chosen instruction range -- used to land an integrity delivery in the
 * window where the red zone is the value's only carrier.
 */
static int catch_in_loop(HANDLE t, CONTEXT *ctx, uint64_t rip_lo, uint64_t rip_hi)
{
	int attempts;

	for (attempts = 0; attempts < 2000000; attempts++) {
		if (SuspendThread(t) == (DWORD)-1)
			return -1;
		memset(ctx, 0, sizeof *ctx);
		ctx->ContextFlags = CONTEXT_FULL;
		if (GetThreadContext(t, ctx) && shared_w.running &&
		    ctx->Rsp == shared_w.rsp &&
		    (!rip_lo || (ctx->Rip >= rip_lo && ctx->Rip < rip_hi)))
			return 0;
		ResumeThread(t);
		SwitchToThread();
	}
	return -1;
}

/* Bounded wait for the handler to advance the done counter past a mark. */
static int wait_done(uint64_t mark)
{
	uint64_t spins = 0;

	while (handler_done == mark) {
		if (++spins > 200000000ull)
			return -1;
		SwitchToThread();
	}
	return 0;
}

/*
 * One delivery: catch the watcher, build the handler frame at the chosen stack
 * pointer, let the handler run, then restore the interrupted context whole.
 * reserve is the bytes held below the interrupted %rsp before the frame; when
 * alt_top is non-zero the frame is built there instead, on an alternate stack.
 */
static int deliver(HANDLE t, uint64_t reserve, uint64_t alt_top,
		   uint64_t rip_lo, uint64_t rip_hi)
{
	CONTEXT save, ctx;
	uint64_t before = handler_done;

	if (catch_in_loop(t, &save, rip_lo, rip_hi))
		return -1;
	ctx = save;
	ctx.Rsp = alt_top ? alt_top : save.Rsp - reserve;
	ctx.Rip = (DWORD64)(uintptr_t)deliver_stub;
	if (!SetThreadContext(t, &ctx)) {
		ResumeThread(t);
		return -1;
	}
	ResumeThread(t);

	if (wait_done(before))
		return -1;

	if (SuspendThread(t) == (DWORD)-1)
		return -1;
	save.ContextFlags = CONTEXT_FULL;
	if (!SetThreadContext(t, &save)) {
		ResumeThread(t);
		return -1;
	}
	ResumeThread(t);
	return 0;
}

/*
 * A second delivery while the handler runs. The first is reserved into the
 * watcher; while its handler spins, a second is reserved 128 below the
 * handler's own %rsp, and each is lifted out in turn -- inner then outer --
 * the way nested signals unwind. The outer frame's red zone, high above both,
 * must be untouched.
 */
static int deliver_nested(HANDLE t)
{
	CONTEXT save1, save2, ctx;
	uint64_t d0 = handler_done, d1;

	if (catch_in_loop(t, &save1, 0, 0))
		return -1;
	ctx = save1;
	ctx.Rsp = save1.Rsp - 128;
	ctx.Rip = (DWORD64)(uintptr_t)deliver_stub;
	if (!SetThreadContext(t, &ctx)) {
		ResumeThread(t);
		return -1;
	}
	ResumeThread(t);
	if (wait_done(d0))
		return -1;

	d1 = handler_done;
	if (SuspendThread(t) == (DWORD)-1)
		return -1;
	memset(&save2, 0, sizeof save2);
	save2.ContextFlags = CONTEXT_FULL;
	if (!GetThreadContext(t, &save2)) {
		ResumeThread(t);
		return -1;
	}
	ctx = save2;
	ctx.Rsp = save2.Rsp - 128;
	ctx.Rip = (DWORD64)(uintptr_t)deliver_stub;
	if (!SetThreadContext(t, &ctx)) {
		ResumeThread(t);
		return -1;
	}
	ResumeThread(t);
	if (wait_done(d1))
		return -1;

	/* sigreturn the inner frame, then the outer */
	if (SuspendThread(t) == (DWORD)-1)
		return -1;
	save2.ContextFlags = CONTEXT_FULL;
	if (!SetThreadContext(t, &save2)) {
		ResumeThread(t);
		return -1;
	}
	ResumeThread(t);
	if (SuspendThread(t) == (DWORD)-1)
		return -1;
	save1.ContextFlags = CONTEXT_FULL;
	if (!SetThreadContext(t, &save1)) {
		ResumeThread(t);
		return -1;
	}
	ResumeThread(t);
	return 0;
}

static HANDLE start_watcher(LPTHREAD_START_ROUTINE proc, uint64_t rounds)
{
	HANDLE t;

	memset(&shared_w, 0, sizeof shared_w);
	shared_w.rounds = rounds;
	shared_w.depth = depth;
	t = CreateThread(NULL, 0, proc, NULL, 0, NULL);
	if (!t)
		return NULL;
	while (!shared_w.running)
		SwitchToThread();
	return t;
}

static void stop_watcher(HANDLE t)
{
	/* Let the watcher run undisturbed for a moment before stopping it. The
	 * hijack suspends it on every delivery, so it scans only in short bursts
	 * between them, and a sigreturn resumes it mid-pass; told to stop right
	 * then, it can exit having scanned only the tail of one pass and never
	 * revisited the last delivery's writes. A free-running settle guarantees
	 * a full pass over the whole region while those writes are still present,
	 * so the shallowest offset touched is measured rather than raced past. */
	Sleep(100);
	shared_w.stop = 1;
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
}

static void collect_rz(struct outcome *c)
{
	c->rz_words = shared_w.failures;
	c->rz_shallow = shared_w.shallowest;
	c->rz_deep = shared_w.deepest;
}

/* ---- the cases --------------------------------------------------------- */

/*
 * quiet -- the watcher spins undisturbed and sees nothing. A blind watcher
 * reports the same zero as an intact red zone, so this control is judged: any
 * movement with nothing provoking it is a failure, and it is the floor the
 * measurements stand on.
 */
static void case_quiet(unsigned long rounds)
{
	struct outcome *c = case_open("quiet", "control");
	struct rz w;

	memset(&w, 0, sizeof w);
	w.rounds = rounds;
	w.depth = depth;
	redzone_watch(&w);
	c->checks = w.checks;
	want(c, w.failures == 0,
	     "the region moved with nothing provoking it, %llu words, first at %llu",
	     (unsigned long long)w.failures, (unsigned long long)w.first_bad);
	c->rz_words = w.failures;
	c->rz_shallow = w.shallowest;
	c->rz_deep = w.deepest;
	if (!c->failures)
		detail(c, "%llu bytes below %%rsp intact over %llu passes",
		       (unsigned long long)w.depth, (unsigned long long)w.checks);
}

/*
 * deliver-naive -- a handler frame built at the interrupted %rsp. The control
 * that proves this model destroys the red zone exactly where Cygwin's real
 * path does: the nearest word lost must be at offset 8. A clean run here would
 * mean the model had gone quiet, and every reserved measurement below would be
 * worthless.
 */
static void case_naive(unsigned long events)
{
	struct outcome *c = case_open("deliver-naive", "control");
	uint64_t base = handler_calls;
	unsigned long i;
	HANDLE t;

	t = start_watcher(watch_proc, 0);
	if (!t) {
		c->note = "could not start the watcher";
		return;
	}
	for (i = 0; i < events; i++)
		if (deliver(t, 0, 0, 0, 0) == 0)
			c->deliveries++;
	stop_watcher(t);
	c->handled = handler_calls - base;
	collect_rz(c);

	want(c, c->deliveries > 0, "no delivery completed");
	want(c, c->rz_words > 0, "a frame at the interrupted %%rsp lost nothing");
	want(c, c->rz_shallow == 8,
	     "nearest word lost at offset %llu, not 8",
	     (unsigned long long)c->rz_shallow);
	if (!c->failures)
		detail(c, "clobbered from offset %llu to %llu over %llu deliveries",
		       (unsigned long long)c->rz_shallow,
		       (unsigned long long)c->rz_deep,
		       (unsigned long long)c->deliveries);
}

/*
 * deliver-reserved -- a handler frame built 128 below the interrupted %rsp.
 * The measurement the spike exists for: the red zone, offsets 8 through 128,
 * must be intact across every delivery, so the nearest word the reservation
 * lets the frame reach must lie past 128.
 */
static void case_reserved(unsigned long events)
{
	struct outcome *c = case_open("deliver-reserved", "measure");
	uint64_t base = handler_calls;
	unsigned long i;
	HANDLE t;

	t = start_watcher(watch_proc, 0);
	if (!t) {
		c->note = "could not start the watcher";
		return;
	}
	for (i = 0; i < events; i++)
		if (deliver(t, 128, 0, 0, 0) == 0)
			c->deliveries++;
	stop_watcher(t);
	c->handled = handler_calls - base;
	collect_rz(c);

	want(c, c->deliveries > 0, "no delivery completed");
	want(c, c->handled == c->deliveries,
	     "%llu deliveries but %llu handler runs",
	     (unsigned long long)c->deliveries, (unsigned long long)c->handled);
	want(c, c->rz_words > 0,
	     "the reserved delivery wrote nothing the watcher could see");
	want(c, c->rz_shallow > 128,
	     "a word inside the red zone moved, nearest at offset %llu",
	     (unsigned long long)c->rz_shallow);
	if (!c->failures)
		detail(c, "red zone intact, nearest write at offset %llu over %llu deliveries",
		       (unsigned long long)c->rz_shallow,
		       (unsigned long long)c->deliveries);
}

/*
 * resume-integrity -- after a reserved delivery the interrupted computation
 * finishes with the value its red-zone scratch held. The accumulate watcher
 * commits a round-derived value to the first red-zone word, drops every
 * register copy, waits, then reads it back and folds it into a checksum; a
 * reservation that preserves the bytes must also preserve the computation, so
 * the checksum it ends on has to be the one an undisturbed run would reach.
 *
 * Built with SPIKE_INTEGRITY_NAIVE the delivery is naive instead, which takes
 * %rsp-8 first and overwrites that word while it is the value's only carrier,
 * and the checksum must break -- the negative control that shows this case can
 * fail.
 */
static void case_integrity(unsigned long events)
{
	struct outcome *c = case_open("resume-integrity", "measure");
	uint64_t base = handler_calls, want_acc;
	unsigned long i;
	HANDLE t;
#ifdef SPIKE_INTEGRITY_NAIVE
	uint64_t reserve = 0;
#else
	uint64_t reserve = 128;
#endif

	t = start_watcher(accum_proc, 0);
	if (!t) {
		c->note = "could not start the watcher";
		return;
	}
	for (i = 0; i < events; i++)
		if (deliver(t, reserve, 0,
			    (uint64_t)(uintptr_t)redzone_accum_win_lo,
			    (uint64_t)(uintptr_t)redzone_accum_win_hi) == 0)
			c->deliveries++;
	stop_watcher(t);
	c->handled = handler_calls - base;
	c->checks = shared_w.checks;
	want_acc = accum_expected(shared_w.checks);

	want(c, c->deliveries > 0, "no delivery completed");
	want(c, shared_w.acc == want_acc,
	     "the leaf finished on 0x%llx, not the 0x%llx an undisturbed run reaches",
	     (unsigned long long)shared_w.acc, (unsigned long long)want_acc);
	if (!c->failures)
		detail(c, "value 0x%llx held across %llu deliveries and %llu folds",
		       (unsigned long long)shared_w.acc,
		       (unsigned long long)c->deliveries,
		       (unsigned long long)shared_w.checks);
}

/*
 * handler-ran -- the redirected %rip actually reached the handler and returned
 * on every reserved delivery, so deliver-reserved measured a delivery rather
 * than a delivery that silently did nothing.
 */
static void case_handler_ran(unsigned long events)
{
	struct outcome *c = case_open("handler-ran", "measure");
	uint64_t cbase = handler_calls, dbase = handler_done;
	unsigned long i;
	HANDLE t;

	t = start_watcher(watch_proc, 0);
	if (!t) {
		c->note = "could not start the watcher";
		return;
	}
	for (i = 0; i < events; i++)
		if (deliver(t, 128, 0, 0, 0) == 0)
			c->deliveries++;
	stop_watcher(t);
	c->handled = handler_calls - cbase;
	collect_rz(c);

	want(c, c->deliveries > 0, "no delivery completed");
	want(c, c->handled == c->deliveries,
	     "%llu deliveries reached the handler %llu times",
	     (unsigned long long)c->deliveries, (unsigned long long)c->handled);
	want(c, handler_done - dbase == c->deliveries,
	     "%llu handler runs but %llu returns recorded",
	     (unsigned long long)c->handled,
	     (unsigned long long)(handler_done - dbase));
	if (!c->failures)
		detail(c, "%llu deliveries, %llu reached the handler and returned",
		       (unsigned long long)c->deliveries,
		       (unsigned long long)c->handled);
}

/*
 * nested -- a second delivery while the handler runs reserves its own 128
 * below the handler's own %rsp, leaving the outer frame's red zone, high above
 * both, untouched. Two handler runs per episode, and the watched red zone must
 * stay past 128.
 */
static void case_nested(unsigned long events)
{
	struct outcome *c = case_open("nested", "measure");
	uint64_t base = handler_calls;
	unsigned long episodes = events / 4 ? events / 4 : 1;
	unsigned long i;
	HANDLE t;

	t = start_watcher(watch_proc, 0);
	if (!t) {
		c->note = "could not start the watcher";
		return;
	}
	for (i = 0; i < episodes; i++)
		if (deliver_nested(t) == 0)
			c->deliveries++;
	stop_watcher(t);
	c->handled = handler_calls - base;
	collect_rz(c);

	want(c, c->deliveries > 0, "no nested episode completed");
	want(c, c->handled == c->deliveries * 2,
	     "%llu episodes but %llu handler runs",
	     (unsigned long long)c->deliveries, (unsigned long long)c->handled);
	want(c, c->rz_shallow > 128,
	     "the outer red zone moved, nearest at offset %llu",
	     (unsigned long long)c->rz_shallow);
	if (!c->failures)
		detail(c, "%llu episodes, two handlers each, outer red zone intact past offset %llu",
		       (unsigned long long)c->deliveries,
		       (unsigned long long)c->rz_shallow);
}

/*
 * altstack -- delivery onto an alternate stack leaves the interrupted frame's
 * red zone untouched, which it must, since the frame is built elsewhere
 * entirely, and still lands and returns. Nothing on the original stack should
 * move at all.
 */
static void case_altstack(unsigned long events)
{
	struct outcome *c = case_open("altstack", "measure");
	uint64_t base = handler_calls;
	unsigned long i;
	HANDLE t;
	size_t altsz = 1u << 16;
	unsigned char *alt = malloc(altsz);
	uint64_t alt_top;

	if (!alt) {
		c->note = "out of memory for the alternate stack";
		return;
	}
	/* the high end, 16-aligned, with room above for the marker word */
	alt_top = ((uint64_t)(uintptr_t)(alt + altsz) - 64) & ~(uint64_t)15;

	t = start_watcher(watch_proc, 0);
	if (!t) {
		c->note = "could not start the watcher";
		free(alt);
		return;
	}
	for (i = 0; i < events; i++)
		if (deliver(t, 0, alt_top, 0, 0) == 0)
			c->deliveries++;
	stop_watcher(t);
	c->handled = handler_calls - base;
	collect_rz(c);
	free(alt);

	want(c, c->deliveries > 0, "no delivery completed");
	want(c, c->handled == c->deliveries,
	     "%llu deliveries but %llu handler runs",
	     (unsigned long long)c->deliveries, (unsigned long long)c->handled);
	want(c, c->rz_words == 0,
	     "the interrupted stack moved, %llu words, nearest at offset %llu",
	     (unsigned long long)c->rz_words, (unsigned long long)c->rz_shallow);
	if (!c->failures)
		detail(c, "%llu deliveries on an alternate stack, original red zone untouched",
		       (unsigned long long)c->deliveries);
}

/* ---- the report -------------------------------------------------------- */

static const char *state(const struct outcome *c)
{
	if (c->note)
		return "unrun";
	return c->failures ? "fail" : "pass";
}

static void report(FILE *out, int terse)
{
	int failed = 0, incomplete = 0, i;
	const char *verdict;

	for (i = 0; i < ncases; i++) {
		if (cases[i].note)
			incomplete++;
		else if (cases[i].failures)
			failed++;
	}
	if (incomplete)
		verdict = "incomplete";
	else
		verdict = failed ? "no" : "yes";

	if (!terse) {
		fprintf(out, "== reserving the red zone before the handler frame\n\n");
		fprintf(out, "    %-16s %-8s %8s %9s %8s %8s  %s\n",
			"", "kind", "deliver", "handled", "nearest", "words",
			"what it saw");
		for (i = 0; i < ncases; i++) {
			struct outcome *c = &cases[i];

			if (c->note) {
				fprintf(out, "    %-16s %-8s %8s %9s %8s %8s  %s\n",
					c->name, c->kind, "-", "-", "-", "-",
					c->note);
				continue;
			}
			fprintf(out, "    %-16s %-8s %8llu %9llu %8llu %8llu  %s\n",
				c->name, c->kind,
				(unsigned long long)c->deliveries,
				(unsigned long long)c->handled,
				(unsigned long long)c->rz_shallow,
				(unsigned long long)c->rz_words,
				c->detail);
		}
		fprintf(out, "\n    Nearest is the offset below the watched %%rsp of the closest\n"
			     "    word the delivery moved; a red zone is intact when nothing\n"
			     "    moved inside 128. quiet and deliver-naive are the controls\n"
			     "    the measurements lean on: the first says the watcher can\n"
			     "    see, the second says this model destroys the red zone where\n"
			     "    Cygwin's real path does, at offset 8.\n\n");
		fprintf(out, "== summary\n\n");
	}

#define K(fmt, ...) fprintf(out, "%s" fmt "\n", terse ? "" : "    ", __VA_ARGS__)
	K("verdict=%s", verdict);
	K("cases=%d", ncases);
	K("cases_failed=%d", failed);
	K("cases_incomplete=%d", incomplete);
	fprintf(out, "%sshape=", terse ? "" : "    ");
	for (i = 0; i < ncases; i++)
		fprintf(out, "%s%s:%s", i ? "," : "", cases[i].name,
			state(&cases[i]));
	fputc('\n', out);
	K("redzone_watched_bytes=%llu", (unsigned long long)depth);
	for (i = 0; i < ncases; i++) {
		struct outcome *c = &cases[i];

		if (c->note)
			K("case_%s=unrun", c->name);
		else
			fprintf(out, "%scase_%s=%s,nearest:%llu,words:%llu,deliveries:%llu,handled:%llu\n",
				terse ? "" : "    ", c->name, state(c),
				(unsigned long long)c->rz_shallow,
				(unsigned long long)c->rz_words,
				(unsigned long long)c->deliveries,
				(unsigned long long)c->handled);
	}
	/* The line a decision record would read: the reservation holds the red
	 * zone only if the reserved and derived cases kept everything inside
	 * 128 while the naive control proved the model can break it. */
	K("redzone_reservation=%s",
	  verdict[0] == 'y' ? "holds across delivery" : "not established");
	K("probe=%s", PROBE_VERSION);
#undef K
}

/* ---- driving ----------------------------------------------------------- */

static void usage(FILE *out)
{
	fputs("Usage:\n"
	      "  redzone [options]\n"
	      "\n"
	      "Options:\n"
	      "  -r N, --rounds=N   Passes for the quiet case. [default: 200000]\n"
	      "  -e N, --events=N   Deliveries per measured case. [default: 2000]\n"
	      "  -z N, --depth=N    Bytes below %rsp to watch, multiple of 8. [default: 1024]\n"
	      "  -c NAME, --case=NAME  Run one case rather than all seven.\n"
	      "  -t, --terse        The summary block alone, one key=value per line.\n"
	      "  -d, --debug        Name each case on stderr as it starts.\n"
	      "  -V, --version      Print the version and exit.\n"
	      "  -h, --help         Print this message and exit.\n", out);
}

static const char *only;

static int selected(const char *name)
{
	return !only || strcmp(only, name) == 0;
}

static long numeric(const char *what, const char *s)
{
	char *end;
	long v;

	errno = 0;
	v = strtol(s, &end, 0);
	if (errno || !*s || *end || v <= 0) {
		fprintf(stderr, "redzone: %s wants a positive number, not %s\n",
			what, s);
		exit(2);
	}
	return v;
}

static const char *known[] = {
	"quiet", "deliver-naive", "deliver-reserved", "resume-integrity",
	"handler-ran", "nested", "altstack", NULL
};

int main(int argc, char **argv)
{
	unsigned long rounds = 200000, events = 2000;
	int terse = 0, i;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		char *val = NULL;

#define OPT(short_, long_)						\
		(!strcmp(a, short_) || !strcmp(a, long_) ||		\
		 (!strncmp(a, long_ "=", strlen(long_) + 1) &&		\
		  (val = a + strlen(long_) + 1)))
#define ARG(what)							\
		(val ? val : (++i < argc ? argv[i] :			\
			     (fprintf(stderr, "redzone: %s wants a value\n", what), \
			      exit(2), (char *)NULL)))

		if (OPT("-h", "--help")) { usage(stdout); return 0; }
		else if (OPT("-V", "--version")) { puts(PROBE_VERSION); return 0; }
		else if (OPT("-t", "--terse")) terse = 1;
		else if (OPT("-d", "--debug")) debug = 1;
		else if (OPT("-r", "--rounds"))
			rounds = (unsigned long)numeric("--rounds", ARG("--rounds"));
		else if (OPT("-e", "--events"))
			events = (unsigned long)numeric("--events", ARG("--events"));
		else if (OPT("-z", "--depth"))
			depth = (uint64_t)numeric("--depth", ARG("--depth"));
		else if (OPT("-c", "--case"))
			only = ARG("--case");
		else {
			fprintf(stderr, "redzone: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
#undef OPT
#undef ARG
	}

	if (depth < 8 || depth % 8) {
		fprintf(stderr, "redzone: --depth wants a positive multiple of 8\n");
		return 2;
	}
	if (only) {
		for (i = 0; known[i]; i++)
			if (!strcmp(known[i], only))
				break;
		if (!known[i]) {
			fprintf(stderr, "redzone: no case named %s\n", only);
			return 2;
		}
	}

	if (selected("quiet"))			case_quiet(rounds);
	if (selected("deliver-naive"))		case_naive(events);
	if (selected("deliver-reserved"))	case_reserved(events);
	if (selected("resume-integrity"))	case_integrity(events);
	if (selected("handler-ran"))		case_handler_ran(events);
	if (selected("nested"))			case_nested(events);
	if (selected("altstack"))		case_altstack(events);

	report(stdout, terse);
	for (i = 0; i < ncases; i++)
		if (cases[i].note || cases[i].failures)
			return 1;
	return 0;
}
