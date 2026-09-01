/* dllinit-probe -- the cygload bring-up that does not work.
 *
 * A foreign PE that LoadLibrary's the faced runtime and then calls the
 * documented foreign-PE bring-up, cygwin_dll_init (System V face). The
 * vendor leaves the main thread marked in-cygwin when dll_crt0_1 returns
 * early for a dynamically loaded DLL, and the call does not return in this
 * process shape -- the same wedge runtime/face/t/fault.c records for the
 * cygload shape. measure.sh runs this under a bounded timeout and reads the
 * finding from whether it ever prints its "returned" line; the source prints
 * around the call so the transcript shows where it stops.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef void (__attribute__((sysv_abi)) *dll_init_fn)(void);

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1]
		: "C:\\-\\repo\\elf-sysv-nt\\a\\build\\wp27-face\\elfsysv1.dll";
	HMODULE h = LoadLibraryA(path);
	if (!h) { printf("load=failed (%lu)\n", GetLastError()); fflush(stdout); return 2; }
	dll_init_fn dll_init = (dll_init_fn)(void *)GetProcAddress(h, "cygwin_dll_init");
	if (!dll_init) { printf("resolve=failed\n"); fflush(stdout); return 3; }
	printf("calling=cygwin_dll_init\n"); fflush(stdout);
	dll_init();
	printf("returned=cygwin_dll_init\n"); fflush(stdout);
	return 0;
}
