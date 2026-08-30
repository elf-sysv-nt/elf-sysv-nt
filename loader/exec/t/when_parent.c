/* The parent side of WP-41's first measurement, and the shape the answer
 * turned out to be.
 *
 * Every hook inside the image runs too late, because two things happen before
 * the first instruction of the image: the kernel places the initial thread's
 * stack, and cygwin1.dll's DllMain lays out its own mappings. Both allocate
 * with no requested base, both are therefore served from the lowest free
 * region, and the lowest free region is where a non-PIE ELF image has to go.
 *
 * What is early enough is the parent. It creates the child suspended, so no
 * instruction of the child -- not the loader's TLS callbacks, not the Cygwin
 * DLL's initialization, nothing -- has run, and reserves the window through
 * VirtualAllocEx before it resumes the thread. The one thing the parent cannot
 * undo from outside is the stack, which the kernel placed while the process
 * was being created, so the child's PE header has to ask for a stack small
 * enough to be placed below the window.
 *
 * Usage:
 *   when_parent [options] CHILD [CHILD-ARG]...
 *
 * Options:
 *   -s N, --size=N   bytes to reserve at the window base [default: the window]
 *   -n, --no-reserve Create the child suspended and resume it without
 *                    reserving, to show the difference is the reservation and
 *                    not the suspension.
 *   -q, --quiet      The key=value block alone.
 *   -h, --help       Print this message and exit.
 *
 * Exit: the child's status, or 1 if the reservation was refused, 2 usage.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../reserve.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *to)
{
	fprintf(to,
"Usage:\n"
"  when_parent [options] CHILD [CHILD-ARG]...\n"
"\n"
"Options:\n"
"  -s N, --size=N   bytes to reserve at the window base\n"
"  -n, --no-reserve resume without reserving\n"
"  -q, --quiet      the key=value block alone\n"
"  -h, --help       print this message and exit\n");
}

int main(int argc, char **argv)
{
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	char cmd[4096];
	uint64_t size = ELF_WINDOW_SIZE;
	int reserve = 1, quiet = 0, i;
	size_t used = 0;
	void *got;
	DWORD status = 0, err = 0;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return 0; }
		else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) quiet = 1;
		else if (!strcmp(a, "-n") || !strcmp(a, "--no-reserve")) reserve = 0;
		else if (!strcmp(a, "-s") && i + 1 < argc) size = strtoull(argv[++i], NULL, 0);
		else if (!strncmp(a, "--size=", 7)) size = strtoull(a + 7, NULL, 0);
		else if (a[0] == '-' && a[1]) { usage(stderr); return 2; }
		else break;
	}
	if (i >= argc) { usage(stderr); return 2; }

	/* The child is named with a Windows path, since CreateProcess is not a
	 * Cygwin call and does not know about mounts. Quoting is the minimum
	 * that survives a path with a space in it. */
	for (; i < argc && used + strlen(argv[i]) + 4 < sizeof cmd; i++)
		used += (size_t) snprintf(cmd + used, sizeof cmd - used,
					  "%s\"%s\"", used ? " " : "", argv[i]);

	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED,
			    NULL, NULL, &si, &pi)) {
		fprintf(stderr, "when_parent: cannot start %s: error %lu\n",
			cmd, GetLastError());
		return 1;
	}

	got = NULL;
	if (reserve) {
		got = VirtualAllocEx(pi.hProcess,
				     (void *) (UINT_PTR) ELF_WINDOW_BASE,
				     (SIZE_T) size, MEM_RESERVE, PAGE_NOACCESS);
		if (!got)
			err = GetLastError();
	}

	printf("parent_reserve_attempted=%d\n", reserve);
	printf("parent_reserved=%d\n", got != NULL);
	printf("parent_base=0x%" PRIx64 "\n", (uint64_t) ELF_WINDOW_BASE);
	printf("parent_size=0x%" PRIx64 "\n", size);
	printf("parent_error=%lu\n", (unsigned long) err);
	fflush(stdout);

	ResumeThread(pi.hThread);
	WaitForSingleObject(pi.hProcess, 60000);
	GetExitCodeProcess(pi.hProcess, &status);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	if (!quiet)
		printf("parent_child_status=%lu\n", (unsigned long) status);
	if (reserve && !got)
		return 1;
	return (int) status;
}
