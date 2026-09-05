/*
 * host.c -- the NT glue beneath the core.  This is the one core-side file that
 * speaks to the host directly, so the forbidden tokens it must name (a console
 * handle, an allocation) carry the substrate-line-ok marker the interface
 * check honours: the glue is deliberately below the line, like the substrate.
 *
 * The lifecycle is a flag, not an event.  exit_group, on the user thread,
 * records the code and raises `done`; the main thread polls it.  A poll rather
 * than a wait object keeps the core's join free of a host handle, and a bounded
 * poll means a wedged run fails on the timeout instead of hanging forever, the
 * same shape the conformance suite draws for its own waits.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

#include "host.h"

long host_console_write(int fd, const void *buf, size_t len)
{
	HANDLE h;			/* substrate-line-ok: host console fd */
	DWORD wrote = 0;
	DWORD want = (DWORD)len;

	if (fd == 1)
		h = GetStdHandle(STD_OUTPUT_HANDLE);	/* substrate-line-ok */
	else if (fd == 2)
		h = GetStdHandle(STD_ERROR_HANDLE);	/* substrate-line-ok */
	else
		return -1;

	if (!WriteFile(h, buf, want, &wrote, NULL))	/* substrate-line-ok */
		return -1;
	return (long)wrote;
}

void *host_alloc_kstack(size_t size)
{
	size_t n = (size + 15) & ~(size_t)15;
	unsigned char *base = malloc(n);

	if (!base)
		return NULL;
	/* The top is one past the block, brought down to a 16 boundary; the gate
	 * builds its first frame just below it. */
	return base + n;
}

/* The run flag and the code exit_group leaves behind it. */
static volatile LONG g_run_done;
static volatile LONG g_run_code;

void host_run_init(void)
{
	g_run_done = 0;
	g_run_code = 0;
}

void host_run_finish(int code)
{
	g_run_code = (LONG)code;
	/* Publish the code before the flag, so a reader that sees done reads the
	 * code that went with it. */
	MemoryBarrier();
	InterlockedExchange(&g_run_done, 1);
}

int host_run_wait(unsigned timeout_ms)
{
	unsigned waited = 0;

	while (!g_run_done) {
		if (waited >= timeout_ms)
			return -1;
		Sleep(1);
		waited++;
	}
	return (int)g_run_code;
}

void host_park(void)
{
	for (;;)
		Sleep(0xffffffffu);
}
