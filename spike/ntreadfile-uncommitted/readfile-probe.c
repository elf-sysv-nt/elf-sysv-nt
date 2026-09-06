/* readfile-probe: does an I/O call accept a user buffer whose pages are
 * reserved but not committed, given a vectored handler that would commit
 * them on the fault?
 *
 * Proposal 0012 § 4 (DR-0098) makes every I/O go through a kernel buffer and
 * says why under N: the I/O manager probes the user buffer in kernel mode,
 * where no vectored handler runs, so a lazily committed user page fails the
 * call. That was a reading. This measures it: ReadFile and WriteFile against
 * reserved, committed-no-access, and committed buffers, with the arena's
 * handler installed, on a synchronous and an overlapped handle.
 *
 * Native, mingw. Reads its own executable as the file.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#define RELEASE "readfile-probe 1.0"
#define SZ (64 * 1024)

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

static uint8_t *volatile g_lo, *volatile g_hi;   /* the pointers themselves are volatile: the store must precede the touch */
static volatile LONG g_handler_hits;

static LONG CALLBACK handler(EXCEPTION_POINTERS *ep)
{
	EXCEPTION_RECORD *r = ep->ExceptionRecord;
	uintptr_t a;
	if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_lo)
		return EXCEPTION_CONTINUE_SEARCH;
	a = (uintptr_t) r->ExceptionInformation[1];
	if (a < (uintptr_t) g_lo || a >= (uintptr_t) g_hi)
		return EXCEPTION_CONTINUE_SEARCH;
	InterlockedIncrement(&g_handler_hits);
	if (!VirtualAlloc((void *) (a & ~4095ULL), 4096, MEM_COMMIT, PAGE_READWRITE))
		return EXCEPTION_CONTINUE_SEARCH;
	return EXCEPTION_CONTINUE_EXECUTION;
}

static void one(const char *tag, HANDLE h, int overlapped, uint8_t *buf, int write)
{
	DWORD n = 0, err = 0;
	BOOL ok;
	OVERLAPPED ov;
	char key[80];

	g_handler_hits = 0;
	g_lo = buf; g_hi = buf + SZ;
	SetLastError(0);
	if (overlapped) {
		memset(&ov, 0, sizeof ov);
		ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		ok = write ? WriteFile(h, buf, SZ, &n, &ov) : ReadFile(h, buf, SZ, &n, &ov);
		err = GetLastError();
		if (!ok && err == ERROR_IO_PENDING) {
			ok = GetOverlappedResult(h, &ov, &n, TRUE);
			err = ok ? 0 : GetLastError();
		}
		CloseHandle(ov.hEvent);
	} else {
		SetFilePointer(h, 0, NULL, FILE_BEGIN);
		ok = write ? WriteFile(h, buf, SZ, &n, NULL) : ReadFile(h, buf, SZ, &n, NULL);
		err = ok ? 0 : GetLastError();
	}
	g_lo = g_hi = NULL;
#define K(s) (snprintf(key, sizeof key, "%s_%s", tag, s), key)
	emit(K("ok"), "%d", ok ? 1 : 0);
	emit(K("error"), "%lu", (unsigned long) err);
	emit(K("bytes"), "%lu", (unsigned long) n);
	emit(K("handler_hits"), "%ld", (long) g_handler_hits);
#undef K
}

int main(int argc, char **argv)
{
	char exe[MAX_PATH], tmp[MAX_PATH], path[MAX_PATH];
	HANDLE sync, async, wsync;
	uint8_t *reserved, *noaccess, *committed;
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		fprintf(stderr, "readfile-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (!AddVectoredExceptionHandler(1, handler)) { emit("handler_installed", "0"); return 1; }
	emit("handler_installed", "1");

	GetModuleFileNameA(NULL, exe, sizeof exe);
	sync = CreateFileA(exe, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	async = CreateFileA(exe, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	GetTempPathA(sizeof tmp, tmp);
	GetTempFileNameA(tmp, "rfp", 0, path);
	wsync = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
	emit("handles", "%d", (sync != INVALID_HANDLE_VALUE && async != INVALID_HANDLE_VALUE && wsync != INVALID_HANDLE_VALUE) ? 1 : 0);

	reserved = VirtualAlloc(NULL, SZ, MEM_RESERVE, PAGE_READWRITE);
	noaccess = VirtualAlloc(NULL, SZ, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
	committed = VirtualAlloc(NULL, SZ, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	emit("buffers", "%d", (reserved && noaccess && committed) ? 1 : 0);

	/* q1. A user-mode touch of the reserved buffer takes the handler: the
	 * control that the handler works at all. */
	g_lo = reserved; g_hi = reserved + SZ; g_handler_hits = 0;
	*(volatile uint8_t *) (reserved + 4096) = 1;
	g_lo = g_hi = NULL;
	emit("q1_user_touch_handler_hits", "%ld", (long) g_handler_hits);
	VirtualFree(reserved, SZ, MEM_DECOMMIT);

	/* q2. ReadFile into reserved memory, synchronous handle. */
	one("q2_read_sync_reserved", sync, 0, reserved, 0);
	VirtualFree(reserved, SZ, MEM_DECOMMIT);
	/* q3. The same on an overlapped handle. */
	one("q3_read_async_reserved", async, 1, reserved, 0);
	VirtualFree(reserved, SZ, MEM_DECOMMIT);
	/* q4. ReadFile into committed PAGE_NOACCESS memory (a PROT_NONE VMA). */
	one("q4_read_sync_noaccess", sync, 0, noaccess, 0);
	/* q5. WriteFile from reserved memory. */
	one("q5_write_sync_reserved", wsync, 0, reserved, 1);
	VirtualFree(reserved, SZ, MEM_DECOMMIT);
	/* q6. The control: committed memory. */
	one("q6_read_sync_committed", sync, 0, committed, 0);
	one("q6_read_async_committed", async, 1, committed, 0);
	/* q7. Committed, then decommitted in the middle: the MADV_DONTNEED shape
	 * under N, half the buffer gone. */
	VirtualFree(committed + 4 * 4096, 4 * 4096, MEM_DECOMMIT);
	one("q7_read_sync_half_decommitted", sync, 0, committed, 0);

	CloseHandle(sync); CloseHandle(async); CloseHandle(wsync);
	return 0;
}
