/*
 * WP-56 item 1 cross stage: the real-process stub's --version path, built from
 * the shipped compatibility units (realproc-str.c, realproc-cross.c), reaches
 * and completes across the faced runtime.
 *
 * This is stub.c's --version in miniature -- parse argv for --version with the
 * host-safe RP_STRCMP, then emit the RELEASE line stub.c carries through the
 * RP_PUTS seam -- but it exercises the real implementation units, not a
 * spike-local copy. Built real-process (-nostdlib, crt0.o, -lcygwin, -lgcc,
 * -DELFSYSV_REALPROC): startup crosses through realproc-cross.c's
 * cygwin_internal bridge, and RP_PUTS rides its sysv_abi thunk.
 *
 * Markers ride kernel32 (native Microsoft ABI, always safe), so a marker is
 * never itself a crossing; only the RELEASE line rides the faced libc. RELEASE
 * matches stub.c's literal so the cert is about the loader's real version
 * string.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../realproc.h"

static const char RELEASE[] = "elfsysv-stub 1.0";

static void mark(const char *s)
{
DWORD n = 0, len = 0;
while (s[len]) len++;
WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, len, &n, NULL);
}

int main(int argc, char **argv)
{
int want_version = 0, i;
mark("A:reached-main\n");
for (i = 1; i < argc; i++)
if (RP_STRCMP(argv[i], "--version") == 0)
want_version = 1;
if (!want_version && argc <= 1)
want_version = 1;
if (want_version) {
mark("B:before-version\n");
RP_PUTS(RELEASE);
mark("E:after-version\n");
}
return 0;
}
