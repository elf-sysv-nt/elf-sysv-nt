/* libc-crossing-probe -- does a host->faced libc call cross when it is reached
 * through an explicit System V thunk, rather than an ordinary Microsoft-ABI
 * call? spike/reent-stub-realproc-window measured the ordinary MS-ABI call and
 * found it does not cross (ms_abi_libc_call_crosses=no); a cygwin_internal
 * reached through a sysv_abi bridge does. This probe asks whether that same
 * bridge shape carries a general libc call, and splits the question so the
 * answer localizes the obstacle:
 *
 *   strlen -- a reent-free leaf. If it crosses, the sysv_abi thunk mechanism
 *     itself carries a host->faced libc call; what is left is not the ABI.
 *   puts   -- a reent-consuming stdio body. If it does not cross while strlen
 *     does, the remaining obstacle is reent/stdio bring-up (item 3), not the
 *     thunk.
 *
 * Startup still crosses cygwin_internal Microsoft-style (crt0's
 * _cygwin_crt0_common), so the same local bridge spike/reent-stub-realproc-
 * window uses is compiled in unconditionally here -- without it control never
 * reaches main and neither libc call can be measured. The bridge is startup
 * scaffolding, not the thing under test; the two sysv_abi libc calls are.
 *
 * All markers report through kernel32 (native Microsoft ABI, always safe), so a
 * marker is never itself a crossing.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

/* -nostdlib drops the CRT's own copies; give the compiler the two it inlines
 * references to, so neither becomes a Microsoft-ABI call into the faced libc. */
void *memset(void *s, int c, size_t n)
{ unsigned char *p = s; while (n--) *p++ = (unsigned char)c; return s; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *dp = d; const unsigned char *sp = s; while (n--) *dp++ = *sp++; return d; }

/* Startup bridge: crt0's cygwin_internal(CW_USER_DATA) must re-cross the call
 * the sanctioned System V way or control faults before main. Same shape as
 * spike/reent-stub-realproc-window's -DBRIDGE. */
unsigned long long cygwin_internal(unsigned int t, ...)
{
	typedef unsigned long long (__attribute__((sysv_abi)) *cw_fn)(unsigned int, ...);
	static cw_fn p;
	if (!p)
		p = (cw_fn)(void *)GetProcAddress(
			GetModuleHandleA("elfsysv1.dll"), "cygwin_internal");
	return p ? p(t) : 0;
}

static void mark(const char *s)
{
	DWORD n = 0, len = 0;
	while (s[len])
		len++;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, len, &n, NULL);
}

/* The faced libc reached through an explicit System V thunk, resolved from the
 * faced DLL's PE export directory -- the same resolution the bridge uses. */
typedef size_t (__attribute__((sysv_abi)) *strlen_fn)(const char *);
typedef int    (__attribute__((sysv_abi)) *puts_fn)(const char *);

int main(void)
{
	HMODULE h = GetModuleHandleA("elfsysv1.dll");
	mark("A:reached-main\n");

	/* reent-free leaf: strlen("abcd") must return 4 across the thunk. */
	strlen_fn sl = (strlen_fn)(void *)GetProcAddress(h, "strlen");
	mark("B:before-strlen\n");
	if (sl) {
		size_t r = sl("abcd");
		mark(r == 4 ? "S:strlen-crossed:4\n" : "S:strlen-wrong\n");
	} else {
		mark("S:strlen-unresolved\n");
	}
	mark("C:after-strlen\n");

	/* reent-consuming stdio body: puts must emit its line across the thunk. */
	puts_fn pu = (puts_fn)(void *)GetProcAddress(h, "puts");
	mark("D:before-puts\n");
	if (pu)
		pu("P:puts-ran");
	mark("E:after-puts\n");

	return 0;
}
