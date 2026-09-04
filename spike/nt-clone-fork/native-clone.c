/* The NtCreateProcessEx clone, retried from an ntdll-only native-subsystem
 * parent -- the one hypothesis the assignment names as most likely.
 *
 * A Win32 process is registered with csrss; a native-subsystem process, whose
 * ntdll startup never connects to the Win32 subsystem, is not. Proposal 0011
 * says the fork host must have no csrss. So the question is whether the same
 * NtCreateProcessEx(null section) clone that refuses a thread with
 * STATUS_PROCESS_IS_TERMINATING under a Win32 parent behaves differently when
 * the parent itself is csrss-free. This is that parent.
 *
 * Built native: x86_64-w64-mingw32-gcc -nostdlib -nodefaultlibs
 *   -e NtProcessStartup -Wl,--subsystem,native -lntdll
 * It has no CRT and no kernel32; everything is ntdll. clone-probe.exe launches
 * it through NtCreateUserProcess (CreateProcess refuses a native image) and
 * reads the one thing this reports: its exit code, which encodes the outcome.
 *
 *   exit 0x00000000  the clone's thread was created and it ran
 *   exit 0x00000001  the thread was created but did not run in time
 *   exit 0x00000002  the clone itself could not be created
 *   exit <NTSTATUS>  NtCreateThreadEx refused, with its status (high bit set,
 *                    so it never collides with the three sentinels above)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdint.h>

#define NT_CURRENT_PROCESS ((HANDLE)(LONG_PTR)-1)
#define NT_CURRENT_THREAD  ((HANDLE)(LONG_PTR)-2)
#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES 0x00000004UL
#define OBJ_INHERIT_ 0x00000002UL
#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 0x00000001UL
#define NT_OK(s) (((NTSTATUS)(s)) >= 0)
#define CHILD_RAN 0x4348494C4452414EULL   /* "CHILDRAN" */

/* ntdll by import; a native image has nothing else to lean on. */
__declspec(dllimport) NTSTATUS NTAPI NtCreateProcessEx(PHANDLE, ACCESS_MASK, PVOID,
	HANDLE, ULONG, HANDLE, HANDLE, HANDLE, ULONG);
__declspec(dllimport) NTSTATUS NTAPI NtCreateThreadEx(PHANDLE, ACCESS_MASK, PVOID,
	HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
__declspec(dllimport) NTSTATUS NTAPI NtCreateSection(PHANDLE, ACCESS_MASK, PVOID,
	PLARGE_INTEGER, ULONG, ULONG, HANDLE);
__declspec(dllimport) NTSTATUS NTAPI NtMapViewOfSection(HANDLE, HANDLE, PVOID *,
	ULONG_PTR, SIZE_T, PLARGE_INTEGER, PSIZE_T, ULONG, ULONG, ULONG);
__declspec(dllimport) NTSTATUS NTAPI NtCreateEvent(PHANDLE, ACCESS_MASK, PVOID, int, BOOLEAN);
__declspec(dllimport) NTSTATUS NTAPI NtSetEvent(HANDLE, PLONG);
__declspec(dllimport) NTSTATUS NTAPI NtWaitForSingleObject(HANDLE, BOOLEAN, PLARGE_INTEGER);
__declspec(dllimport) NTSTATUS NTAPI NtResumeThread(HANDLE, PULONG);
__declspec(dllimport) NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);
__declspec(dllimport) NTSTATUS NTAPI NtTerminateThread(HANDLE, NTSTATUS);

/* -nostdlib means no memset; -O1 emits one for the zeroed struct below. */
void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;
	while (n--) *p++ = (unsigned char) c;
	return d;
}

static volatile uint64_t *g_view;
static HANDLE g_event;

/* Runs on the thread created in the clone. Touches only inherited handles and
 * the shared view; reports through both, then stops itself. */
static DWORD WINAPI worker(LPVOID arg)
{
	(void) arg;
	g_view[0] = CHILD_RAN;
	NtSetEvent(g_event, NULL);
	NtTerminateThread(NT_CURRENT_THREAD, 0);
	return 0;
}

static HANDLE clone_self(NTSTATUS *st)
{
	HANDLE child = NULL;
	NTSTATUS s = NtCreateProcessEx(&child, PROCESS_ALL_ACCESS, NULL,
		NT_CURRENT_PROCESS, PROCESS_CREATE_FLAGS_INHERIT_HANDLES,
		NULL, NULL, NULL, 0);
	*st = s;
	return NT_OK(s) ? child : NULL;
}

void NtProcessStartup(void *param)
{
	(void) param;
	NTSTATUS s;
	HANDLE sec = NULL, child, th = NULL;
	PVOID base = NULL;
	SIZE_T vs = 0x1000;
	LARGE_INTEGER max, timeout;
	OBJECT_ATTRIBUTES oa;

	max.QuadPart = 0x1000;
	NtCreateSection(&sec, SECTION_ALL_ACCESS, NULL, &max, PAGE_READWRITE, SEC_COMMIT, NULL);
	NtMapViewOfSection(sec, NT_CURRENT_PROCESS, &base, 0, 0, NULL, &vs, 1, 0, PAGE_READWRITE);
	g_view = base;
	g_view[0] = 0;

	oa.Length = sizeof oa;
	oa.RootDirectory = NULL;
	oa.ObjectName = NULL;
	oa.Attributes = OBJ_INHERIT_;
	oa.SecurityDescriptor = NULL;
	oa.SecurityQualityOfService = NULL;
	NtCreateEvent(&g_event, EVENT_ALL_ACCESS, &oa, 1 /* NotificationEvent */, FALSE);

	child = clone_self(&s);
	if (!child)
		NtTerminateProcess(NT_CURRENT_PROCESS, (NTSTATUS) 0x00000002);

	s = NtCreateThreadEx(&th, THREAD_ALL_ACCESS, NULL, child,
		(PVOID) worker, NULL, THREAD_CREATE_FLAGS_CREATE_SUSPENDED,
		0, 0, 0, NULL);
	if (!NT_OK(s))
		NtTerminateProcess(NT_CURRENT_PROCESS, s);

	NtResumeThread(th, NULL);
	timeout.QuadPart = -((LONGLONG) 3000 * 10000);
	NtWaitForSingleObject(g_event, FALSE, &timeout);

	if (g_view[0] == CHILD_RAN)
		NtTerminateProcess(NT_CURRENT_PROCESS, (NTSTATUS) 0x00000000);
	NtTerminateProcess(NT_CURRENT_PROCESS, (NTSTATUS) 0x00000001);
}
