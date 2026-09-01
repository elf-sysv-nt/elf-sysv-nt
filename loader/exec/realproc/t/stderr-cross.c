/*
 * WP-56 item 1 ecross stage: the real-process stub's stderr diagnostics
 * reach fd 2 across the faced runtime. The stub composes each diagnostic
 * host-side and emits the finished line through the RP_EPUTS seam, whose
 * real-process body is realproc-cross.c's sysv_abi write(2) thunk -- the
 * route spike/reent-stub-stderr-crossing measured, the faced elfsysv1.dll
 * exporting no `stderr` FILE*. This exercises that real unit, not a
 * spike-local copy.
 *
 * Built real-process (-nostdlib, crt0.o, -lcygwin, -lgcc, -DELFSYSV_REALPROC):
 * startup crosses through realproc-cross.c's cygwin_internal bridge, and
 * RP_EPUTS rides its sysv_abi write thunk. Markers ride kernel32 (native
 * Microsoft ABI, always safe), so a marker is never itself a crossing; only
 * the D: line rides the faced libc. Both land on fd 2 and are read together.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../realproc.h"

static void mark(const char *s)
{
DWORD n = 0, len = 0;
while (s[len]) len++;
WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, len, &n, NULL);
}

int main(void)
{
mark("A:reached-main\n");
mark("B:before-eputs\n");
RP_EPUTS("D:crossed-stderr\n");
mark("E:after-eputs\n");
return 0;
}
