/*
 * hostload -- WP-27 milestone 6: DllMain and the PE TLS callback fired by
 * the host's own loader.
 *
 * WP-22 called both shapes from a harness.  Here nothing calls them: this is
 * a plain PE process that asks the host's loader for the faced DLL and then
 * only observes.  DllMain firing is measured twice -- positively, in that
 * LoadLibrary succeeds only after the DLL's dll_entry ran and returned TRUE,
 * and by a leaky control, a DLL built alongside whose DllMain answers FALSE
 * and whose load must therefore fail with ERROR_DLL_INIT_FAILED, which
 * proves the verdict really is DllMain's.  The TLS callback is read through
 * tlsdir.c's observation seam: the environment variable must be absent
 * before the load, must show one process attach after it, and must count a
 * thread's attach and detach after a thread runs and exits.
 *
 * The DLL is never unloaded: the runtime beneath the face is Cygwin, which
 * does not support unload, so process detach is out of scope here.
 *
 * Built and driven by t/hostload.sh with the host gcc.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

static void want(int ok, const char *fmt, ...)
{
	va_list ap;
	checks++;
	if (ok)
		return;
	failures++;
	fputs("FAIL: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static int observed(char *buf, DWORD len)
{
	return GetEnvironmentVariableA("ELFSYSV_TLS_OBSERVED", buf, len) != 0;
}

static DWORD WINAPI quick_thread(void *arg)
{
	(void)arg;
	return 0;
}

int main(int argc, char **argv)
{
	char buf[64];
	HMODULE dll, control;
	HANDLE thread;

	if (argc != 3) {
		fprintf(stderr, "usage: %s <elfsysv1.dll> <control.dll>\n",
			argv[0]);
		return 2;
	}

	/* The control first: a DllMain that returns FALSE must sink the
	 * load, or a LoadLibrary verdict proves nothing about DllMain. */
	SetLastError(0);
	control = LoadLibraryA(argv[2]);
	want(control == NULL, "control DLL loaded despite DllMain's FALSE");
	want(GetLastError() == ERROR_DLL_INIT_FAILED,
	     "control load failed with %lu, not ERROR_DLL_INIT_FAILED",
	     GetLastError());

	/* No firing before the load. */
	want(!observed(buf, sizeof buf),
	     "ELFSYSV_TLS_OBSERVED set before the DLL loaded: %s", buf);

	/* The load itself: the host loader maps the DLL, runs the TLS
	 * process-attach callback, enters dll_entry, and hands back a module
	 * only if dll_entry said TRUE. */
	dll = LoadLibraryA(argv[1]);
	want(dll != NULL, "LoadLibrary(%s): error %lu", argv[1],
	     GetLastError());
	if (!dll)
		goto out;

	want(observed(buf, sizeof buf) && strcmp(buf, "1 0 0") == 0,
	     "after load, expected \"1 0 0\", got \"%s\"",
	     observed(buf, sizeof buf) ? buf : "(unset)");

	/* A thread created after the load: the loader owes its attach and,
	 * once it returns, its detach to every TLS callback in the image. */
	thread = CreateThread(NULL, 0, quick_thread, NULL, 0, NULL);
	want(thread != NULL, "CreateThread: error %lu", GetLastError());
	if (thread) {
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
	}
	want(observed(buf, sizeof buf) && strcmp(buf, "1 1 1") == 0,
	     "after a thread ran, expected \"1 1 1\", got \"%s\"",
	     observed(buf, sizeof buf) ? buf : "(unset)");

out:
	printf("hostload: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
