/* stderr-crossing-probe -- does a host->faced stderr write cross through an
 * explicit System V thunk, and by which route?
 *
 * spike/reent-stub-libc-crossing established that a host->faced libc call
 * crosses through a sysv_abi thunk, reent-consuming stdio (puts) included, so
 * the landed rp_puts thunk carries the stub's stdout output (--version, the
 * --dry-run report). The stub's diagnostics -- say, refuse, usage, and the
 * unknown-option and no-argument messages -- write to stderr, not stdout, so
 * they need a stderr twin of that route before they reroute.
 *
 * The faced elfsysv1.dll exports no `stderr` FILE* (nor `stdout`), only the
 * fd-taking bodies: this probe confirms that and measures the two FILE*-free
 * routes to fd 2 that remain:
 *
 *   write(2, s, n)      -- the raw fd-2 body. Non-variadic, no FILE*, no
 *     va_list crossing the ABI boundary. If it crosses, a stderr twin of
 *     rp_puts is a plain sysv_abi write thunk.
 *   dprintf(2, "%s", s) -- the fd-2 formatted body. Variadic: it carries a
 *     Microsoft-ABI va_list into the faced runtime's System V vararg reader,
 *     the two-register-save-area disagreement DR-0066 draws a line at. If it
 *     does not cross while write does, that line holds for stderr too and the
 *     reroute must format host-side and cross through write, as rp_puts does.
 *
 * Startup still crosses cygwin_internal Microsoft-style (crt0's
 * _cygwin_crt0_common), so the same local bridge spike/reent-stub-libc-crossing
 * uses is compiled in unconditionally -- scaffolding, not the thing under test.
 *
 * All markers report through kernel32 WriteFile to the real stderr (native
 * Microsoft ABI, always safe), so a marker is never itself a crossing; the
 * faced routes emit their own distinct tokens, which only a crossing produces.
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
 * spike/reent-stub-libc-crossing's bridge. */
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

static unsigned slen(const char *s)
{ unsigned n = 0; while (s[n]) n++; return n; }

/* The faced fd-2 bodies reached through explicit System V thunks, resolved from
 * the faced DLL's PE export directory -- the same resolution the bridge uses. */
typedef long (__attribute__((sysv_abi)) *write_fn)(int, const void *, unsigned long);
typedef int  (__attribute__((sysv_abi)) *dprintf_fn)(int, const char *, ...);

int main(void)
{
	HMODULE h = GetModuleHandleA("elfsysv1.dll");
	mark("A:reached-main\n");

	/* Route 1: the raw fd-2 write. Its token reaching output is the crossing. */
	write_fn wr = (write_fn)(void *)GetProcAddress(h, "write");
	mark("B:before-write\n");
	if (wr) {
		const char *msg = "W:write-crossed\n";
		long r = wr(2, msg, slen(msg));
		mark(r == (long)slen(msg) ? "W:write-returned-len\n"
		                          : "W:write-returned-other\n");
	} else {
		mark("W:write-unresolved\n");
	}
	mark("C:after-write\n");

	/* Route 2: the fd-2 formatted body, a variadic %s carrying a va_list. */
	dprintf_fn dp = (dprintf_fn)(void *)GetProcAddress(h, "dprintf");
	mark("D:before-dprintf\n");
	if (dp)
		dp(2, "%s", "P:dprintf-crossed\n");
	else
		mark("D:dprintf-unresolved\n");
	mark("E:after-dprintf\n");

	return 0;
}
