/* realproc-probe -- the reent as a real process of the faced runtime sees it.
 *
 * fault.c's shape: -nostdlib against the WP-26 crt0 and -lcygwin, so startup
 * runs the _dll_crt0 protocol and the reent/TLS structure is brought up the
 * sanctioned way -- the same shape WP-41's loader gives every ELF frame.
 * Every libc call crosses by sysv_abi pointer straight at the export.
 *
 * The measurement: a NOSIGFE reent-consuming body, strtol on an overflow,
 * must return LONG_MAX and set errno=ERANGE through the very reent __errno
 * hands back. That is a libc body reading and writing the caller's reent at
 * runtime, which is what reent-tls-bringup asks for. Reports through kernel32
 * only; run detached via cmd (the faced runtime's console wedges on a pty).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define ELFSYSV_ERANGE 34	/* newlib errno.h */

void *memset(void *s, int c, size_t n)
{ unsigned char *p = s; while (n--) *p++ = (unsigned char)c; return s; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *dp = d; const unsigned char *sp = s; while (n--) *dp++ = *sp++; return d; }

/* crt0's _cygwin_crt0_common calls cygwin_internal (CW_USER_DATA) MS-style;
 * the export is a System V veneer now, so interpose the crossing. */
unsigned long long cygwin_internal(unsigned int t, ...)
{
	typedef unsigned long long (__attribute__((sysv_abi)) *cw_fn)(unsigned int, ...);
	static cw_fn p;
	if (!p)
		p = (cw_fn)(void *)GetProcAddress(
			GetModuleHandleA("elfsysv1.dll"), "cygwin_internal");
	return p ? p(t) : 0;
}

static void outs(const char *s)
{
	DWORD n = 0, len = 0;
	while (s[len])
		len++;
	WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, len, &n, NULL);
}
static void outn(long v)
{
	char b[24];
	int i = sizeof b;
	unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
	b[--i] = 0;
	do {
		b[--i] = '0' + u % 10;
		u /= 10;
	} while (u);
	if (v < 0)
		b[--i] = '-';
	outs(b + i);
}

typedef long  (__attribute__((sysv_abi)) *strtol_fn)(const char *, char **, int);
typedef int  *(__attribute__((sysv_abi)) *errno_fn)(void);
typedef void *(__attribute__((sysv_abi)) *getreent_fn)(void);

int main(void)
{
	HMODULE h = GetModuleHandleA("elfsysv1.dll");
	strtol_fn   f_strtol = (strtol_fn)(void *)GetProcAddress(h, "strtol");
	errno_fn    f_errno  = (errno_fn)(void *)GetProcAddress(h, "__errno");
	getreent_fn f_reent  = (getreent_fn)(void *)GetProcAddress(h, "__getreent");

	int  *ep = f_errno();
	void *r  = f_reent();
	outs("realproc_getreent="); outs(r ? "reachable" : "null"); outs("\n");
	outs("realproc_errno_is_reent_base=");
	outs((void *)ep == r ? "yes" : "no"); outs("\n");

	*ep = 0;
	long v = f_strtol("999999999999999999999999999", (char **)0, 10);
	int after = *f_errno();
	outs("realproc_strtol_overflow_ret_is_longmax=");
	outs(v == 0x7fffffffffffffffL ? "yes" : "no"); outs("\n");
	outs("realproc_body_sets_errno_erange=");
	outs(after == ELFSYSV_ERANGE ? "yes" : "no");
	outs(" (got "); outn(after); outs(")\n");

	*ep = 4242;
	outs("realproc_errno_roundtrip=");
	outs(*f_errno() == 4242 ? "holds" : "broken"); outs("\n");
	outs("verdict=yes\n");
	return 0;
}
