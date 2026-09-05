/*
 * substrate_n.c -- substrate N, the native one: user code runs as ordinary NT
 * threads in the host process, reached through a gate, and the address space is
 * host virtual memory.  This is the first real implementation of the nine-call
 * interface (doc/design/Substrate-Interface.md); it replaces the mock and must
 * pass the identical conformance suite.
 *
 * Where the mock stood in for a mechanism, N carries the real one.  Two places
 * matter, and each is a decision the plan already fixed:
 *
 *   - the thread pointer is a runtime-owned word reached through %gs, keyed to
 *     NtTib.StackBase and a build-constant offset, exactly the carrier DR-0003
 *     chose and DR-0021 placed.  The mock kept a per-tid word in a side table;
 *     that measured its own mechanism rather than the contract, so N does not.
 *
 *   - as_clone forks a child *process* with RtlCloneUserProcess (spike 35), the
 *     ntdll call that returns a second time in the child on a live thread.  The
 *     mock snapshotted each mapping in-process because it had to; N's clone is
 *     a real copy-on-write across a process boundary, and the as_clone group is
 *     driven to match (the child certifies itself and reports its exit status).
 *
 * The rest is the same NT machinery the mock already proved coherent, because
 * that machinery is genuinely how N realises those calls: VirtualAlloc and its
 * siblings for the address space, native threads with suspend-and-rewrite for
 * the thread calls (spike 38), and a memcpy behind a vectored-handler fault
 * guard for the user copies, since GCC here has no __try/__except.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>

#include <stdlib.h>
#include <string.h>

#include "substrate.h"

/* ---- ntdll, resolved by name -------------------------------------------- */

/*
 * The clone and the cross-thread TEB read live in ntdll and nowhere in the
 * Win32 import libraries the suite links, so they are fetched by name at init.
 * Keeping them here rather than in an import means run.sh links N the same way
 * it links the mock, with no extra library on the command line.
 */
#define NT_CURRENT_PROCESS ((HANDLE)(LONG_PTR)-1)
#define STATUS_PROCESS_CLONED  ((LONG)0x00000129)
#define RTL_CLONE_INHERIT_HANDLES 0x00000002u

typedef struct _RTL_USER_PROCESS_INFORMATION {
	ULONG Length;
	HANDLE Process;
	HANDLE Thread;
	CLIENT_ID ClientId;
	BYTE ImageInformation[64];	/* SECTION_IMAGE_INFORMATION, opaque */
} RTL_USER_PROCESS_INFORMATION;

/* ThreadBasicInformation's payload; winternl names the class but not this. The
 * one field N wants is TebBaseAddress, at offset 8. */
typedef struct {
	LONG ExitStatus;
	PVOID TebBaseAddress;
	CLIENT_ID ClientId;
	ULONG_PTR AffinityMask;
	LONG Priority;
	LONG BasePriority;
} NT_TBI;

typedef LONG (NTAPI *fn_clone)(ULONG, PVOID, PVOID, HANDLE,
			      RTL_USER_PROCESS_INFORMATION *);
typedef LONG (NTAPI *fn_term)(HANDLE, LONG);
typedef LONG (NTAPI *fn_qit)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static fn_clone p_RtlCloneUserProcess;
static fn_term  p_NtTerminateProcess;
static fn_qit   p_NtQueryInformationThread;

static void resolve_ntdll(void)
{
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");

	if (!nt)
		return;
	p_RtlCloneUserProcess = (fn_clone)(void *)
		GetProcAddress(nt, "RtlCloneUserProcess");
	p_NtTerminateProcess = (fn_term)(void *)
		GetProcAddress(nt, "NtTerminateProcess");
	p_NtQueryInformationThread = (fn_qit)(void *)
		GetProcAddress(nt, "NtQueryInformationThread");
}

/* ---- the fault guard ---------------------------------------------------- */

/*
 * A guarded copy so a bad user pointer becomes a returned fault address, not a
 * dead kernel.  The guard is a vectored handler that longjmps back into the
 * copy when an access violation lands inside one, the same shape the mock uses
 * and for the same reason: this GCC has no structured __try/__except.  The
 * landing pad and the flag are per-thread because two threads can be mid-copy
 * at once.
 */
static __thread void *g_land[5];
static __thread volatile uint64_t g_fault;
static __thread volatile int g_guarding;

static LONG CALLBACK fault_veh(EXCEPTION_POINTERS *ep)
{
	DWORD code = ep->ExceptionRecord->ExceptionCode;

	if (g_guarding && code == EXCEPTION_ACCESS_VIOLATION) {
		g_fault = (uint64_t)ep->ExceptionRecord->ExceptionInformation[1];
		__builtin_longjmp(g_land, 1);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

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

/* ---- Linux prot to NT page protection ----------------------------------- */

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

/*
 * N tracks only what as_unmap and the idempotent as_map need to know: which
 * ranges began a VirtualAlloc reservation, so a whole-reservation drop can
 * RELEASE while a sub-range drop only DECOMMITs.  There are no per-mapping
 * shadows here -- the mock carried those to fake a clone in one process, and
 * N's clone is a real one across a process boundary, so the child reads its own
 * copy-on-write pages rather than a snapshot the parent keeps for it.
 */
#define N_MAX_MAPS 256

struct nmap {
	uint64_t base;
	size_t len;
	int reservation;
};

struct nsub {
	struct substrate api;
	struct nmap maps[N_MAX_MAPS];
	int nmaps;
	/* Set on the holder a successful cross-process as_clone hands back; the
	 * parent waits on it through clone_wait.  Zero on an ordinary space. */
	HANDLE child_proc;
	HANDLE child_thread;
};

static struct nmap *map_covering(struct nsub *n, uint64_t addr, size_t len)
{
	int i;

	for (i = 0; i < n->nmaps; i++) {
		struct nmap *m = &n->maps[i];

		if (addr >= m->base && addr + len <= m->base + m->len)
			return m;
	}
	return NULL;
}

static void map_add(struct nsub *n, uint64_t base, size_t len, int reservation)
{
	if (n->nmaps >= N_MAX_MAPS)
		return;
	n->maps[n->nmaps].base = base;
	n->maps[n->nmaps].len = len;
	n->maps[n->nmaps].reservation = reservation;
	n->nmaps++;
}

/* ---- the thread table --------------------------------------------------- */

/*
 * Threads belong to the process, so the table is global across substrates.
 * interruptible is the gate flag -- down inside the kernel path, up in user
 * code -- and pending is the latch thread_interrupt sets when it arrives while
 * the flag is down; the gate exit takes it exactly once.  tp is kept for the
 * record and for a tp_set that lands before the thread (and its stack) exists.
 */
#define N_MAX_THREADS 64

struct tent {
	int used;
	int tid;
	HANDLE handle;
	DWORD win_tid;
	uint64_t tp;
	volatile LONG interruptible;
	volatile LONG pending;
};

static struct tent g_threads[N_MAX_THREADS];

static struct tent *thread_find(int tid)
{
	int i;

	for (i = 0; i < N_MAX_THREADS; i++)
		if (g_threads[i].used && g_threads[i].tid == tid)
			return &g_threads[i];
	return NULL;
}

static struct tent *thread_alloc(int tid)
{
	int i;

	for (i = 0; i < N_MAX_THREADS; i++)
		if (!g_threads[i].used) {
			memset(&g_threads[i], 0, sizeof g_threads[i]);
			g_threads[i].used = 1;
			g_threads[i].tid = tid;
			g_threads[i].interruptible = 1;
			return &g_threads[i];
		}
	return NULL;
}

/* ---- the thread pointer: the %gs carrier -------------------------------- */

/*
 * DR-0003 settled that the thread pointer on this platform cannot be the FS
 * base -- a user-written FS base does not survive a deschedule (spike 1) -- so
 * it is a runtime-owned word reached through %gs.  DR-0021 fixed where the word
 * lives: keyed to NtTib.StackBase, a build-constant offset below it.  The read
 * is one chain, load StackBase from %gs then load the word below it, and that
 * is all substrate_thread_pointer does; nothing here consults a side table
 * keyed by thread id, which is the whole point of the carrier over the mock.
 *
 * A managed N thread runs on a stack the suite mapped and never touches its NT
 * thread stack, so the carrier word sits in that dormant stack a fixed distance
 * below its top -- committed from creation, and reached by the same %gs chain a
 * forked runtime will use against its own _cygtls.  The offset is the one piece
 * DR-0021 leaves to the runtime; N pins it here.
 */
#define CARRIER_OFF 0x100

uint64_t substrate_thread_pointer(void)
{
	uint64_t base, tp;

	/* %gs:[0x08] is NtTib.StackBase for the running thread. */
	__asm__ __volatile__("movq %%gs:0x08, %0" : "=r"(base));
	tp = *(volatile uint64_t *)(base - CARRIER_OFF);
	return tp;
}

/* Write the carrier for another thread, which the running thread cannot reach
 * through its own %gs.  Its TEB comes from NtQueryInformationThread, its
 * StackBase from NtTib at TEB+8, and the word a fixed offset below that -- the
 * same location that thread will later read through %gs.  Returns 0 on success. */
static int carrier_write(struct tent *t, uint64_t value)
{
	NT_TBI tbi;
	ULONG got = 0;
	uint64_t stackbase;

	if (!t->handle || !p_NtQueryInformationThread)
		return -1;
	memset(&tbi, 0, sizeof tbi);
	if (p_NtQueryInformationThread(t->handle, 0 /* ThreadBasicInformation */,
				       &tbi, sizeof tbi, &got) < 0)
		return -1;
	if (!tbi.TebBaseAddress)
		return -1;
	stackbase = *(uint64_t *)((uint64_t)(uintptr_t)tbi.TebBaseAddress + 0x08);
	*(volatile uint64_t *)(stackbase - CARRIER_OFF) = value;
	return 0;
}

/* ---- register mapping --------------------------------------------------- */

/*
 * The Linux user_regs the core reasons in, mapped to NT's CONTEXT.  N runs
 * native code, so only the integer file, rip, rsp and the flags cross; the
 * segment bases and selectors stay whatever the host thread already has, and
 * the thread pointer travels through the carrier rather than fs_base.
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

static int n_as_map(struct substrate *s, void **addr, size_t len,
		    const struct sub_backing *backing, uint64_t offset,
		    int prot)
{
	struct nsub *n = s->self;
	void *want = *addr;
	void *base;
	int reservation = 1;
	DWORD old;

	if (len == 0 || len > (SIZE_MAX >> 1))
		return -1;

	/*
	 * Re-realising a range the core's tree already describes must succeed,
	 * because clone and demand paging both re-issue as_map.  A request that
	 * falls inside a live reservation re-commits it rather than reserving
	 * afresh; otherwise this is a fresh reserve-and-commit.  Either way the
	 * page comes back writable so a backing can be laid down, then drops to
	 * the caller's protection.
	 */
	if (want && map_covering(n, (uint64_t)want, len)) {
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
		/* A shipping N reads an fd or a section view here; the suite
		 * carries the file's bytes, so N does the offsetting. */
		size_t avail = offset < backing->file_len
			     ? backing->file_len - offset : 0;
		size_t take = avail < len ? avail : len;

		if (take)
			memcpy(base, (const unsigned char *)backing->file_image
			       + offset, take);
	}
	/* Anonymous, stack and section backings read zero on first touch, which
	 * a just-committed page already gives. */

	if (!VirtualProtect(base, len, to_win_prot(prot), &old)) {
		if (reservation)
			VirtualFree(base, 0, MEM_RELEASE);
		return -1;
	}
	if (reservation)
		map_add(n, (uint64_t)base, len, 1);
	*addr = base;
	return 0;
}

static int n_as_unmap(struct substrate *s, void *addr, size_t len)
{
	struct nsub *n = s->self;
	struct nmap *m = map_covering(n, (uint64_t)addr, len);

	/* A whole reservation from its base is released outright and forgotten,
	 * so a later as_map may take the address again without meeting the
	 * idempotent path. */
	if (m && m->reservation && m->base == (uint64_t)addr && m->len == len) {
		if (!VirtualFree(addr, 0, MEM_RELEASE))
			return -1;
		*m = n->maps[--n->nmaps];
		return 0;
	}
	/* A sub-range decommits exactly its pages, so it faults while its
	 * neighbours in the same reservation stay live. */
	if (!VirtualFree(addr, len, MEM_DECOMMIT))
		return -1;
	return 0;
}

static int n_as_protect(struct substrate *s, void *addr, size_t len, int prot)
{
	DWORD old;

	(void)s;
	return VirtualProtect(addr, len, to_win_prot(prot), &old) ? 0 : -1;
}

/* ---- user_copy_in / user_copy_out --------------------------------------- */

/*
 * Under N user memory is host virtual memory the kernel shares an address space
 * with, so a copy is a guarded memcpy: the bytes move directly, and a bad user
 * pointer comes back as a fault address rather than a crash.  There is no
 * shadow indirection the mock needed for its in-process clone; a cloned child
 * is a separate process reading its own pages.
 */
static int n_user_copy_in(struct substrate *s, void *dst, uint64_t uaddr,
			  size_t len, uint64_t *fault)
{
	(void)s;
	return guarded_copy(dst, (const void *)(uintptr_t)uaddr, len, fault);
}

static int n_user_copy_out(struct substrate *s, uint64_t uaddr,
			   const void *src, size_t len, uint64_t *fault)
{
	(void)s;
	return guarded_copy((void *)(uintptr_t)uaddr, src, len, fault);
}

/* ---- as_clone ----------------------------------------------------------- */

static struct substrate *nsub_new(void);

/* The parent's wait on a cloned child: block until it exits and hand back its
 * exit status, which is the verdict the child's self-certification returned. */
static int n_clone_wait(struct substrate *child)
{
	struct nsub *c = child->self;
	DWORD code = (DWORD)-1;

	if (!c->child_proc)
		return -1;
	if (WaitForSingleObject(c->child_proc, 10000) != WAIT_OBJECT_0)
		return -1;
	if (!GetExitCodeProcess(c->child_proc, &code))
		return -1;
	return (int)code;
}

/*
 * RtlCloneUserProcess is the fork-shaped clone spike 35 found: it returns once
 * in the parent with the child's handles, and a second time in the child on the
 * cloned calling thread with STATUS_PROCESS_CLONED.  The child cannot return
 * into the conformance loop -- it would rerun the whole suite -- so it runs the
 * suite's registered self-certification and terminates with that verdict as its
 * exit code.  The child touches only its own cloned pages and ntdll on that
 * path, which is all a fresh clone may safely reach.
 */
static int n_as_clone(struct substrate *s, struct substrate **child)
{
	RTL_USER_PROCESS_INFORMATION info;
	struct substrate *cs;
	struct nsub *c;
	LONG st;

	if (!p_RtlCloneUserProcess || !p_NtTerminateProcess)
		return -1;

	memset(&info, 0, sizeof info);
	st = p_RtlCloneUserProcess(RTL_CLONE_INHERIT_HANDLES, NULL, NULL, NULL,
				   &info);

	if (st == STATUS_PROCESS_CLONED) {
		int verdict = s->clone_child_certify
			    ? s->clone_child_certify(s) : 0;

		p_NtTerminateProcess(NT_CURRENT_PROCESS, (LONG)verdict);
		return 0;		/* not reached */
	}
	if (st < 0)
		return -1;

	cs = nsub_new();
	if (!cs) {
		p_NtTerminateProcess(info.Process, 1);
		CloseHandle(info.Process);
		CloseHandle(info.Thread);
		return -1;
	}
	c = cs->self;
	c->child_proc = info.Process;
	c->child_thread = info.Thread;
	cs->clone_wait = n_clone_wait;
	*child = cs;
	return 0;
}

/* ---- threads ------------------------------------------------------------ */

/*
 * A started thread never runs this: thread_start rewrites its rip to the
 * supplied register state before it is resumed.  A thread that runs its user
 * code to the end returns here through the address thread_start left on the
 * stack, and exits.
 */
static DWORD WINAPI n_boot(LPVOID p)
{
	(void)p;
	return 0;
}

__attribute__((force_align_arg_pointer))
static void n_thread_return(void)
{
	ExitThread(0);
}

static int n_thread_start(struct substrate *s, int tid,
			  const struct sub_regs *ctx, uint64_t tls)
{
	struct tent *t = thread_find(tid);
	CONTEXT wc;

	(void)s;
	/* An existing tid is a resume: the thread was stopped by an interrupt
	 * and the core hands a register state to run from.  Set it exactly, rsp
	 * included, so a red-zone-respecting frame is honoured. */
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
	t->handle = CreateThread(NULL, 0, n_boot, NULL, CREATE_SUSPENDED,
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

	/* Lay a return address so user code can simply return; the slot sits at
	 * a 16-aligned address minus 8, the alignment a call-entered function
	 * expects, leaving the red zone below rsp untouched. */
	{
		uint64_t sp = (ctx->rsp & ~(uint64_t)15) - 8;

		*(uint64_t *)(uintptr_t)sp = (uint64_t)(uintptr_t)n_thread_return;
		wc.Rsp = sp;
	}
	if (!SetThreadContext(t->handle, &wc)) {
		CloseHandle(t->handle);
		t->used = 0;
		return -1;
	}
	/* The thread has a stack and a TEB now, so its carrier can be planted
	 * before it runs: it will read this back through %gs at first touch. */
	carrier_write(t, tls);
	ResumeThread(t->handle);
	return 0;
}

static int n_thread_interrupt(struct substrate *s, int tid)
{
	struct tent *t = thread_find(tid);

	(void)s;
	if (!t)
		return -1;

	/* Down in the kernel path: latch it.  Repeated interrupts across the
	 * window collapse to one pending, taken once at the gate's exit. */
	if (!t->interruptible) {
		InterlockedExchange(&t->pending, 1);
		return 0;
	}
	/* In user code: force it out now by suspending where it runs.  It stays
	 * stopped for the core to read and rewrite until a resume. */
	if (SuspendThread(t->handle) == (DWORD)-1)
		return -1;
	return 0;
}

static int n_thread_context(struct substrate *s, int tid, struct sub_regs *ctx)
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
	ctx->gs_base = t->tp;		/* the value behind the carrier */
	return 0;
}

static int n_set_context(struct substrate *s, int tid,
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
	/* SetThreadContext moves registers only and writes no user stack, so
	 * the frame the core built 128 bytes below the interrupted rsp is left
	 * whole by construction. */
	if (!SetThreadContext(t->handle, &wc))
		return -1;
	return 0;
}

static int n_tp_set(struct substrate *s, int tid, uint64_t base)
{
	struct tent *t = thread_find(tid);

	(void)s;
	if (!t)
		t = thread_alloc(tid);		/* tp_set may precede a start */
	if (!t)
		return -1;
	t->tp = base;
	/* If the thread exists, write the carrier now; otherwise thread_start
	 * plants it from t->tp once the stack is there. */
	if (t->handle)
		return carrier_write(t, base);
	return 0;
}

/* ---- the gate, beside the nine calls ------------------------------------ */

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

static void n_global_init(void)
{
	if (InterlockedCompareExchange(&g_inited, 1, 0) == 0) {
		resolve_ntdll();
		AddVectoredExceptionHandler(1, fault_veh);
	}
}

static struct substrate *nsub_new(void)
{
	struct nsub *n = calloc(1, sizeof *n);

	if (!n)
		return NULL;
	n_global_init();
	n->api.self = n;
	n->api.as_map = n_as_map;
	n->api.as_unmap = n_as_unmap;
	n->api.as_protect = n_as_protect;
	n->api.as_clone = n_as_clone;
	n->api.thread_start = n_thread_start;
	n->api.thread_interrupt = n_thread_interrupt;
	n->api.thread_context = n_thread_context;
	n->api.set_context = n_set_context;
	n->api.tp_set = n_tp_set;
	n->api.user_copy_in = n_user_copy_in;
	n->api.user_copy_out = n_user_copy_out;
	/* N's clone forks a real child process, so the as_clone group drives it
	 * cross-process rather than operating an in-process copy. */
	n->api.clone_cross_process = 1;
	return &n->api;
}

struct substrate *substrate_create(void)
{
	return nsub_new();
}
