/* stub-abi-probe -- where the real-process relink of the loader stub faults,
 * and why. The loader's PE host stub (loader/exec/stub.c) is host code that
 * speaks the Microsoft ABI. spike/reent-stub-link relinks it in the real-
 * process shape (-nostdlib + WP-26 crt0.o + -lcygwin) so _dll_crt0 brings the
 * reent up, and finds it faults before its --version path. This probe isolates
 * that fault to a cause with a controlled before/after rather than an account.
 *
 * The stub's own code carries no dependency on this probe; the probe stands in
 * for the stub as the smallest real-process image that reproduces the two
 * facts that matter: a startup that faults, and a host libc call that does not
 * cross. Built twice, with and without -DBRIDGE.
 *
 * crt0's _cygwin_crt0_common calls cygwin_internal(CW_USER_DATA) during startup
 * with the Microsoft ABI. Against the faced elfsysv1.dll that export is a
 * System V veneer (the WP-27 crossing ABI), so the call reaches a System V
 * body Microsoft-style and faults before main. -DBRIDGE interposes a local
 * cygwin_internal that re-crosses the call the sanctioned way; without it the
 * faced import is what the call reaches.
 *
 * Reports through kernel32 (native Microsoft ABI, always safe) so a marker is
 * never itself a crossing. The single printf is the one host libc call under
 * test: a Microsoft-ABI call into the faced System V libc.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdio.h>

/* -nostdlib drops the CRT's own copies; give the compiler the two it inlines
 * references to, so neither becomes a Microsoft-ABI call into the faced libc. */
void *memset(void *s, int c, size_t n)
{ unsigned char *p = s; while (n--) *p++ = (unsigned char)c; return s; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *dp = d; const unsigned char *sp = s; while (n--) *dp++ = *sp++; return d; }

#ifdef BRIDGE
unsigned long long cygwin_internal(unsigned int t, ...)
{
	typedef unsigned long long (__attribute__((sysv_abi)) *cw_fn)(unsigned int, ...);
	static cw_fn p;
	if (!p)
		p = (cw_fn)(void *)GetProcAddress(
			GetModuleHandleA("elfsysv1.dll"), "cygwin_internal");
	return p ? p(t) : 0;
}
#endif

static void mark(const char *s)
{
	DWORD n = 0, len = 0;
	while (s[len])
		len++;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, len, &n, NULL);
}

int main(void)
{
	mark("A:reached-main\n");        /* startup got here */
	printf("B:printf-ran\n");        /* one host MS-ABI call into the faced SysV libc */
	fflush(stdout);
	mark("C:after-printf\n");        /* control survived the call */
	return 0;
}
