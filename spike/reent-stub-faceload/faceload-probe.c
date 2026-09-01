/* faceload-probe -- item 1's face-base half, measured in the real-process shape.
 *
 * The loader's --runtime option (loader/exec/stub.c) LoadLibraryA's the faced
 * elfsysv1.dll and hands its base to the ELF image through AT_BASE, so the
 * WP-53 veneer's resolver reaches the face at run time. In the plain-PE cygload
 * host the stub is today, that LoadLibraryA wedges: elfsysv1.dll's cygwin init
 * reserves its cygheap at a fixed high address and fails when the foreign PE
 * host never reserved it -- "heap allocated at wrong address ... error 1114"
 * (DR-0060, and the reent-face-bringup live run's face_base_via_runtime=no).
 *
 * DR-0060/0066/0067 settle the shape that clears it: a real process OF the
 * faced runtime -- linked -nostdlib against the WP-26 crt0.o and -lcygwin, so
 * _dll_crt0 brings the reent up and the faced DLL is the process's OWN runtime,
 * loaded and cygheap-reserved at startup rather than by a later LoadLibraryA.
 * This probe is that host. It measures the fact the full stub relink turns on:
 * in this shape, is the faced runtime's base reachable -- through the already-
 * loaded module (GetModuleHandleA) and through the literal --runtime operation
 * (LoadLibraryA) -- without the 1114 wedge?
 *
 * Built twice, like stub-abi-probe: without -DBRIDGE to confirm the startup
 * crossing still gates a real-process host, and with it to reach main and take
 * the measurement. All reporting is through kernel32 (native Microsoft ABI,
 * always safe) and hand-formatted, so no marker is itself a crossing and the
 * measurement is not perturbed by the boundary it studies.
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

#ifdef BRIDGE
/* crt0's _cygwin_crt0_common calls cygwin_internal(CW_USER_DATA) Microsoft-style
 * during startup; the faced export is a System V veneer. Interpose one local
 * cygwin_internal that re-crosses it the sanctioned way, so startup reaches
 * main -- the bridge spike/reent-stub-realproc-window pinned. */
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

/* Hand-formatted so reporting calls no libc. Emits "<label>0x<hex>\n". */
static void mark_hex(const char *label, unsigned long long v)
{
	char buf[64];
	int i = 0, j;
	while (label[i]) { buf[i] = label[i]; i++; }
	buf[i++] = '0'; buf[i++] = 'x';
	if (v == 0) buf[i++] = '0';
	else {
		char t[16]; int k = 0;
		while (v) { unsigned d = (unsigned)(v & 0xf); t[k++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v >>= 4; }
		for (j = k - 1; j >= 0; j--) buf[i++] = t[j];
	}
	buf[i++] = '\n'; buf[i] = 0;
	mark(buf);
}

int main(void)
{
	mark("A:reached-main\n");

	/* The faced runtime, already the process's own -- brought up by crt0, not
	 * by a LoadLibraryA. This is the base --runtime would publish through
	 * AT_BASE in this shape. */
	HMODULE own = GetModuleHandleA("elfsysv1.dll");
	if (own) mark_hex("H:own-runtime-base=", (unsigned long long)(UINT_PTR) own);
	else mark("H:own-runtime-absent\n");

	/* The literal --runtime operation: LoadLibraryA the faced DLL. In the
	 * cygload host this is the 1114 wedge; here the module is already loaded,
	 * so this bumps its refcount and returns its handle without re-reserving
	 * the cygheap. */
	SetLastError(0);
	HMODULE l = LoadLibraryA("elfsysv1.dll");
	if (l) {
		mark_hex("L:loadlibrary-base=", (unsigned long long)(UINT_PTR) l);
		if (own && l == own) mark("M:base-matches\n");
		else mark("M:base-differs\n");
	} else {
		mark_hex("L:loadlibrary-failed-err=", (unsigned long long) GetLastError());
	}

	mark("Z:done\n");
	return 0;
}
