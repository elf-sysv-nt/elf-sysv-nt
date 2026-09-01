/* version-probe -- does a real-process host stub reach and complete its
 * --version path, once the two fixes the reent-stub-* spikes measured are
 * applied?  spike/reent-stub-link found the real stub links in the
 * real-process shape but faults before --version
 * (realproc_stub_reaches_version=no).  Two later spikes localized why and
 * proved each fix in isolation: spike/reent-stub-realproc-window found the
 * fault is the unbridged startup cygwin_internal crossing and that a local
 * sysv_abi bridge reaches main, while a plain Microsoft-ABI libc call still
 * does not cross (ms_abi_libc_call_crosses=no); spike/reent-stub-libc-crossing
 * found a libc call reached through an explicit sysv_abi thunk does cross,
 * stdio included.
 *
 * This probe combines them at the exact path reent-stub-link found faulting:
 * loader/exec/stub.c's --version, which is printf("%s\n", RELEASE) -- a
 * reent-consuming stdio body.  One source, three build variants select the
 * three findings the measure script reads:
 *
 *   NO_BRIDGE   -- no startup bridge.  Control faults in crt0 startup before
 *                  the version path, reproducing reent-stub-link at this path.
 *   PLAIN_PRINT -- bridge in, but the RELEASE line printed through an ordinary
 *                  Microsoft-ABI call into the faced libc.  main is reached;
 *                  the line does not cross (the realproc-window ms_abi finding
 *                  at the version path).
 *   (default)   -- bridge in, RELEASE line printed through an explicit sysv_abi
 *                  thunk.  The line crosses and control survives: the
 *                  real-process stub completes its --version.
 *
 * Only the ABI direction of the version print, and the presence of the startup
 * bridge, differ across the variants -- the isolation the two prior spikes set
 * up.  All markers report through kernel32 (native Microsoft ABI, always safe),
 * so a marker is never itself a crossing.  RELEASE matches stub.c's literal so
 * the finding is about the loader's real version string, not a stand-in.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stddef.h>

static const char RELEASE[] = "elfsysv-stub 1.0";

/* -nostdlib drops the CRT's own copies; supply the two the compiler inlines
 * references to so neither becomes a Microsoft-ABI call into the faced libc. */
void *memset(void *s, int c, size_t n)
{ unsigned char *p = s; while (n--) *p++ = (unsigned char)c; return s; }
void *memcpy(void *d, const void *s, size_t n)
{ unsigned char *dp = d; const unsigned char *sp = s; while (n--) *dp++ = *sp++; return d; }

#ifndef NO_BRIDGE
/* Startup bridge: crt0's cygwin_internal(CW_USER_DATA) must re-cross the call
 * the sanctioned System V way or control faults before main.  Same shape as
 * spike/reent-stub-realproc-window's -DBRIDGE and the libc-crossing probe. */
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

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int seq(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* The faced libc puts, reached two ways.  sysv_abi -- the crossing the ELF
 * world uses.  ms_abi (the platform default here) -- the direction the plain
 * host call takes, the one realproc-window found does not cross. */
typedef int (__attribute__((sysv_abi)) *puts_sysv)(const char *);
typedef int (*puts_ms)(const char *);

/* The stub's --version path in miniature: on "--version", emit RELEASE through
 * the faced libc, then a survival marker.  This mirrors stub.c line 291. */
static void print_version(HMODULE h)
{
mark("B:before-version\n");
#ifdef PLAIN_PRINT
puts_ms pu = (puts_ms)(void *)GetProcAddress(h, "puts");
#else
puts_sysv pu = (puts_sysv)(void *)GetProcAddress(h, "puts");
#endif
if (pu)
pu(RELEASE);
mark("E:after-version\n");
}

int main(int argc, char **argv)
{
HMODULE h = GetModuleHandleA("elfsysv1.dll");
mark("A:reached-main\n");

int want_version = 0;
for (int i = 1; i < argc; i++)
if (seq(argv[i], "--version"))
want_version = 1;
/* Detached under cmd the harness passes --version; guard against an empty
 * argv so the path is exercised regardless. */
if (!want_version && argc <= 1)
want_version = 1;

if (want_version)
print_version(h);

(void)slen;
return 0;
}
