/* Does NtCreateProcessEx with a null section clone this process on Windows 11?
 *
 * Proposal 0011 rests `fork` on the executive's clone: open question 2 asks
 * whether the primitive is sound on this build, and the "Not verified" section
 * lists it first among the NT behaviours the proposal reads out of
 * documentation rather than out of a measurement. Interix used it; its source
 * was never public. So this measures it here.
 *
 * Seven questions. The first four and the sixth need no thread in the child at
 * all -- the parent reaches into the clone with NtReadVirtualMemory and
 * NtWriteVirtualMemory, which is the honest way to ask what an address space
 * contains without depending on the part that historically breaks. The fifth
 * asks the part that historically breaks, on its own, on fresh clones, so that
 * a hang there cannot contaminate the answers above it.
 *
 * Native, built with x86_64-w64-mingw32-gcc. The design's host process has no
 * Cygwin under it, so a Cygwin binary would be measuring the wrong process.
 * Cygwin's fork appears here only as a number beside ours, taken by a separate
 * program; see fork-timer.c.
 *
 * Output is one key=value line per fact on stdout. It measures and decides
 * nothing; measure.sh reads the keys and states the verdict.
 *
 * Usage:
 *   clone-probe [options]
 *
 * Options:
 *   -n N, --iterations=N  Clones to time for q7. [default: 200]
 *   -v, --verbose         Narrate each question as it runs.
 *   -t, --terse           The key=value block alone.
 *   -V, --version         Print the version and exit.
 *   -h, --help            Print this message and exit.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#define RELEASE "clone-probe 1.0"

#ifndef OBJ_INHERIT
#define OBJ_INHERIT 0x00000002L
#endif

#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES 0x00000004UL

/* SECTION_INHERIT */
#define VIEW_SHARE 1

#define NT_OK(s) (((NTSTATUS)(s)) >= 0)

#define NT_CURRENT_PROCESS ((HANDLE)(LONG_PTR) -1)
#define NT_CURRENT_THREAD  ((HANDLE)(LONG_PTR) -2)

/* ntdll, resolved by name rather than linked, so the probe builds against any
 * w32api import library and so the child worker below calls through pointers
 * that were already filled in when the address space was cloned. */
typedef NTSTATUS (NTAPI *fn_CreateProcessEx)(PHANDLE, ACCESS_MASK, PVOID, HANDLE,
    ULONG, HANDLE, HANDLE, HANDLE, ULONG);
typedef NTSTATUS (NTAPI *fn_CreateThreadEx)(PHANDLE, ACCESS_MASK, PVOID, HANDLE,
    PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
typedef NTSTATUS (NTAPI *fn_CreateSection)(PHANDLE, ACCESS_MASK, PVOID,
    PLARGE_INTEGER, ULONG, ULONG, HANDLE);
typedef NTSTATUS (NTAPI *fn_MapViewOfSection)(HANDLE, HANDLE, PVOID *, ULONG_PTR,
    SIZE_T, PLARGE_INTEGER, PSIZE_T, ULONG, ULONG, ULONG);
typedef NTSTATUS (NTAPI *fn_ReadVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
typedef NTSTATUS (NTAPI *fn_WriteVM)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
typedef NTSTATUS (NTAPI *fn_DuplicateObject)(HANDLE, HANDLE, HANDLE, PHANDLE,
    ACCESS_MASK, ULONG, ULONG);
typedef NTSTATUS (NTAPI *fn_SetEvent)(HANDLE, PLONG);
typedef NTSTATUS (NTAPI *fn_ResetEvent)(HANDLE, PLONG);
typedef NTSTATUS (NTAPI *fn_WaitSingle)(HANDLE, BOOLEAN, PLARGE_INTEGER);
typedef NTSTATUS (NTAPI *fn_TerminateProcess)(HANDLE, NTSTATUS);
typedef NTSTATUS (NTAPI *fn_TerminateThread)(HANDLE, NTSTATUS);
typedef NTSTATUS (NTAPI *fn_Close)(HANDLE);
typedef NTSTATUS (NTAPI *fn_QueryProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI *fn_RtlCreateUserThread)(HANDLE, PVOID, BOOLEAN,
    ULONG, SIZE_T, SIZE_T, PVOID, PVOID, PHANDLE, PVOID);

static fn_CreateProcessEx   p_NtCreateProcessEx;
static fn_CreateThreadEx    p_NtCreateThreadEx;
static fn_CreateSection     p_NtCreateSection;
static fn_MapViewOfSection  p_NtMapViewOfSection;
static fn_ReadVM            p_NtReadVirtualMemory;
static fn_WriteVM           p_NtWriteVirtualMemory;
static fn_DuplicateObject   p_NtDuplicateObject;
static fn_SetEvent          p_NtSetEvent;
static fn_ResetEvent        p_NtResetEvent;
static fn_WaitSingle        p_NtWaitForSingleObject;
static fn_TerminateProcess  p_NtTerminateProcess;
static fn_TerminateThread   p_NtTerminateThread;
static fn_Close             p_NtClose;
static fn_QueryProcess      p_NtQueryInformationProcess;
static fn_RtlCreateUserThread p_RtlCreateUserThread;

static int verbose, terse;

static void say(const char *fmt, ...)
{
	va_list ap;
	if (!verbose)
		return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fflush(stderr);
}

/* Patterns. Distinct constants so a mix-up shows up as the wrong word rather
 * than as a plausible one. */
#define PRIV_BEFORE   0x5041545445524E31ULL   /* "PATTERN1" */
#define PRIV_AFTER    0x504F5354434C4F4EULL   /* "POSTCLON" */
#define VIEW_BEFORE   0x53484152454431ULL
#define VIEW_P2C      0x50325F435F5052ULL
#define VIEW_C2P      0x43325F505F5052ULL
#define CHILD_RAN     0x4348494C4452414EULL   /* "CHILDRAN" */

/* Shared-view word indices, and the private page's. */
enum { W_PRE = 0, W_P2C = 1, W_C2P = 2, W_CHILD_SEES = 3, W_ARG = 4, W_RAN = 5 };

/* Set before the first clone, so the clone carries them. The child worker
 * reads them out of its own copy of this image's data. */
static volatile uint64_t *g_view;
static volatile uint64_t *g_priv;
static HANDLE g_child_event;
static uint64_t g_worker_arg = 0x574F524B45524147ULL;   /* "WORKERAG" */

/* Runs in the clone, on a thread created after the clone. It touches nothing
 * outside ntdll: no CRT, no kernel32, no heap, because a cloned process has no
 * csrss registration and this routine is the one place that would find out the
 * hard way. Everything it needs is a pointer the clone inherited. */
static DWORD WINAPI child_worker(LPVOID arg)
{
	volatile uint64_t *v = g_view;

	v[W_CHILD_SEES] = g_priv[0];
	v[W_ARG] = (uint64_t)(uintptr_t) arg;
	v[W_RAN] = CHILD_RAN;
	p_NtSetEvent(g_child_event, NULL);
	p_NtTerminateThread(NT_CURRENT_THREAD, 0);
	return 0;
}

/* -2 means the question could not run, and prints as na. Every real answer is
 * 0 or 1. */
static struct {
	int q1_clone_created;
	NTSTATUS q1_status;
	int q2_address_space_cloned;
	int q2_post_clone_write_isolated;
	int q3_inherited_handles;
	int q3_noninherited_absent;
	int q4_shared_section_view;
	int q4_parent_to_child;
	int q4_child_to_parent;
	int q5_thread_created;
	int q5_thread_after_clone;
	int q5_child_saw_private;
	int q5_wait;
	int q5_control_self_thread;
	int q5_rtl_ran;
	NTSTATUS q5_rtl_status;
	NTSTATUS q5_clone_exit_status;
	NTSTATUS q5_plain_status;
	NTSTATUS q5_status;
	ULONG q5_flags;
	int q5_attempts;
	int q6_parent_cfg, q6_child_cfg;
	int q6_parent_cet, q6_child_cet;
	double q7_clone_median_us;
	int q7_iterations, q7_failures;
} r = {
	.q1_clone_created = -2,
	.q2_address_space_cloned = -2, .q2_post_clone_write_isolated = -2,
	.q3_inherited_handles = -2, .q3_noninherited_absent = -2,
	.q4_shared_section_view = -2, .q4_parent_to_child = -2,
	.q4_child_to_parent = -2,
	.q5_thread_created = -2, .q5_thread_after_clone = -2,
	.q5_child_saw_private = -2, .q5_wait = -2,
	.q5_control_self_thread = -2, .q5_rtl_ran = -2,
	.q5_rtl_status = (NTSTATUS) 0xFFFFFFFFL,
	.q5_clone_exit_status = (NTSTATUS) 0xFFFFFFFFL,
	.q5_plain_status = (NTSTATUS) 0xFFFFFFFFL,
	.q6_parent_cfg = -2, .q6_child_cfg = -2,
	.q6_parent_cet = -2, .q6_child_cet = -2,
};

static void p(const char *key, int val)
{
	if (val == -2)
		printf("%s=na\n", key);
	else
		printf("%s=%d\n", key, val);
}

static void pflag(const char *key, int val)
{
	printf("%s=%s\n", key, val == 1 ? "on" : val == 0 ? "off" : "unavailable");
}

/* One clone. Returns the child handle or NULL, and always records a status. */
static HANDLE clone_once(NTSTATUS *st)
{
	HANDLE child = NULL;
	NTSTATUS s = p_NtCreateProcessEx(&child, PROCESS_ALL_ACCESS, NULL,
	                                 NT_CURRENT_PROCESS,
	                                 PROCESS_CREATE_FLAGS_INHERIT_HANDLES,
	                                 NULL /* SectionHandle */, NULL, NULL, 0);
	if (st)
		*st = s;
	return NT_OK(s) ? child : NULL;
}

static void kill_clone(HANDLE child)
{
	if (!child)
		return;
	p_NtTerminateProcess(child, 0);
	p_NtClose(child);
}

/* Read one 64-bit word out of another process. Returns 0 on a failed read and
 * sets *ok, so a caller can tell a zero word from an unreadable one. */
static uint64_t peek(HANDLE proc, const volatile void *addr, int *ok)
{
	uint64_t out = 0;
	SIZE_T got = 0;
	NTSTATUS s = p_NtReadVirtualMemory(proc, (PVOID)(uintptr_t) addr,
	                                   &out, sizeof out, &got);
	*ok = (NT_OK(s) && got == sizeof out);
	return out;
}

static int poke(HANDLE proc, volatile void *addr, uint64_t val)
{
	SIZE_T put = 0;
	NTSTATUS s = p_NtWriteVirtualMemory(proc, (PVOID)(uintptr_t) addr,
	                                    &val, sizeof val, &put);
	return NT_OK(s) && put == sizeof val;
}

static HANDLE h_section;
static HANDLE h_inherit_event;      /* marked inheritable, used for q3 */
static HANDLE h_private_event;      /* not inheritable, the q3 control */
static SIZE_T view_size = 0x10000;

/* Everything the clone has to carry has to exist before the clone. */
static int setup(void)
{
	SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
	LARGE_INTEGER max;
	PVOID base = NULL;
	SIZE_T sz = view_size;
	NTSTATUS s;
	OBJECT_ATTRIBUTES oa;

	g_priv = VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!g_priv) {
		say("setup: private commit failed\n");
		return 0;
	}
	g_priv[0] = PRIV_BEFORE;

	max.QuadPart = (LONGLONG) view_size;
	memset(&oa, 0, sizeof oa);
	oa.Length = sizeof oa;
	oa.Attributes = OBJ_INHERIT;
	s = p_NtCreateSection(&h_section, SECTION_ALL_ACCESS, &oa, &max,
	                      PAGE_READWRITE, SEC_COMMIT, NULL);
	if (!NT_OK(s)) {
		say("setup: NtCreateSection 0x%08lx\n", (unsigned long) s);
		return 0;
	}
	s = p_NtMapViewOfSection(h_section, NT_CURRENT_PROCESS, &base, 0, 0, NULL,
	                         &sz, VIEW_SHARE, 0, PAGE_READWRITE);
	if (!NT_OK(s)) {
		say("setup: NtMapViewOfSection 0x%08lx\n", (unsigned long) s);
		return 0;
	}
	g_view = (volatile uint64_t *) base;
	g_view[W_PRE] = VIEW_BEFORE;

	h_inherit_event = CreateEventW(&sa, TRUE, FALSE, NULL);
	g_child_event = CreateEventW(&sa, TRUE, FALSE, NULL);
	h_private_event = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!h_inherit_event || !g_child_event || !h_private_event) {
		say("setup: event creation failed\n");
		return 0;
	}
	say("setup: private %p view %p section %p ev %p/%p\n",
	    (void *) g_priv, (void *) g_view, h_section,
	    h_inherit_event, g_child_event);
	return 1;
}

/* q2. Private committed data written before the clone, read back out of the
 * child at the same address. Method: NtReadVirtualMemory from the parent, so
 * the answer does not depend on q5. The control is the second word, written
 * after the clone: a real clone must not show it. */
static void q2_private_data(HANDLE child)
{
	int ok1 = 0, ok2 = 0;
	uint64_t before, after;

	before = peek(child, &g_priv[0], &ok1);
	r.q2_address_space_cloned = (ok1 && before == PRIV_BEFORE);

	g_priv[1] = PRIV_AFTER;
	after = peek(child, &g_priv[1], &ok2);
	r.q2_post_clone_write_isolated = (ok2 && after != PRIV_AFTER);
	say("  q2: child@%p reads 0x%llx (%s); post-clone word reads 0x%llx\n",
	    (void *) &g_priv[0], (unsigned long long) before,
	    ok1 ? "read ok" : "unreadable", (unsigned long long) after);
}

/* Wait on a handle for at most ms milliseconds. 1 signalled, 0 timed out,
 * -1 the wait itself failed. */
static int wait_ms(HANDLE h, unsigned ms)
{
	LARGE_INTEGER t;
	NTSTATUS s;
	t.QuadPart = -((LONGLONG) ms * 10000LL);
	s = p_NtWaitForSingleObject(h, FALSE, &t);
	if (s == 0)
		return 1;
	if (s == (NTSTATUS) 0x00000102L)
		return 0;
	return -1;
}

/* q3. Is the inheritable handle valid in the child at the same numeric value?
 * Duplicating it back out of the child answers half; signalling the duplicate
 * and watching the parent's own handle go signalled answers the other half,
 * which is that the two values name one object rather than two. The control is
 * the handle that was not marked inheritable. */
static void q3_handles(HANDLE child)
{
	HANDLE back = NULL, stray = NULL;
	NTSTATUS s;

	s = p_NtDuplicateObject(child, h_inherit_event, NT_CURRENT_PROCESS, &back,
	                        0, 0, DUPLICATE_SAME_ACCESS);
	if (!NT_OK(s)) {
		r.q3_inherited_handles = 0;
		say("  q3: duplicate back from the child 0x%08lx\n", (unsigned long) s);
	} else {
		p_NtSetEvent(back, NULL);
		r.q3_inherited_handles = (wait_ms(h_inherit_event, 0) == 1);
		ResetEvent(h_inherit_event);
		p_NtClose(back);
		say("  q3: handle %p duplicated back, same object: %s\n",
		    h_inherit_event, r.q3_inherited_handles ? "yes" : "no");
	}

	s = p_NtDuplicateObject(child, h_private_event, NT_CURRENT_PROCESS, &stray,
	                        0, 0, DUPLICATE_SAME_ACCESS);
	r.q3_noninherited_absent = !NT_OK(s);
	if (NT_OK(s))
		p_NtClose(stray);
	say("  q3: the non-inheritable handle %s in the child\n",
	    r.q3_noninherited_absent ? "is absent" : "IS PRESENT");
}

/* q4. Is the ViewShare section view present in the child at the same address,
 * and is it the same memory? Two writes settle it: one from the parent into
 * its own view, read back out of the child; one written into the child's view
 * from outside, read back out of the parent's. Private pages would fail both,
 * since a write on either side of a cloned private page splits it. */
static void q4_shared_view(HANDLE child)
{
	int ok = 0;
	uint64_t pre, seen;

	pre = peek(child, &g_view[W_PRE], &ok);
	r.q4_shared_section_view = (ok && pre == VIEW_BEFORE);

	g_view[W_P2C] = VIEW_P2C;
	seen = peek(child, &g_view[W_P2C], &ok);
	r.q4_parent_to_child = (ok && seen == VIEW_P2C);

	if (poke(child, &g_view[W_C2P], VIEW_C2P))
		r.q4_child_to_parent = (g_view[W_C2P] == VIEW_C2P);
	else
		r.q4_child_to_parent = 0;

	say("  q4: view@%p present %d, parent->child %d, child->parent %d\n",
	    (void *) g_view, r.q4_shared_section_view, r.q4_parent_to_child,
	    r.q4_child_to_parent);
}

/* q5. A thread created in the clone after the fact. This is the part that
 * historically breaks: the clone was never registered with csrss, and the
 * first thread to run in a process is the one that finds out. Each variant
 * gets its own fresh clone so a hang in one cannot be read as a result for
 * the next; plain flags first, then with the DLL thread-attach callbacks
 * skipped, which is the difference between asking the loader to do work in an
 * unregistered process and asking it not to. The third and fourth carry
 * THREAD_CREATE_FLAGS_INITIAL_THREAD, which is what the executive wants for
 * the first thread a process ever has, and a clone has never had one. */
static const ULONG q5_variants[] = {
	0x00000000UL, 0x00000002UL, 0x00000080UL, 0x00000082UL
};

/* Wipe the shared slots the worker writes, so a stale word from an earlier
 * attempt cannot be read as this attempt's. */
static void q5_clear(void)
{
	g_view[W_RAN] = 0;
	g_view[W_CHILD_SEES] = 0;
	g_view[W_ARG] = 0;
	ResetEvent(g_child_event);
}

/* The control. The same call, the same worker, the same arguments, against a
 * process that was created the ordinary way: this one. A refusal in the clone
 * means something about the clone only if this succeeds. */
static void q5_control(unsigned wait_msec)
{
	HANDLE th = NULL;
	NTSTATUS s;
	int w;

	q5_clear();
	s = p_NtCreateThreadEx(&th, THREAD_ALL_ACCESS, NULL, NT_CURRENT_PROCESS,
	                       (PVOID) child_worker, (PVOID)(uintptr_t) g_worker_arg,
	                       0, 0, 0, 0, NULL);
	if (!NT_OK(s)) {
		r.q5_control_self_thread = 0;
		say("  q5 control: NtCreateThreadEx on self 0x%08lx\n",
		    (unsigned long) s);
		return;
	}
	w = wait_ms(g_child_event, wait_msec);
	r.q5_control_self_thread = (w == 1 && g_view[W_RAN] == CHILD_RAN);
	say("  q5 control: a thread in this process %s\n",
	    r.q5_control_self_thread ? "ran" : "did not run");
	p_NtClose(th);
}

/* Is the clone alive at the moment the thread is asked for? ExitStatus reads
 * back as STATUS_PENDING while a process is still running. */
static NTSTATUS clone_exit_status(HANDLE child)
{
	PROCESS_BASIC_INFORMATION pbi;
	ULONG len = 0;
	NTSTATUS s;
	if (!p_NtQueryInformationProcess)
		return (NTSTATUS) 0xFFFFFFFFL;
	memset(&pbi, 0, sizeof pbi);
	s = p_NtQueryInformationProcess(child, 0, &pbi, sizeof pbi, &len);
	if (!NT_OK(s))
		return s;
	return (NTSTATUS) pbi.ExitStatus;
}

/* RtlCreateUserThread, the other door onto the same executive service, tried
 * on its own fresh clone because the assignment names it and because a wrapper
 * that fills in what the raw call leaves out would show up here. */
static void q5_rtl_attempt(unsigned wait_msec)
{
	NTSTATUS cs = 0, s;
	HANDLE child, th = NULL;
	int w;

	if (!p_RtlCreateUserThread)
		return;
	q5_clear();
	child = clone_once(&cs);
	if (!child)
		return;
	s = p_RtlCreateUserThread(child, NULL, FALSE, 0, 0, 0,
	                          (PVOID) child_worker,
	                          (PVOID)(uintptr_t) g_worker_arg, &th, NULL);
	r.q5_rtl_status = s;
	if (NT_OK(s)) {
		w = wait_ms(g_child_event, wait_msec);
		r.q5_rtl_ran = (w == 1 && g_view[W_RAN] == CHILD_RAN);
		p_NtClose(th);
	} else {
		r.q5_rtl_ran = 0;
	}
	say("  q5: RtlCreateUserThread 0x%08lx, ran %d\n", (unsigned long) s,
	    r.q5_rtl_ran);
	kill_clone(child);
}

static void q5_thread_in_clone(unsigned wait_msec)
{
	size_t i;

	q5_control(wait_msec);

	for (i = 0; i < sizeof q5_variants / sizeof q5_variants[0]; i++) {
		NTSTATUS cs = 0, s;
		HANDLE child, th = NULL;
		int w;

		q5_clear();

		child = clone_once(&cs);
		if (!child) {
			say("  q5: clone for the thread test failed 0x%08lx\n",
			    (unsigned long) cs);
			r.q5_status = cs;
			return;
		}
		r.q5_attempts++;
		r.q5_flags = q5_variants[i];
		if (i == 0)
			r.q5_clone_exit_status = clone_exit_status(child);

		s = p_NtCreateThreadEx(&th, THREAD_ALL_ACCESS, NULL, child,
		                       (PVOID) child_worker,
		                       (PVOID)(uintptr_t) g_worker_arg,
		                       q5_variants[i], 0, 0, 0, NULL);
		r.q5_status = s;
		if (i == 0)
			r.q5_plain_status = s;
		r.q5_thread_created = NT_OK(s);
		if (!NT_OK(s)) {
			say("  q5: flags 0x%lx, NtCreateThreadEx 0x%08lx\n",
			    (unsigned long) q5_variants[i], (unsigned long) s);
			r.q5_thread_after_clone = 0;
			r.q5_wait = -2;
			kill_clone(child);
			continue;
		}

		w = wait_ms(g_child_event, wait_msec);
		r.q5_wait = w;
		r.q5_thread_after_clone = (w == 1 && g_view[W_RAN] == CHILD_RAN &&
		                           g_view[W_ARG] == g_worker_arg);
		r.q5_child_saw_private = (g_view[W_CHILD_SEES] == PRIV_BEFORE);
		say("  q5: flags 0x%lx, thread created, wait %s, ran %d\n",
		    (unsigned long) q5_variants[i],
		    w == 1 ? "signalled" : w == 0 ? "timed out" : "failed",
		    r.q5_thread_after_clone);
		p_NtClose(th);
		kill_clone(child);
		if (r.q5_thread_after_clone == 1)
			return;
	}
	q5_rtl_attempt(wait_msec);
}

/* q6. What the build actually enforces on the two processes, asked from the
 * parent about both. Proposal 0011's open question names Control Flow Guard
 * and user-mode shadow stacks by name, so both are read rather than assumed. */
typedef BOOL (WINAPI *fn_GetMit)(HANDLE, int, PVOID, SIZE_T);
static fn_GetMit p_GetProcessMitigationPolicy;

#define MIT_CFG  7
#define MIT_CET 15

static int mitigation_bit0(HANDLE proc, int policy)
{
	DWORD flags = 0;
	if (!p_GetProcessMitigationPolicy)
		return -2;
	if (!p_GetProcessMitigationPolicy(proc, policy, &flags, sizeof flags))
		return -2;
	return (flags & 1u) ? 1 : 0;
}

static void q6_mitigations(HANDLE child)
{
	HANDLE me = GetCurrentProcess();
	r.q6_parent_cfg = mitigation_bit0(me, MIT_CFG);
	r.q6_parent_cet = mitigation_bit0(me, MIT_CET);
	r.q6_child_cfg = child ? mitigation_bit0(child, MIT_CFG) : -2;
	r.q6_child_cet = child ? mitigation_bit0(child, MIT_CET) : -2;
	say("  q6: parent cfg %d cet %d; child cfg %d cet %d\n",
	    r.q6_parent_cfg, r.q6_parent_cet, r.q6_child_cfg, r.q6_child_cet);
}

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *) a, y = *(const double *) b;
	return x < y ? -1 : x > y ? 1 : 0;
}

/* q7. Time per clone, median over n. The call alone is timed; tearing the
 * clone down again is real work but it is not the primitive. A measurement,
 * not a finding: nothing above depends on it. */
static void q7_time_clones(int n)
{
	LARGE_INTEGER freq, t0, t1;
	double *us;
	int i, kept = 0;

	QueryPerformanceFrequency(&freq);
	us = malloc((size_t) n * sizeof *us);
	if (!us)
		return;
	for (i = 0; i < n; i++) {
		NTSTATUS s = 0;
		HANDLE child;
		QueryPerformanceCounter(&t0);
		child = clone_once(&s);
		QueryPerformanceCounter(&t1);
		if (!child) {
			r.q7_failures++;
			continue;
		}
		us[kept++] = (double)(t1.QuadPart - t0.QuadPart) * 1e6 /
		             (double) freq.QuadPart;
		kill_clone(child);
	}
	r.q7_iterations = n;
	if (kept > 0) {
		qsort(us, (size_t) kept, sizeof *us, cmp_double);
		r.q7_clone_median_us = us[kept / 2];
	}
	free(us);
	say("  q7: %d clones, %d failed, median %.1f us\n", n, r.q7_failures,
	    r.q7_clone_median_us);
}

static int resolve(void)
{
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
	if (!nt)
		return 0;
	p_NtCreateProcessEx    = (fn_CreateProcessEx)  (void *) GetProcAddress(nt, "NtCreateProcessEx");
	p_NtCreateThreadEx     = (fn_CreateThreadEx)   (void *) GetProcAddress(nt, "NtCreateThreadEx");
	p_NtCreateSection      = (fn_CreateSection)    (void *) GetProcAddress(nt, "NtCreateSection");
	p_NtMapViewOfSection   = (fn_MapViewOfSection) (void *) GetProcAddress(nt, "NtMapViewOfSection");
	p_NtReadVirtualMemory  = (fn_ReadVM)           (void *) GetProcAddress(nt, "NtReadVirtualMemory");
	p_NtWriteVirtualMemory = (fn_WriteVM)          (void *) GetProcAddress(nt, "NtWriteVirtualMemory");
	p_NtDuplicateObject    = (fn_DuplicateObject)  (void *) GetProcAddress(nt, "NtDuplicateObject");
	p_NtSetEvent           = (fn_SetEvent)         (void *) GetProcAddress(nt, "NtSetEvent");
	p_NtResetEvent         = (fn_ResetEvent)       (void *) GetProcAddress(nt, "NtResetEvent");
	p_NtWaitForSingleObject= (fn_WaitSingle)       (void *) GetProcAddress(nt, "NtWaitForSingleObject");
	p_NtTerminateProcess   = (fn_TerminateProcess) (void *) GetProcAddress(nt, "NtTerminateProcess");
	p_NtTerminateThread    = (fn_TerminateThread)  (void *) GetProcAddress(nt, "NtTerminateThread");
	p_NtClose              = (fn_Close)            (void *) GetProcAddress(nt, "NtClose");
	p_NtQueryInformationProcess = (fn_QueryProcess) (void *)
		GetProcAddress(nt, "NtQueryInformationProcess");
	p_RtlCreateUserThread  = (fn_RtlCreateUserThread) (void *)
		GetProcAddress(nt, "RtlCreateUserThread");
	if (k32)
		p_GetProcessMitigationPolicy = (fn_GetMit) (void *)
			GetProcAddress(k32, "GetProcessMitigationPolicy");

	return p_NtCreateProcessEx && p_NtCreateThreadEx && p_NtCreateSection &&
	       p_NtMapViewOfSection && p_NtReadVirtualMemory &&
	       p_NtWriteVirtualMemory && p_NtDuplicateObject && p_NtSetEvent &&
	       p_NtWaitForSingleObject && p_NtTerminateProcess &&
	       p_NtTerminateThread && p_NtClose;
}

static void usage(FILE *f)
{
	fprintf(f,
	    "Usage:\n  clone-probe [options]\n\n"
	    "Options:\n"
	    "  -n N, --iterations=N  Clones to time for q7. [default: 200]\n"
	    "  -v, --verbose         Narrate each question as it runs.\n"
	    "  -t, --terse           The key=value block alone.\n"
	    "  -V, --version         Print the version and exit.\n"
	    "  -h, --help            Print this message and exit.\n");
}

int main(int argc, char **argv)
{
	int i, iterations = 200;
	NTSTATUS st = 0;
	HANDLE child;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("%s\n", RELEASE);
			return 0;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			verbose = 1;
		} else if (!strcmp(a, "-t") || !strcmp(a, "--terse")) {
			terse = 1;
		} else if (!strcmp(a, "-n") && i + 1 < argc) {
			iterations = atoi(argv[++i]);
		} else if (!strncmp(a, "--iterations=", 13)) {
			iterations = atoi(a + 13);
		} else {
			fprintf(stderr, "clone-probe: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
	}
	if (iterations < 1)
		iterations = 1;

	if (!resolve()) {
		fprintf(stderr, "clone-probe: ntdll did not yield the entry points\n");
		return 1;
	}
	if (!setup()) {
		fprintf(stderr, "clone-probe: the pre-clone state could not be built\n");
		return 1;
	}

	if (!terse) {
		printf("NtCreateProcessEx with a null section, as a fork primitive\n\n");
		fflush(stdout);
	}

	/* q1, and with it the clone every question below q5 reads. */
	child = clone_once(&st);
	r.q1_status = st;
	r.q1_clone_created = (child != NULL);
	say("  q1: NtCreateProcessEx 0x%08lx, child %p\n", (unsigned long) st,
	    (void *) child);

	if (child) {
		q2_private_data(child);
		q3_handles(child);
		q4_shared_view(child);
	}
	q6_mitigations(child);
	kill_clone(child);

	if (r.q1_clone_created == 1) {
		q5_thread_in_clone(5000);
		q7_time_clones(iterations);
	}

	if (!terse)
		printf("\n");
	printf("view_size=0x%llx\n", (unsigned long long) view_size);
	p("q1_clone_created", r.q1_clone_created);
	printf("q1_status=0x%08lx\n", (unsigned long) r.q1_status);
	p("q2_address_space_cloned", r.q2_address_space_cloned);
	printf("q2_method=NtReadVirtualMemory-from-parent\n");
	p("q2_post_clone_write_isolated", r.q2_post_clone_write_isolated);
	p("q3_inherited_handles", r.q3_inherited_handles);
	p("q3_noninherited_absent", r.q3_noninherited_absent);
	p("q4_shared_section_view", r.q4_shared_section_view);
	p("q4_parent_to_child", r.q4_parent_to_child);
	p("q4_child_to_parent", r.q4_child_to_parent);
	p("q5_thread_created", r.q5_thread_created);
	p("q5_thread_after_clone", r.q5_thread_after_clone);
	p("q5_child_saw_private", r.q5_child_saw_private);
	printf("q5_plain_status=0x%08lx\n", (unsigned long) r.q5_plain_status);
	printf("q5_status=0x%08lx\n", (unsigned long) r.q5_status);
	printf("q5_flags=0x%08lx\n", (unsigned long) r.q5_flags);
	printf("q5_wait=%s\n", r.q5_wait == 1 ? "signalled" :
	                       r.q5_wait == 0 ? "timeout" :
	                       r.q5_wait == -1 ? "wait-failed" : "na");
	printf("q5_attempts=%d\n", r.q5_attempts);
	p("q5_control_self_thread", r.q5_control_self_thread);
	printf("q5_clone_exit_status=0x%08lx\n",
	       (unsigned long) r.q5_clone_exit_status);
	p("q5_rtl_ran", r.q5_rtl_ran);
	printf("q5_rtl_status=0x%08lx\n", (unsigned long) r.q5_rtl_status);
	pflag("q6_parent_cfg", r.q6_parent_cfg);
	pflag("q6_child_cfg", r.q6_child_cfg);
	pflag("q6_parent_cet", r.q6_parent_cet);
	pflag("q6_child_cet", r.q6_child_cet);
	printf("q7_clone_iterations=%d\n", r.q7_iterations);
	printf("q7_clone_failures=%d\n", r.q7_failures);
	printf("q7_clone_median_us=%.1f\n", r.q7_clone_median_us);
	return 0;
}
