/* WP-41's first measurement: how early is early enough?
 *
 * Spike 2 established that a reservation at 0x400000 is refused once a Cygwin
 * process is running, and left open where the reservation therefore has to go.
 * The plan named two candidates -- a PE TLS callback and the image entry point
 * -- and said whether either is early enough was unmeasured. This is the child
 * side of that measurement. It is built several times from one source,
 * differing only in where it arms the window:
 *
 *   late    arm from main, after the CRT has run, which is the arrangement
 *           spike 2 measured; the control rather than a candidate
 *   tls     arm from a callback in .CRT$XLB, which the loader runs on process
 *           attach, before the image entry point
 *   entry   arm from a replacement image entry point, which reserves and then
 *           tail-calls the entry the toolchain would have used
 *   none    do not arm at all, and report what the parent left behind; this is
 *           the fourth route, where the reservation is made from outside by a
 *           parent holding a suspended child
 *
 * Every build reports the same key=value block, so the transcripts are
 * comparable line by line. The two numbers that turned out to decide the
 * question are when_stack_base and when_stack_limit: the initial thread's
 * stack is placed by the kernel before any instruction of the image runs, and
 * where it lands is not something a hook inside the image can affect.
 *
 * Nothing formats or allocates in the early hooks. A Cygwin process has no
 * working stdio until dll_crt0 has run, and an early printf hangs rather than
 * printing, so the hooks record raw values into statics and main prints them.
 *
 * Usage:
 *   when [options]
 *
 * Options:
 *   -q, --quiet   the key=value block alone
 *   -m, --map     also print the low address space as the hook saw it
 *   -h, --help    print this message and exit
 *
 * Exit: 0 the window stood at the base when main ran, 1 it did not, 2 usage.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../reserve.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#ifndef ARM_ROUTE
#define ARM_ROUTE "late"
#endif

/* Everything the early hook may record. Set by whichever hook ran, so a build
 * whose hook the toolchain silently dropped is distinguishable from one whose
 * hook ran and was refused -- a TLS directory that never got emitted would
 * otherwise read as a reservation that was made too late. */
static volatile LONG hook_ran;
static volatile LONG hook_reserved;
static uint64_t stack_base, stack_limit;
static int span_4m, span_full;
static MEMORY_BASIC_INFORMATION regs[96];
static unsigned nreg;

static void probe_span(void)
{
	void *p;

	p = VirtualAlloc((void *) (UINT_PTR) ELF_WINDOW_BASE, 0x400000,
			 MEM_RESERVE, PAGE_NOACCESS);
	span_4m = p != NULL;
	if (p)
		VirtualFree(p, 0, MEM_RELEASE);
	p = VirtualAlloc((void *) (UINT_PTR) ELF_WINDOW_BASE,
			 (SIZE_T) ELF_WINDOW_SIZE, MEM_RESERVE, PAGE_NOACCESS);
	span_full = p != NULL;
	if (p)
		VirtualFree(p, 0, MEM_RELEASE);
}

/* Taken before anything else the hook does, and held raw. Surveying after an
 * attempt would report the diagnostic's own allocations as the thing that
 * filled the hole. */
static void survey(void)
{
	MEMORY_BASIC_INFORMATION m;
	uint64_t at = 0x10000;

	while (at < 0x80000000ULL && nreg < 96 &&
	       VirtualQuery((void *) (UINT_PTR) at, &m, sizeof m)) {
		regs[nreg++] = m;
		if (!m.RegionSize)
			break;
		at = (uint64_t) (UINT_PTR) m.BaseAddress + m.RegionSize;
	}
}

static void arm_once(void)
{
	NT_TIB *tib = (NT_TIB *) NtCurrentTeb();

	stack_base = (uint64_t) (UINT_PTR) tib->StackBase;
	stack_limit = (uint64_t) (UINT_PTR) tib->StackLimit;
	survey();
	probe_span();
#if !defined(ARM_NONE)
	elf_window_arm();
#endif
	hook_ran = 1;
	hook_reserved = elf_window_low()->held;
}

#if defined(ARM_TLS)
static void NTAPI tls_hook(PVOID base, DWORD reason, PVOID reserved)
{
	(void) base; (void) reserved;
	if (reason == DLL_PROCESS_ATTACH)
		arm_once();
}

/* The TLS directory is only emitted if something references _tls_used, and the
 * callback is only found if it sits in .CRT$XLB, between the markers the
 * linker sorts around it. Cygwin's runtime supplies neither; this build
 * therefore fails to link on this toolchain, which is itself the answer for
 * this route and is what the driver records. */
extern char _tls_used;
static const void *const tls_used_ref __attribute__((used)) = &_tls_used;

__attribute__((section(".CRT$XLB"), used))
PIMAGE_TLS_CALLBACK elf_tls_hook = tls_hook;
#endif

#if defined(ARM_ENTRY)
extern void mainCRTStartup(void);

/* The replacement entry point: as early as anything in the image can run. */
void elf_stub_entry(void)
{
	arm_once();
	mainCRTStartup();
}
#endif

static const char *state_name(DWORD state)
{
	return state == MEM_FREE ? "free" :
	       state == MEM_RESERVE ? "reserve" : "commit";
}

static const char *type_name(const MEMORY_BASIC_INFORMATION *m)
{
	if (m->State == MEM_FREE)
		return "-";
	return m->Type == MEM_IMAGE ? "image" :
	       m->Type == MEM_MAPPED ? "mapped" : "private";
}

int main(int argc, char **argv)
{
	elf_window *w = elf_window_low();
	int quiet = 0, map = 0, held, i;
	unsigned k;
	const MEMORY_BASIC_INFORMATION *at_base = NULL;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-q") || !strcmp(argv[i], "--quiet"))
			quiet = 1;
		else if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--map"))
			map = 1;
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("Usage:\n  when [options]\n\n"
			       "Options:\n"
			       "  -q, --quiet   the key=value block alone\n"
			       "  -m, --map     also print the low address space\n"
			       "  -h, --help    print this message and exit\n");
			return 0;
		} else {
			fprintf(stderr, "when: unknown option %s\n", argv[i]);
			return 2;
		}
	}

	/* The control route arms here, after the CRT and after everything main
	 * has already done. The others armed long ago and this is a no-op. */
	if (!hook_ran)
		arm_once();

	for (k = 0; k < nreg; k++)
		if ((uint64_t) (UINT_PTR) regs[k].BaseAddress <= ELF_WINDOW_BASE &&
		    ELF_WINDOW_BASE < (uint64_t) (UINT_PTR) regs[k].BaseAddress +
				      regs[k].RegionSize)
			at_base = &regs[k];

	held = w->held;
#if defined(ARM_NONE)
	/* The parent route: the window is not ours to have armed. What decides
	 * the case is whether a reservation stands at the base and covers the
	 * whole window, put there from outside before this process ran. */
	held = at_base && at_base->State == MEM_RESERVE &&
	       (uint64_t) at_base->RegionSize >= ELF_WINDOW_SIZE;
#endif

	if (!quiet)
		printf("\n== arming the low window from the %s route\n\n", ARM_ROUTE);

	printf("when_route=%s\n", ARM_ROUTE);
	printf("when_hook_ran=%ld\n", (long) hook_ran);
	printf("when_hook_reserved=%ld\n", (long) hook_reserved);
	printf("when_base=0x%" PRIx64 "\n", (uint64_t) ELF_WINDOW_BASE);
	printf("when_size=0x%" PRIx64 "\n", (uint64_t) ELF_WINDOW_SIZE);
	printf("when_got_base=0x%" PRIx64 "\n", w->base);
	printf("when_got_size=0x%" PRIx64 "\n", w->size);
	printf("when_span_4m=%d\n", span_4m);
	printf("when_span_full=%d\n", span_full);
	printf("when_stack_base=0x%" PRIx64 "\n", stack_base);
	printf("when_stack_limit=0x%" PRIx64 "\n", stack_limit);
	printf("when_stack_at_window=%d\n",
	       (stack_base > ELF_WINDOW_BASE &&
		stack_limit < ELF_WINDOW_BASE + ELF_WINDOW_SIZE) ? 1 : 0);
	if (at_base)
		printf("when_at_base=%s %s 0x%" PRIx64 " for 0x%" PRIx64 "\n",
		       state_name(at_base->State), type_name(at_base),
		       (uint64_t) (UINT_PTR) at_base->BaseAddress,
		       (uint64_t) at_base->RegionSize);
	else
		printf("when_at_base=unsurveyed\n");
	printf("when_module_base=0x%" PRIx64 "\n",
	       (uint64_t) (UINT_PTR) GetModuleHandle(NULL));
	printf("when_result=%s\n", held ? "reserved" : "refused");

	if (map && !quiet) {
		printf("\n== the low address space as the hook saw it\n\n");
		for (k = 0; k < nreg; k++) {
			char name[MAX_PATH];
			name[0] = '\0';
			if (regs[k].State != MEM_FREE && regs[k].Type == MEM_IMAGE)
				GetModuleFileNameA((HMODULE) regs[k].AllocationBase,
						   name, sizeof name);
			printf("    0x%012" PRIx64 " 0x%010" PRIx64 " %-8s %-8s %s\n",
			       (uint64_t) (UINT_PTR) regs[k].BaseAddress,
			       (uint64_t) regs[k].RegionSize,
			       state_name(regs[k].State),
			       type_name(&regs[k]), name);
		}
	}

	elf_window_release(w);
	return held ? 0 : 1;
}
