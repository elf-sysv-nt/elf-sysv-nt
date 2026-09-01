/* reent-stub-realproc-window-reconcile 1.0 -- WP-56 reent-tls-bringup item 1.
 *
 * spike/reent-stub-realproc-window-occupant measured that the raw DR-0028
 * handover -- a single whole-window VirtualAllocEx of ELF_WINDOW at 0x400000
 * into a suspended cygwin-linked child -- is refused (err=487), because the
 * child holds its own private MEM_RESERVE over the low ~2 MB before any user
 * code runs. DR-0068/DR-0069 answered that with a reconciling reservation:
 * elf_window_reserve_in falls, on refusal, to a VirtualQueryEx walk + the pure
 * elf_window_plan + a per-gap reserve, recognizing the child's own low region
 * rather than reserving over it. Those planners are unit-certified in-process
 * (loader/exec/t/unit.c). This spike drives the real elf_window_reserve_in
 * against a real cygwin-linked child -- item 1's "reserve ... through a real
 * cygwin-linked child" verb, the half drivable entirely parent-side -- and
 * measures that the reconcile clears the refusal the occupant spike recorded.
 *
 * Per child, deterministically:
 *   raw_whole_window  the naive whole-window VirtualAllocEx verdict against
 *                     this child at suspend -- ok/refused (the occupant result).
 *   reserve_in        elf_window_reserve_in's verdict -- win_ok when the
 *                     reconcile (or the fast path) reserved the window.
 *   window_covered    after reserve_in, whether every byte of the window is
 *                     MEM_RESERVE with no free hole -- the reconcile's product.
 *
 * Addresses and region sizes are volatile and print as VirtualQuery-style
 * context the t3 runner strips; the reproducible findings are the words.
 *
 * usage: reconcile-probe <stub.exe>
 * exit: 0 measured; 2 spawn failed; 3 usage.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "reserve.h"

static const char *state_word(DWORD s)
{
	switch (s) {
	case MEM_FREE:    return "free";
	case MEM_RESERVE: return "reserved";
	case MEM_COMMIT:  return "committed";
	}
	return "state?";
}

/* Spawn the stub suspended -- the moment dispatch.c attempts the handover,
 * before ResumeThread and any user code. The caller measures against the live
 * child and terminates it. Returns 0 on success. */
static int spawn_suspended(const char *exe, PROCESS_INFORMATION *pi)
{
	char cmd[1024];
	STARTUPINFOA si;
	snprintf(cmd, sizeof cmd, "\"%s\"", exe);
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	memset(pi, 0, sizeof *pi);
	return CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
			      CREATE_SUSPENDED, NULL, NULL, &si, pi) ? 0 : -1;
}

/* Walk the window in the child. Reports whether every byte is reserved with no
 * free hole (the reconcile's product), and classifies -- as a stable word,
 * apart from the volatile addresses -- what non-free content the window holds
 * before its free tail: what elf_window_plan must reconcile. Region rows print
 * as stripped context. */
static int window_covered(HANDLE proc, const char **occ)
{
	uint64_t lo = ELF_WINDOW_BASE, hi = ELF_WINDOW_BASE + ELF_WINDOW_SIZE;
	uint64_t at = lo;
	MEMORY_BASIC_INFORMATION m;
	int hole = 0, any_reserved = 0, any_committed = 0;
	while (at < hi) {
		if (!VirtualQueryEx(proc, (void *)(UINT_PTR) at, &m, sizeof m)) {
			*occ = "query-failed";
			return 0;
		}
		uint64_t rlo = (uint64_t)(UINT_PTR) m.BaseAddress;
		uint64_t rhi = rlo + (uint64_t) m.RegionSize;
		if (rhi > lo && rlo < hi) {
			printf("    region base=0x%llx size=0x%llx %s\n",
			       (unsigned long long) rlo,
			       (unsigned long long) m.RegionSize,
			       state_word(m.State));
			if (m.State == MEM_FREE)      hole = 1;
			if (m.State == MEM_RESERVE)   any_reserved = 1;
			if (m.State == MEM_COMMIT)    any_committed = 1;
		}
		if (m.RegionSize == 0) {
			*occ = "query-failed";
			return 0;
		}
		at = rhi;
	}
	*occ = any_committed && any_reserved ? "reserved+committed" :
	       any_committed                 ? "committed" :
	       any_reserved                  ? "reserved" : "free";
	return !hole;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: reconcile-probe <stub.exe>\n");
		return 3;
	}

	PROCESS_INFORMATION pi;
	if (spawn_suspended(argv[1], &pi) != 0) {
		fprintf(stderr, "spawn=failed err=%lu\n",
			(unsigned long) GetLastError());
		return 2;
	}

	/* The occupant result: the naive whole-window reservation against this
	 * child. On a cygwin-linked child it is refused; freeing it (when it
	 * did succeed, as for the plain-PE control) leaves the fast path to be
	 * retried by elf_window_reserve_in below. On the cygwin child nothing
	 * was taken, so elf_window_reserve_in hits the same refusal and takes
	 * the reconciling fallback -- the path under measurement. */
	void *raw = VirtualAllocEx(pi.hProcess,
				   (void *)(UINT_PTR) ELF_WINDOW_BASE,
				   (SIZE_T) ELF_WINDOW_SIZE,
				   MEM_RESERVE, PAGE_NOACCESS);
	printf("raw_whole_window=%s\n", raw ? "ok" : "refused");
	if (raw)
		VirtualFreeEx(pi.hProcess, raw, 0, MEM_RELEASE);

	elf_window w;
	memset(&w, 0, sizeof w);
	win_err rc = elf_window_reserve_in(pi.hProcess, &w,
					   ELF_WINDOW_BASE, ELF_WINDOW_SIZE);
	printf("reserve_in=%s\n", win_err_name(rc));

	const char *occ = "free";
	int covered = window_covered(pi.hProcess, &occ);
	printf("low_window_occupant=%s\n", occ);
	printf("window_covered=%s\n", covered ? "yes" : "no");

	TerminateProcess(pi.hProcess, 0);
	WaitForSingleObject(pi.hProcess, 2000);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
