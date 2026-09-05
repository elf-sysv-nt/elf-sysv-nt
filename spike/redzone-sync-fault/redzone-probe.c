/* redzone-probe: does NT's exception dispatch write into the 128 bytes below
 * the faulting thread's %rsp?
 *
 * Substrate N takes every synchronous fault in user code -- the first touch of
 * a lazily committed page, a SIGSEGV, an int3 under ptrace -- as an NT
 * exception dispatched to the faulting thread and handled by a vectored
 * handler. DR-0050 retired -mno-red-zone on evidence from the asynchronous
 * path (a hijack from another thread, spike 38), where the kernel builds the
 * frame and can leave the gap. On the synchronous path NT builds the frame, on
 * the faulting thread's stack, before any handler runs; a System V leaf keeps
 * live temporaries in that gap. This measures whether they survive.
 *
 * Native, built with x86_64-w64-mingw32-gcc plus redzone.S: the dispatch under
 * test is NT's own and no runtime may sit between it and the leaf.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#define RELEASE "redzone-probe 1.0"

extern uint64_t redzone_leaf(void *page, uint64_t *first_bad, int kind);

static void emit(const char *k, const char *fmt, ...)
{
	va_list ap;
	printf("%s=", k);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

/* What the handler sees: where NT put the dispatch records relative to the
 * interrupted %rsp, recorded once per fault kind. */
static volatile uint8_t *g_page;
static volatile LONG g_faults, g_breaks, g_unexpected;
static volatile int g_sabotage;   /* the negative control: the handler clobbers rsp-8 itself */
static volatile uint64_t g_ctx_below_rsp, g_rec_below_rsp, g_handler_rsp_below;

static LONG CALLBACK handler(EXCEPTION_POINTERS *ep)
{
	CONTEXT *ctx = ep->ContextRecord;
	EXCEPTION_RECORD *rec = ep->ExceptionRecord;
	uint64_t rsp = ctx->Rsp;
	volatile int here;

	if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && g_page
	    && rec->ExceptionInformation[1] >= (uintptr_t) g_page
	    && rec->ExceptionInformation[1] < (uintptr_t) g_page + 4096) {
		g_ctx_below_rsp = rsp - (uint64_t) (uintptr_t) ctx;
		g_rec_below_rsp = rsp - (uint64_t) (uintptr_t) rec;
		g_handler_rsp_below = rsp - (uint64_t) (uintptr_t) &here;
		if (!VirtualAlloc((void *) g_page, 4096, MEM_COMMIT, PAGE_READWRITE))
			return EXCEPTION_CONTINUE_SEARCH;
		InterlockedIncrement(&g_faults);
		return EXCEPTION_CONTINUE_EXECUTION;   /* the store re-executes */
	}
	if (rec->ExceptionCode == EXCEPTION_BREAKPOINT) {
		g_ctx_below_rsp = rsp - (uint64_t) (uintptr_t) ctx;
		g_rec_below_rsp = rsp - (uint64_t) (uintptr_t) rec;
		g_handler_rsp_below = rsp - (uint64_t) (uintptr_t) &here;
		ctx->Rip += 1;                          /* step over int3 */
		if (g_sabotage)
			*(volatile uint8_t *) (uintptr_t) (rsp - 8) ^= 0xff;   /* the control: one byte, 8 below */
		InterlockedIncrement(&g_breaks);
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	InterlockedIncrement(&g_unexpected);
	return EXCEPTION_CONTINUE_SEARCH;
}

/* One kind, n times: fresh reserved page per iteration for kind 0, a committed
 * one for kind 2, none needed for kind 1. Reports how many runs lost bytes,
 * the fewest and most bytes lost, and the nearest offset to %rsp that was
 * ever clobbered (1 = the byte just below %rsp). */
static void run_kind(const char *tag, int kind, int n)
{
	int lost_runs = 0, i;
	uint64_t min_lost = ~0ULL, max_lost = 0, nearest = 129, first_bad;
	uint8_t *page;
	char key[64];

	g_ctx_below_rsp = g_rec_below_rsp = g_handler_rsp_below = 0;
	for (i = 0; i < n; i++) {
		uint64_t lost;
		if (kind == 2)
			page = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		else
			page = VirtualAlloc(NULL, 4096, MEM_RESERVE, PAGE_READWRITE);
		g_page = page;
		first_bad = 0;
		lost = redzone_leaf(page, &first_bad, kind);
		g_page = NULL;
		if (lost) {
			lost_runs++;
			if (lost < min_lost) min_lost = lost;
			if (lost > max_lost) max_lost = lost;
			/* first_bad counts down from 128, so it is the farthest clobbered
			 * offset found first; the nearest is what a leaf cares about, and
			 * with a contiguous frame it is first_bad - lost + 1 */
			if (first_bad && first_bad - lost + 1 < nearest) nearest = first_bad - lost + 1;
		}
		if (page) VirtualFree(page, 0, MEM_RELEASE);
	}
#define K(name) (snprintf(key, sizeof key, "%s_%s", tag, name), key)
	emit(K("runs"), "%d", n);
	emit(K("runs_with_loss"), "%d", lost_runs);
	emit(K("bytes_lost_min"), "%llu", lost_runs ? (unsigned long long) min_lost : 0ULL);
	emit(K("bytes_lost_max"), "%llu", (unsigned long long) max_lost);
	emit(K("nearest_clobbered_offset"), "%llu", nearest == 129 ? 0ULL : (unsigned long long) nearest);
	emit(K("context_below_rsp"), "%llu", (unsigned long long) g_ctx_below_rsp);
	emit(K("record_below_rsp"), "%llu", (unsigned long long) g_rec_below_rsp);
	emit(K("handler_frame_below_rsp"), "%llu", (unsigned long long) g_handler_rsp_below);
#undef K
}

int main(int argc, char **argv)
{
	int i, n = 1000;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--iterations") && i + 1 < argc) { n = atoi(argv[++i]); continue; }
		fprintf(stderr, "redzone-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!AddVectoredExceptionHandler(1, handler)) { emit("handler_installed", "0"); return 1; }
	emit("handler_installed", "1");

	run_kind("q1_lazy_commit_fault", 0, n);
	run_kind("q2_breakpoint", 1, n);
	run_kind("q3_control_no_fault", 2, n);
	/* q4. The watcher has to be able to see a loss, or an intact red zone
	 * means nothing: the handler writes one byte eight below %rsp itself. */
	g_sabotage = 1;
	run_kind("q4_control_handler_clobbers", 1, n);
	g_sabotage = 0;
	emit("faults_handled", "%ld", (long) g_faults);
	emit("breakpoints_handled", "%ld", (long) g_breaks);
	emit("unexpected_exceptions", "%ld", (long) g_unexpected);
	return 0;
}
