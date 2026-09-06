/*
 * mock_substrate.c -- a working substrate over NT/Win32 primitives, the mock
 * the spec's Conformance section describes: VirtualAlloc/VirtualProtect/
 * VirtualFree for the address space, native threads with suspend and context
 * rewrite for the thread calls, a runtime-owned per-thread word for the thread
 * pointer, and a memcpy behind a fault guard for the user copies.
 *
 * Its job is not to be a real substrate.  It is to prove the interface's
 * contract is coherent and its conformance suite is real before N or H exists;
 * a suite no implementation has ever passed is a wish.  Two places stand in for
 * mechanism a real substrate owns, and each says so where it stands:
 *
 *   - as_clone is a same-process copy, not a process fork.  It satisfies the
 *     contract the suite tests -- the child reads the parent's pre-clone
 *     contents and later writes on either side stay private -- by snapshotting
 *     each mapping into a private shadow the child's user copies read and write.
 *     A real N clone is RtlCloneUserProcess (spike 35); H duplicates guest page
 *     tables copy-on-write.
 *
 *   - the fault guard is a vectored exception handler with __builtin_setjmp,
 *     because GCC on this toolchain has no MSVC __try/__except.  It catches an
 *     access violation inside a guarded copy, records the faulting address, and
 *     returns to the copy's caller with a failure -- the same observable the
 *     spec asks of __try/__except, reached the way this compiler allows.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>

#include "substrate.h"

/* ---- the fault guard ---------------------------------------------------- */

/*
 * Per-thread, because two threads may be inside a guarded copy at once and each
 * needs its own landing point.  __builtin_setjmp wants a buffer of five words
 * on this target; the vectored handler longjmps into it when a guarded copy
 * touches an address that faults.
 */
static __thread void *g_land[5];
static __thread volatile uint64_t g_fault;
static __thread volatile int g_guarding;

static LONG CALLBACK fault_veh(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;

	if (g_guarding && code == EXCEPTION_ACCESS_VIOLATION) {
		/* ExceptionInformation[1] is the address the access hit -- the
		 * fault address the core turns into EFAULT. */
		g_fault = (uint64_t)ep->ExceptionRecord->ExceptionInformation[1];
		__builtin_longjmp(g_land, 1);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

/* A memcpy that returns -1 with the fault address in *fault when it touches a
 * bad address, instead of taking the process down with it. */
static int guarded_copy(void *dst, const void *src, size_t len, uint64_t *fault)
{
	g_guarding = 1;
	g_fault = 0;
	if (__builtin_setjmp(g_land)) {
		g_guarding = 0;
		if (fault)
			*fault = g_fault;
		return -1;
	}
	memcpy(dst, src, len);
	g_guarding = 0;
	return 0;
}

/* ---- protection mapping ------------------------------------------------- */

static DWORD to_win_prot(int prot)
{
	int r = prot & SUB_PROT_READ;
	int w = prot & SUB_PROT_WRITE;
	int x = prot & SUB_PROT_EXEC;

	if (x)
		return w ? PAGE_EXECUTE_READWRITE
			 : (r ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	if (w)
		return PAGE_READWRITE;
	if (r)
		return PAGE_READONLY;
	return PAGE_NOACCESS;
}

/* ---- the address space -------------------------------------------------- */

#define MOCK_MAX_MAPS 256

/*
 * One realised range.  For a parent, base is live memory VirtualAlloc handed
 * back and shadow is NULL; the user copies hit base directly.  For a clone
 * child, shadow holds the private copy the child's copies read and write, so a
 * write on either side stays on its own side.  reservation marks the range that
 * began a VirtualAlloc reservation, which is the only one as_unmap may RELEASE.
 */
struct mapping {
	uint64_t base;
	size_t len;
	int reservation;
	unsigned char *shadow;
};

struct mock {
	struct substrate api;
	struct mapping maps[MOCK_MAX_MAPS];
	int nmaps;
	int is_child;
};

static struct mapping *map_covering(struct mock *m, uint64_t addr, size_t len)
{
	int i;

	for (i = 0; i < m->nmaps; i++) {
		struct mapping *mp = &m->maps[i];

		if (addr >= mp->base && addr + len <= mp->base + mp->len)
			return mp;
	}
	return NULL;
}

/* A cloned range whose bytes live in a shadow, if this address falls in one. */
static struct mapping *shadow_covering(struct mock *m, uint64_t addr, size_t len)
{
	struct mapping *mp = map_covering(m, addr, len);

	return (mp && mp->shadow) ? mp : NULL;
}

static void map_add(struct mock *m, uint64_t base, size_t len, int reservation,
		    unsigned char *shadow)
{
	struct mapping *mp;

	if (m->nmaps >= MOCK_MAX_MAPS)
		return;
	mp = &m->maps[m->nmaps++];
	mp->base = base;
	mp->len = len;
	mp->reservation = reservation;
	mp->shadow = shadow;
}

/* ---- the thread table --------------------------------------------------- */

/*
 * Threads belong to the process, not to one substrate, so the table is global.
 * interruptible is the gate flag the design names -- lowered inside the kernel
 * path, raised in user code -- and pending is the latch a thread_interrupt sets
 * when it arrives while the flag is down.  Both are touched from more than one
 * thread, so both move through the interlocked calls.
 */
#define MOCK_MAX_THREADS 64

struct tent {
	int used;
	int tid;
	HANDLE handle;
	DWORD win_tid;
	volatile uint64_t tp;		/* the runtime-owned thread pointer */
	volatile LONG interruptible;
	volatile LONG pending;
};

static struct tent g_threads[MOCK_MAX_THREADS];
static CRITICAL_SECTION g_tlock;

static struct tent *thread_find(int tid)
{
	int i;

	for (i = 0; i < MOCK_MAX_THREADS; i++)
		if (g_threads[i].used && g_threads[i].tid == tid)
			return &g_threads[i];
	return NULL;
}

static struct tent *thread_alloc(int tid)
{
	int i;

	for (i = 0; i < MOCK_MAX_THREADS; i++)
		if (!g_threads[i].used) {
			memset(&g_threads[i], 0, sizeof g_threads[i]);
			g_threads[i].used = 1;
			g_threads[i].tid = tid;
			g_threads[i].interruptible = 1;
			return &g_threads[i];
		}
	return NULL;
}

/* ---- register mapping --------------------------------------------------- */

/*
 * Between the Linux register set the core reasons in and NT's CONTEXT.  Only
 * the integer file, rip, rsp and the flags are carried: the mock runs native
 * code, so the segment bases and selectors are the host's and are left as the
 * thread already has them.  A real N substrate maps the same set; H maps to
 * WHP registers.
 */
static void regs_to_ctx(const struct sub_regs *r, CONTEXT *c)
{
	c->Rax = r->rax; c->Rbx = r->rbx; c->Rcx = r->rcx; c->Rdx = r->rdx;
	c->Rsi = r->rsi; c->Rdi = r->rdi; c->Rbp = r->rbp; c->Rsp = r->rsp;
	c->R8 = r->r8; c->R9 = r->r9; c->R10 = r->r10; c->R11 = r->r11;
	c->R12 = r->r12; c->R13 = r->r13; c->R14 = r->r14; c->R15 = r->r15;
	c->Rip = r->rip;
	if (r->eflags)
		c->EFlags = (DWORD)r->eflags;
}

static void ctx_to_regs(const CONTEXT *c, struct sub_regs *r)
{
	memset(r, 0, sizeof *r);
	r->rax = c->Rax; r->rbx = c->Rbx; r->rcx = c->Rcx; r->rdx = c->Rdx;
	r->rsi = c->Rsi; r->rdi = c->Rdi; r->rbp = c->Rbp; r->rsp = c->Rsp;
	r->r8 = c->R8; r->r9 = c->R9; r->r10 = c->R10; r->r11 = c->R11;
	r->r12 = c->R12; r->r13 = c->R13; r->r14 = c->R14; r->r15 = c->R15;
	r->rip = c->Rip;
	r->eflags = c->EFlags;
}

/* ---- as_map / as_unmap / as_protect ------------------------------------- */

static int m_as_map(struct substrate *s, void **addr, size_t len,
		    const struct sub_backing *backing, uint64_t offset,
		    int prot)
{
	struct mock *m = s->self;
	void *want = *addr;
	void *base;
	int reservation = 1;
	DWORD old;

	if (len == 0 || len > (SIZE_MAX >> 1))
		return -1;

	/*
	 * Commit writable first so the backing can be laid down, then drop to
	 * the requested protection.  Re-realising a range the tree already
	 * describes must not be an error -- clone and demand paging both
	 * re-issue as_map -- so a request inside an existing reservation just
	 * re-commits rather than reserving afresh.
	 */
	if (want && map_covering(m, (uint64_t)want, len)) {
		base = VirtualAlloc(want, len, MEM_COMMIT, PAGE_READWRITE);
		reservation = 0;
	} else {
		base = VirtualAlloc(want, len, MEM_RESERVE | MEM_COMMIT,
				    PAGE_READWRITE);
	}
	if (!base)
		return -1;			/* a failed map leaves nothing */

	if (backing && backing->kind == SUB_BACKING_FILE &&
	    backing->file_image) {
		/* A real substrate reads an fd or an NT section here; the mock
		 * carries the file's bytes and does the offsetting itself. */
		size_t avail = offset < backing->file_len
			     ? backing->file_len - offset : 0;
		size_t n = avail < len ? avail : len;

		if (n)
			memcpy(base,
			       (const unsigned char *)backing->file_image + offset,
			       n);
	}
	/* Anonymous, section and stack backings read zero on first touch, which
	 * a freshly committed page already does. */

	if (!VirtualProtect(base, len, to_win_prot(prot), &old)) {
		if (reservation)
			VirtualFree(base, 0, MEM_RELEASE);
		return -1;
	}
	if (reservation)
		map_add(m, (uint64_t)base, len, 1, NULL);
	*addr = base;
	return 0;
}

static int m_as_unmap(struct substrate *s, void *addr, size_t len)
{
	struct mock *m = s->self;
	struct mapping *mp = map_covering(m, (uint64_t)addr, len);

	/* Whole reservation from its base: release it, and forget it so a later
	 * as_map may reuse the address without tripping the idempotent path. */
	if (mp && mp->reservation && mp->base == (uint64_t)addr &&
	    mp->len == len) {
		if (!VirtualFree(addr, 0, MEM_RELEASE))
			return -1;
		*mp = m->maps[--m->nmaps];
		return 0;
	}
	/* A sub-range, or a span within a reservation: decommit exactly those
	 * pages so the range faults while its neighbours stay committed. */
	if (!VirtualFree(addr, len, MEM_DECOMMIT))
		return -1;
	return 0;
}

static int m_as_protect(struct substrate *s, void *addr, size_t len, int prot)
{
	DWORD old;

	(void)s;
	if (!VirtualProtect(addr, len, to_win_prot(prot), &old))
		return -1;
	return 0;
}

/* ---- as_clone ----------------------------------------------------------- */

static struct substrate *mock_new(int is_child);

static int m_as_clone(struct substrate *s, struct substrate **child)
{
	struct mock *m = s->self;
	struct substrate *cs = mock_new(1);
	struct mock *c;
	int i;

	if (!cs)
		return -1;
	c = cs->self;

	/*
	 * Snapshot each live range into a shadow the child owns.  The child
	 * reads the parent's contents as they stand now, and from here a write
	 * on the child lands in its shadow while a write on the parent lands in
	 * the live page, so neither sees the other -- the copy-on-write the
	 * contract asks for, done eagerly because the mock is not chasing the
	 * cost a real substrate would.
	 */
	for (i = 0; i < m->nmaps; i++) {
		struct mapping *mp = &m->maps[i];
		unsigned char *snap;

		if (!mp->reservation)
			continue;
		snap = malloc(mp->len);
		if (!snap)
			return -1;
		/* The parent range may be protected against reads; guard the
		 * snapshot so a PROT_NONE page yields zeroes rather than a
		 * crash. */
		if (guarded_copy(snap, (const void *)mp->base, mp->len, NULL))
			memset(snap, 0, mp->len);
		map_add(c, mp->base, mp->len, 0, snap);
	}
	*child = cs;
	return 0;
}

/* ---- user_copy_in / user_copy_out --------------------------------------- */

static int m_user_copy_in(struct substrate *s, void *dst, uint64_t uaddr,
			  size_t len, uint64_t *fault)
{
	struct mock *m = s->self;
	struct mapping *mp = shadow_covering(m, uaddr, len);

	if (mp) {
		memcpy(dst, mp->shadow + (uaddr - mp->base), len);
		return 0;
	}
	return guarded_copy(dst, (const void *)uaddr, len, fault);
}

static int m_user_copy_out(struct substrate *s, uint64_t uaddr,
			   const void *src, size_t len, uint64_t *fault)
{
	struct mock *m = s->self;
	struct mapping *mp = shadow_covering(m, uaddr, len);

	if (mp) {
		memcpy(mp->shadow + (uaddr - mp->base), src, len);
		return 0;
	}
	return guarded_copy((void *)uaddr, src, len, fault);
}

/* ---- threads ------------------------------------------------------------ */

/*
 * A created thread never runs this: thread_start redirects its rip to the
 * supplied register state before resuming it, so the boot routine is only the
 * entry CreateThread demands.  A thread that reaches the end of the user code
 * it was started on returns here instead, through the return address
 * thread_start leaves on its stack, and exits cleanly.
 */
static DWORD WINAPI mock_boot(LPVOID p)
{
	(void)p;
	return 0;
}

__attribute__((force_align_arg_pointer))
static void mock_thread_return(void)
{
	/* Reached by a `ret` rather than a `call`, so the stack is off the
	 * 16-byte alignment a prologue assumes; force_align_arg_pointer fixes
	 * it before anything here touches aligned state. */
	ExitThread(0);
}

static int m_thread_start(struct substrate *s, int tid,
			  const struct sub_regs *ctx, uint64_t tls)
{
	struct tent *t = thread_find(tid);
	CONTEXT wc;

	(void)s;
	/* An existing tid is a resume: the thread was stopped by
	 * thread_interrupt, and the core hands back a register state to run
	 * from.  Set it exactly -- rsp included, so a red-zone-respecting frame
	 * is honoured -- and let it go. */
	if (t) {
		memset(&wc, 0, sizeof wc);
		wc.ContextFlags = CONTEXT_FULL;
		if (!GetThreadContext(t->handle, &wc))
			return -1;
		regs_to_ctx(ctx, &wc);
		if (!SetThreadContext(t->handle, &wc))
			return -1;
		ResumeThread(t->handle);
		return 0;
	}

	t = thread_alloc(tid);
	if (!t)
		return -1;
	t->tp = tls;
	t->handle = CreateThread(NULL, 0, mock_boot, NULL, CREATE_SUSPENDED,
				 &t->win_tid);
	if (!t->handle) {
		t->used = 0;
		return -1;
	}
	memset(&wc, 0, sizeof wc);
	wc.ContextFlags = CONTEXT_FULL;
	if (!GetThreadContext(t->handle, &wc)) {
		CloseHandle(t->handle);
		t->used = 0;
		return -1;
	}
	regs_to_ctx(ctx, &wc);

	/*
	 * Lay a return address so the user code can simply `return`.  The slot
	 * sits at a 16-aligned address minus 8, which is the alignment a
	 * function entered by a call expects, and it leaves the whole red zone
	 * below rsp untouched.
	 */
	{
		uint64_t sp = (ctx->rsp & ~(uint64_t)15) - 8;

		*(uint64_t *)(uintptr_t)sp =
			(uint64_t)(uintptr_t)mock_thread_return;
		wc.Rsp = sp;
	}
	if (!SetThreadContext(t->handle, &wc)) {
		CloseHandle(t->handle);
		t->used = 0;
		return -1;
	}
	ResumeThread(t->handle);
	return 0;
}

static int m_thread_interrupt(struct substrate *s, int tid)
{
	struct tent *t = thread_find(tid);

	(void)s;
	if (!t)
		return -1;

	/* In the kernel path: latch it.  Repeated interrupts across the window
	 * collapse to one pending, which the gate exit takes exactly once. */
	if (!t->interruptible) {
		InterlockedExchange(&t->pending, 1);
		return 0;
	}
	/* In user code: force it out now, by suspending it where it runs.  It
	 * stays stopped for the core to read and rewrite until thread_start
	 * resumes it. */
	if (SuspendThread(t->handle) == (DWORD)-1)
		return -1;
	return 0;
}

static int m_thread_context(struct substrate *s, int tid, struct sub_regs *ctx)
{
	struct tent *t = thread_find(tid);
	CONTEXT wc;

	(void)s;
	if (!t)
		return -1;
	memset(&wc, 0, sizeof wc);
	wc.ContextFlags = CONTEXT_FULL;
	if (!GetThreadContext(t->handle, &wc))
		return -1;
	ctx_to_regs(&wc, ctx);
	ctx->fs_base = 0;
	ctx->gs_base = t->tp;		/* the carrier the pointer lives in */
	return 0;
}

static int m_set_context(struct substrate *s, int tid,
			 const struct sub_regs *ctx)
{
	struct tent *t = thread_find(tid);
	CONTEXT wc;

	(void)s;
	if (!t)
		return -1;
	memset(&wc, 0, sizeof wc);
	wc.ContextFlags = CONTEXT_FULL;
	if (!GetThreadContext(t->handle, &wc))
		return -1;
	regs_to_ctx(ctx, &wc);
	/* SetThreadContext moves registers only; it writes no user stack, so
	 * the red zone below the interrupted rsp is left whole by construction.
	 * The core placed its frame there, and the substrate does not touch
	 * it. */
	if (!SetThreadContext(t->handle, &wc))
		return -1;
	return 0;
}

static int m_tp_set(struct substrate *s, int tid, uint64_t base)
{
	struct tent *t = thread_find(tid);

	(void)s;
	if (!t)
		t = thread_alloc(tid);		/* tp_set may precede a start */
	if (!t)
		return -1;
	t->tp = base;
	return 0;
}

/* ---- the runtime machinery beside the nine calls ------------------------ */

uint64_t substrate_thread_pointer(void)
{
	DWORD self = GetCurrentThreadId();
	int i;

	/* The %gs carrier under N: a runtime-owned word keyed by the running
	 * thread, which survives a deschedule where a user-written FS base does
	 * not (spike 1, DR-0101). */
	for (i = 0; i < MOCK_MAX_THREADS; i++)
		if (g_threads[i].used && g_threads[i].win_tid == self)
			return g_threads[i].tp;
	return 0;
}

int substrate_gate_enter(struct substrate *s, int tid)
{
	struct tent *t = thread_find(tid);

	(void)s;
	if (!t)
		return -1;
	InterlockedExchange(&t->interruptible, 0);
	return 0;
}

int substrate_gate_exit(struct substrate *s, int tid)
{
	struct tent *t = thread_find(tid);

	(void)s;
	if (!t)
		return -1;
	InterlockedExchange(&t->interruptible, 1);
	/* Take the latch, if one is set, exactly once. */
	return InterlockedExchange(&t->pending, 0) ? 1 : 0;
}

/* ---- construction ------------------------------------------------------- */

static LONG g_inited;

static void mock_global_init(void)
{
	/* Register the fault handler and the table lock once, whichever
	 * substrate instance is built first. */
	if (InterlockedCompareExchange(&g_inited, 1, 0) == 0) {
		InitializeCriticalSection(&g_tlock);
		AddVectoredExceptionHandler(1, fault_veh);
	}
}

static struct substrate *mock_new(int is_child)
{
	struct mock *m = calloc(1, sizeof *m);

	if (!m)
		return NULL;
	mock_global_init();
	m->is_child = is_child;
	m->api.self = m;
	m->api.as_map = m_as_map;
	m->api.as_unmap = m_as_unmap;
	m->api.as_protect = m_as_protect;
	m->api.as_clone = m_as_clone;
	m->api.thread_start = m_thread_start;
	m->api.thread_interrupt = m_thread_interrupt;
	m->api.thread_context = m_thread_context;
	m->api.set_context = m_set_context;
	m->api.tp_set = m_tp_set;
	m->api.user_copy_in = m_user_copy_in;
	m->api.user_copy_out = m_user_copy_out;
	return &m->api;
}

struct substrate *substrate_create(void)
{
	return mock_new(0);
}
