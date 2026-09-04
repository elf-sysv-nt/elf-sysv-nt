/* The Win32 parent. It is the ordinary process the supervisor would be: it
 * imports kernel32, and it asks the executive to create the ntdll-only child
 * with NtCreateUserProcess (q1), once for the native-subsystem image and once
 * for the console-subsystem image so the distinction is measured rather than
 * assumed. It then runs the console image through cmd.exe with CreateProcess
 * (q2), and relaunches the native image many times to see it survive the
 * machine's actual security configuration (q7).
 *
 * The child does q3 through q6 inside itself and reports them through a file
 * and through its exit code; this parent reads both and prints the whole
 * key=value block. It measures; the shell reads a verdict from it.
 *
 * Usage:
 *   host-probe [options] NATIVE_EXE CONSOLE_EXE OUT_DIR
 *
 * Options:
 *   -n, --count N   Relaunch the native image N times for q7. [default: 20]
 *   -v, --verbose   Narrate each step on stderr.
 *   -V, --version   Print the version and exit.
 *   -h, --help      Print this message and exit.
 */
#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "ntshared.h"

#define RELEASE "host-probe 1.0"

NTSTATUS NTAPI NtCreateUserProcess(PHANDLE, PHANDLE, ACCESS_MASK, ACCESS_MASK,
	POBJECT_ATTRIBUTES, POBJECT_ATTRIBUTES, ULONG, ULONG,
	PRTL_USER_PROCESS_PARAMETERS, PPS_CREATE_INFO, PPS_ATTRIBUTE_LIST);
NTSTATUS NTAPI RtlCreateProcessParametersEx(PRTL_USER_PROCESS_PARAMETERS *,
	PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING,
	PVOID, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, PUNICODE_STRING, ULONG);
NTSTATUS NTAPI RtlDestroyProcessParameters(PRTL_USER_PROCESS_PARAMETERS);

static int verbose;
static void say(const char *fmt, ...)
{
	va_list ap;
	if (!verbose) return;
	va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

/* Create the ntdll-only child with NtCreateUserProcess. dospath is the image
 * (C:\...), cmdline is its whole command line. Returns the create NTSTATUS;
 * on success the child's exit code lands in *exit. */
static NTSTATUS launch_native(const wchar_t *dospath, const wchar_t *cmdline,
                              DWORD *exitcode)
{
	UNICODE_STRING uImage, uCmd, uNt;
	RTL_USER_PROCESS_PARAMETERS *params = NULL;
	PS_CREATE_INFO ci;
	PS_ATTRIBUTE_LIST al;
	HANDLE hProc = NULL, hThread = NULL;
	wchar_t ntpath[600];
	NTSTATUS st;

	swprintf(ntpath, 600, L"\\??\\%ls", dospath);
	RtlInitUnicodeString(&uImage, dospath);
	RtlInitUnicodeString(&uCmd, cmdline);
	RtlInitUnicodeString(&uNt, ntpath);

	st = RtlCreateProcessParametersEx(&params, &uImage, NULL, NULL, &uCmd,
		NULL, NULL, NULL, NULL, NULL, RTL_USER_PROC_PARAMS_NORMALIZED);
	if (st < 0) { say("  RtlCreateProcessParametersEx: 0x%08lx\n", st); return st; }

	ZeroMemory(&ci, sizeof ci);
	ci.Size = sizeof ci;
	ci.State = PsCreateInitialState;

	ZeroMemory(&al, sizeof al);
	al.TotalLength = sizeof(SIZE_T) + sizeof(PS_ATTRIBUTE);
	al.Attributes[0].Attribute = PS_ATTRIBUTE_IMAGE_NAME;
	al.Attributes[0].Size = uNt.Length;
	al.Attributes[0].u.ValuePtr = uNt.Buffer;

	*exitcode = 0xFFFFFFFF;
	st = NtCreateUserProcess(&hProc, &hThread, PROCESS_ALL_ACCESS,
		THREAD_ALL_ACCESS, NULL, NULL, 0, 0, params, &ci, &al);
	if (st >= 0) {
		WaitForSingleObject(hProc, 20000);
		GetExitCodeProcess(hProc, exitcode);
		CloseHandle(hThread);
		CloseHandle(hProc);
	} else {
		say("  NtCreateUserProcess: 0x%08lx (state %d)\n", st, ci.State);
	}
	RtlDestroyProcessParameters(params);
	return st;
}

/* Read the child's result file and echo its key=value lines to stdout. */
static void echo_child_file(const wchar_t *path)
{
	FILE *f = _wfopen(path, L"rb");
	char buf[4096]; size_t n;
	if (!f) { printf("child_file=absent\n"); return; }
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) fwrite(buf, 1, n, stdout);
	fclose(f);
}

/* Launch an image the way a shell would: cmd /c "<image>" child <ntpath>.
 * Returns 1 if CreateProcess started it; on start, *exit gets the exit code and
 * on failure *err gets GetLastError. */
static int cmd_launch(const wchar_t *image, const wchar_t *outpath,
                      DWORD *exitcode, DWORD *err)
{
	wchar_t line[900]; STARTUPINFOW si; PROCESS_INFORMATION pi; BOOL ok;
	swprintf(line, 900, L"cmd.exe /c \"\"%ls\" child \\??\\%ls\"", image, outpath);
	ZeroMemory(&si, sizeof si); si.cb = sizeof si;
	ZeroMemory(&pi, sizeof pi);
	ok = CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	if (!ok) { *err = GetLastError(); return 0; }
	WaitForSingleObject(pi.hProcess, 20000);
	GetExitCodeProcess(pi.hProcess, exitcode);
	CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
	return 1;
}

int wmain(int argc, wchar_t **argv)
{
	const wchar_t *native = NULL, *console = NULL, *outdir = NULL;
	int count = 20, i, clean = 0, crashed = 0;
	wchar_t out1[700], cmd1[800], cmdc[800];
	DWORD ex_native = 0, ex_console = 0, ex_cmd = 0;
	NTSTATUS st_native, st_console;

	for (i = 1; i < argc; i++) {
		if (!wcscmp(argv[i], L"-h") || !wcscmp(argv[i], L"--help")) {
			printf("Usage:\n  host-probe [options] NATIVE_EXE CONSOLE_EXE OUT_DIR\n");
			return 0;
		} else if (!wcscmp(argv[i], L"-V") || !wcscmp(argv[i], L"--version")) {
			printf("%s\n", RELEASE); return 0;
		} else if (!wcscmp(argv[i], L"-v") || !wcscmp(argv[i], L"--verbose")) {
			verbose = 1;
		} else if (!wcscmp(argv[i], L"-n") || !wcscmp(argv[i], L"--count")) {
			if (++i < argc) count = _wtoi(argv[i]);
		} else if (!native) native = argv[i];
		else if (!console) console = argv[i];
		else if (!outdir) outdir = argv[i];
	}
	if (!native || !console || !outdir) {
		fprintf(stderr, "host-probe: need NATIVE_EXE CONSOLE_EXE OUT_DIR\n");
		return 2;
	}

	/* q1: native-subsystem image, then console-subsystem image */
	swprintf(out1, 700, L"%ls\\child-native.txt", outdir);
	swprintf(cmd1, 800, L"child \\??\\%ls", out1);
	say("q1: NtCreateUserProcess on the native-subsystem image\n");
	st_native = launch_native(native, cmd1, &ex_native);

	swprintf(out1, 700, L"%ls\\child-console.txt", outdir);
	swprintf(cmdc, 800, L"child \\??\\%ls", out1);
	say("q1: NtCreateUserProcess on the console-subsystem image\n");
	st_console = launch_native(console, cmdc, &ex_console);

	/* q2: each image through cmd.exe with CreateProcess. cmd builds a Win32
	 * process the classic way, so this asks whether a shell can start the
	 * ntdll-only image at all. Both shapes are tried: the finding is in which
	 * one cmd will run. */
	{
		DWORD nat_err = 0, con_err = 0, ex_nat_cmd = 0;
		int nat_ok, con_ok;
		swprintf(out1, 700, L"%ls\\child-cmd-native.txt", outdir);
		nat_ok = cmd_launch(native, out1, &ex_nat_cmd, &nat_err);
		swprintf(out1, 700, L"%ls\\child-cmd-console.txt", outdir);
		con_ok = cmd_launch(console, out1, &ex_cmd, &con_err);
		printf("q2_cmd_native_started=%d\n", nat_ok);
		printf("q2_cmd_native_lasterror=%lu\n", nat_err);
		printf("q2_cmd_native_exit=%lu\n", ex_nat_cmd);
		printf("q2_cmd_console_started=%d\n", con_ok);
		printf("q2_cmd_console_lasterror=%lu\n", con_err);
		printf("q2_cmd_console_exit=%lu\n", ex_cmd);
	}

	/* q7: relaunch the native image, count clean exits */
	say("q7: %d relaunches of the native image\n", count);
	for (i = 0; i < count; i++) {
		DWORD e = 0; NTSTATUS s;
		swprintf(out1, 700, L"%ls\\child-run.txt", outdir);
		swprintf(cmd1, 800, L"child \\??\\%ls", out1);
		s = launch_native(native, cmd1, &e);
		if (s >= 0 && (e & 1) && e < 0x100) clean++;
		else crashed++;
	}

	/* the block */
	printf("host_windows=10.0.26200\n");
	printf("q1_native_subsystem_ntstatus=0x%08lx\n", (unsigned long) st_native);
	printf("q1_native_subsystem_exit=%lu\n", ex_native);
	printf("q1_console_subsystem_ntstatus=0x%08lx\n", (unsigned long) st_console);
	printf("q1_console_subsystem_exit=%lu\n", ex_console);
	printf("q7_launches=%d\n", count);
	printf("q7_clean=%d\n", clean);
	printf("q7_crashed=%d\n", crashed);

	/* the child's own report, from the native run */
	swprintf(out1, 700, L"%ls\\child-native.txt", outdir);
	echo_child_file(out1);
	return 0;
}
