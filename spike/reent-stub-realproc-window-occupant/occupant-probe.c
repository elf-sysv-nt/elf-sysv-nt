/* reent-stub-realproc-window-occupant 1.0 -- WP-56 reent-tls-bringup item 1.
 *
 * spike/reent-stub-realproc-faceload found the DR-0028 low-window handover
 * (elf_window_reserve_in: VirtualAllocEx of ELF_WINDOW at base 0x400000 into
 * the suspended child) succeeds for the plain-PE stub but is refused
 * (win_err_refused) for the real-process stub, and read that as "the
 * cygwin-linked child already holds the low region at suspend". That clause was
 * inference. This probe measures it: it spawns a stub CREATE_SUSPENDED -- the
 * moment dispatch.c tries the handover, before ResumeThread and so before any
 * user code or the statically-imported cygwin1.dll is mapped -- walks the
 * child with VirtualQueryEx, and reports, deterministically:
 *
 *   window_free     whether [0x400000, 0x400000+0x3FC00000) is entirely
 *                   MEM_FREE in the child at suspend.
 *   occupant        if not, the first non-free region intersecting the window,
 *                   classified by Type/State (image/mapped/private,
 *                   committed/reserved) -- what the handover collides with.
 *   occupant_span   whether it covers the window base or the whole window.
 *   reserve_in      the DR-0028 handover reproduced against this child: the
 *                   whole-window VirtualAllocEx at 0x400000 -- ok/refused.
 *
 * Addresses and sizes are volatile; the reproducible finding is the
 * classification words. Raw rows print as VirtualQuery-style context the t3
 * runner strips.
 *
 * usage: occupant-probe <stub.exe> [stub-arg]
 * exit: 0 walked and reported; 2 spawn failed; 3 usage.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIN_BASE UINT64_C(0x00400000)
#define WIN_SIZE UINT64_C(0x3FC00000)

static const char *state_word(DWORD s)
{
	switch (s) {
	case MEM_FREE:    return "free";
	case MEM_RESERVE: return "reserved";
	case MEM_COMMIT:  return "committed";
	}
	return "state?";
}

static const char *type_word(DWORD t)
{
	switch (t) {
	case MEM_IMAGE:   return "image";
	case MEM_MAPPED:  return "mapped";
	case MEM_PRIVATE: return "private";
	}
	return "type?";
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: occupant-probe <stub.exe> [arg]\n");
		return 3;
	}

	char cmd[1024];
	if (argc >= 3)
		snprintf(cmd, sizeof cmd, "\"%s\" %s", argv[1], argv[2]);
	else
		snprintf(cmd, sizeof cmd, "\"%s\"", argv[1]);

	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	memset(&pi, 0, sizeof pi);

	if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
			    CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "spawn=failed err=%lu\n",
			(unsigned long) GetLastError());
		return 2;
	}

	uint64_t win_lo = WIN_BASE, win_hi = WIN_BASE + WIN_SIZE;
	uint64_t addr = 0;
	int occupied = 0;
	MEMORY_BASIC_INFORMATION occ;
	memset(&occ, 0, sizeof occ);

	MEMORY_BASIC_INFORMATION m;
	while (addr < win_hi) {
		if (!VirtualQueryEx(pi.hProcess, (void *)(UINT_PTR) addr,
				    &m, sizeof m))
			break;
		uint64_t rlo = (uint64_t)(UINT_PTR) m.BaseAddress;
		uint64_t rhi = rlo + (uint64_t) m.RegionSize;

		if (rhi > win_lo && rlo < win_hi)
			printf("    region base=0x%llx size=0x%llx %s %s\n",
			       (unsigned long long) rlo,
			       (unsigned long long) m.RegionSize,
			       state_word(m.State), type_word(m.Type));

		if (!occupied && m.State != MEM_FREE &&
		    rhi > win_lo && rlo < win_hi) {
			occupied = 1;
			occ = m;
		}
		if (m.RegionSize == 0)
			break;
		addr = rhi;
	}

	printf("window_free=%s\n", occupied ? "no" : "yes");
	if (occupied) {
		uint64_t olo = (uint64_t)(UINT_PTR) occ.BaseAddress;
		uint64_t ohi = olo + (uint64_t) occ.RegionSize;
		printf("occupant=%s-%s\n",
		       type_word(occ.Type), state_word(occ.State));
		const char *span = "intersects-window";
		if (olo <= win_lo && ohi >= win_hi)
			span = "covers-whole-window";
		else if (olo <= win_lo && ohi > win_lo)
			span = "covers-window-base";
		else if (olo > win_lo && ohi >= win_hi)
			span = "covers-window-top";
		printf("occupant_span=%s\n", span);
	}

	void *got = VirtualAllocEx(pi.hProcess, (void *)(UINT_PTR) win_lo,
				   (SIZE_T) WIN_SIZE, MEM_RESERVE, PAGE_NOACCESS);
	if (got) {
		printf("reserve_in=ok\n");
		VirtualFreeEx(pi.hProcess, got, 0, MEM_RELEASE);
	} else {
		printf("reserve_in=refused err=%lu\n",
		       (unsigned long) GetLastError());
	}

	TerminateProcess(pi.hProcess, 0);
	WaitForSingleObject(pi.hProcess, 2000);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
