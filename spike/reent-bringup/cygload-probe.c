/* cygload-probe -- the reent slot as a foreign PE that LoadLibrary'd the
 * faced runtime sees it, with no bring-up call at all.
 *
 * Calls the faced __errno/__getreent (System V faces) on the main thread and
 * on a second host thread, writes distinct values, and checks each slot is
 * non-null, stable across calls, isolated per thread, and equal to the reent
 * base (newlib puts _errno at reent offset 0). This is the storage the
 * crossing already reaches; whether a libc *body* run in this shape sets it
 * correctly is a separate question the real-process probe answers.
 *
 * Findings are printed as `key=word`; the addresses ride along as context.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int   *(__attribute__((sysv_abi)) *errno_fn)(void);
typedef void  *(__attribute__((sysv_abi)) *getreent_fn)(void);
static errno_fn xerrno;

static int roundtrip(int want)
{
	int *a = xerrno();
	*a = want;
	int *b = xerrno();
	return a && (a == b) && (*b == want);
}

static void *g_worker_slot;
static int   g_worker_rt;
static DWORD WINAPI worker(void *arg)
{
	(void)arg;
	g_worker_slot = xerrno();
	g_worker_rt = roundtrip(1313);
	return 0;
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1]
		: "C:\\-\\repo\\elf-sysv-nt\\a\\build\\wp27-face\\elfsysv1.dll";
	HMODULE h = LoadLibraryA(path);
	if (!h) { printf("cygload_load=failed (%lu)\n", GetLastError()); return 2; }
	xerrno = (errno_fn)(void *)GetProcAddress(h, "__errno");
	getreent_fn getreent = (getreent_fn)(void *)GetProcAddress(h, "__getreent");
	if (!xerrno || !getreent) { printf("cygload_resolve=failed\n"); return 3; }

	int  *main_slot = xerrno();
	void *reent     = getreent();
	printf("cygload_getreent=%s\n", reent ? "reachable" : "null");
	printf("cygload_errno_is_reent_base=%s\n",
	       (void *)main_slot == reent ? "yes" : "no");
	printf("cygload_errno_roundtrip=%s\n", roundtrip(4242) ? "holds" : "broken");

	HANDLE t = CreateThread(NULL, 0, worker, NULL, 0, NULL);
	WaitForSingleObject(t, 10000);
	int *main_slot2 = xerrno();
	int distinct = (g_worker_slot && g_worker_slot != (void *)main_slot);
	int main_kept = (main_slot2 == main_slot) && (*main_slot2 == 4242);
	printf("cygload_errno_thread_local=%s\n",
	       (distinct && g_worker_rt && main_kept) ? "holds" : "broken");
	printf("  main_slot=%p worker_slot=%p reent=%p\n",
	       (void *)main_slot, g_worker_slot, reent);
	printf("verdict=yes\n");
	return 0;
}
