/* The ntdll-only process under test. Built twice from this one source, once
 * with --subsystem native and once with --subsystem console, differing only in
 * that PE field; neither links the CRT and neither imports kernel32. It is the
 * lk-host shape from proposal 0011 section 3: a process whose only module at
 * entry is ntdll, doing the kernel's work.
 *
 * It runs the per-question probes (AFD open, RtlWaitOnAddress across two
 * threads, an ALPC port, the loader walk), writes a key=value block to the NT
 * path handed on its command line, and exits through NtTerminateProcess with a
 * status that encodes a summary the parent reads even when the file does not
 * survive. It talks to nothing but ntdll: NtCreateFile is its only output path.
 */
#include "ntchild.h"

/* extra prototypes not carried by winternl.h (NtCreateFile, NtClose,
 * NtWaitForSingleObject, NtDeviceIoControlFile are) */
NTSTATUS NTAPI NtWriteFile(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK,
	PVOID, ULONG, PLARGE_INTEGER, PULONG);
NTSTATUS NTAPI NtDelayExecution(BOOLEAN, PLARGE_INTEGER);

#define FILE_DEVICE_AFD_OPEN L"\\Device\\Afd\\Endpoint"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS) 0x00000000)
#endif
#ifndef STATUS_TIMEOUT
#define STATUS_TIMEOUT ((NTSTATUS) 0x00000102)
#endif
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS) 0xC0000001)
#endif

/* freestanding memory helpers: the CRT is not linked. Named memset/memcpy so a
 * call the compiler synthesises for a struct copy still resolves to ours. */
void *memset(void *d, int c, SIZE_T n)
{
	unsigned char *p = d;
	while (n--) *p++ = (unsigned char) c;
	return d;
}
void *memcpy(void *d, const void *s, SIZE_T n)
{
	unsigned char *pd = d; const unsigned char *ps = s;
	while (n--) *pd++ = *ps++;
	return d;
}
#define mmemset memset
#define mmemcpy memcpy

static PPEB read_peb(void)
{
	PPEB peb;
	__asm__ volatile ("movq %%gs:0x60, %0" : "=r"(peb));
	return peb;
}

/* a byte sink for the transcript, flushed to the file once at the end */
static char out[16384];
static int outn;
static void puts_(const char *s) { while (*s) out[outn++] = *s++; }
static void putc_(char c) { out[outn++] = c; }
static void putkv_i(const char *k, long v)
{
	char t[24]; int i = 0, neg = 0; unsigned long u;
	puts_(k); putc_('=');
	if (v < 0) { neg = 1; u = (unsigned long) -v; } else u = (unsigned long) v;
	if (u == 0) t[i++] = '0';
	while (u) { t[i++] = (char) ('0' + u % 10); u /= 10; }
	if (neg) putc_('-');
	while (i--) putc_(t[i]);
	putc_('\n');
}
static void putkv_x(const char *k, unsigned long long v)
{
	static const char h[] = "0123456789abcdef"; int i;
	puts_(k); puts_("=0x");
	for (i = 60; i >= 0; i -= 4) putc_(h[(v >> i) & 0xf]);
	putc_('\n');
}

/* lowercase ASCII copy of a wide base name; returns 1 if it equals want */
static int wide_is(const UNICODE_STRING *w, const char *want)
{
	int i, n = w->Length / 2;
	for (i = 0; i < n && want[i]; i++) {
		int c = w->Buffer[i];
		if (c >= 'A' && c <= 'Z') c += 32;
		if (c != want[i]) return 0;
	}
	return want[i] == 0 && i == n;
}

/* ---- q6: walk the loader list, record every mapped module -------------- */
static int q6_kernel32, q6_kernelbase, q6_count;
static void q6_modules(PPEB peb)
{
	MY_PEB_LDR_DATA *ldr = (MY_PEB_LDR_DATA *) peb->Ldr;
	MY_LIST_ENTRY *head = &ldr->InLoadOrderModuleList;
	MY_LIST_ENTRY *cur = head->Flink;
	while (cur != head && q6_count < 64) {
		MY_LDR_ENTRY *e = (MY_LDR_ENTRY *) cur;
		UNICODE_STRING *nm = &e->BaseDllName;
		int i, n = nm->Length / 2;
		puts_("mod=");
		for (i = 0; i < n && i < 96; i++) {
			int c = nm->Buffer[i];
			putc_(c < 32 || c > 126 ? '?' : (char) c);
		}
		putc_('\n');
		if (wide_is(nm, "kernel32.dll")) q6_kernel32 = 1;
		if (wide_is(nm, "kernelbase.dll")) q6_kernelbase = 1;
		q6_count++;
		cur = cur->Flink;
	}
}

/* ---- q4: RtlWaitOnAddress / RtlWakeAddressSingle over two threads ------- */
static volatile LONG q4_word;
static LONG q4_compare;
static NTSTATUS q4_wait_status = STATUS_TIMEOUT;
static volatile LONG q4_entered;

static void q4_waiter(void *arg)
{
	LARGE_INTEGER to;
	(void) arg;
	to.QuadPart = -50000000LL;          /* 5 s relative timeout */
	q4_entered = 1;
	q4_wait_status = RtlWaitOnAddress(&q4_word, &q4_compare, sizeof q4_word, &to);
	NtTerminateProcess((HANDLE) -2, 0); /* never reached: thread, not process */
}

static int q4_run(void)
{
	HANDLE th = 0; LARGE_INTEGER d; NTSTATUS st;
	q4_word = 0; q4_compare = 0;
	st = NtCreateThreadEx(&th, 0x1FFFFF, 0, (HANDLE) -1,
		(PVOID) q4_waiter, 0, 0, 0, 0, 0, 0);
	if (st < 0) { putkv_x("q4_thread_ntstatus", (unsigned) st); return -1; }
	/* let the waiter reach RtlWaitOnAddress */
	d.QuadPart = -2000000LL;            /* 0.2 s */
	NtDelayExecution(FALSE, &d);
	q4_word = 1;
	RtlWakeAddressSingle((void *) &q4_word);
	d.QuadPart = -50000000LL;           /* 5 s guard so a hang fails, not hangs */
	NtWaitForSingleObject(th, FALSE, &d);
	NtClose(th);
	return (q4_entered && q4_wait_status == STATUS_SUCCESS) ? 1 : 0;
}

/* ---- q3: AFD ----------------------------------------------------------- */
static NTSTATUS q3_open_status, q3_socket_status, q3_bind_status, q3_poll_status;

static HANDLE afd_bare_open(void)
{
	UNICODE_STRING name; OBJECT_ATTRIBUTES oa; IO_STATUS_BLOCK iosb;
	HANDLE h = 0;
	RtlInitUnicodeString(&name, FILE_DEVICE_AFD_OPEN);
	InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, 0, 0);
	q3_open_status = NtCreateFile(&h, 0x120089 /*generic read|sync*/, &oa, &iosb,
		0, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0, 0, 0);
	return q3_open_status >= 0 ? h : 0;
}

/* an AF_INET/SOCK_STREAM/TCP endpoint through the AFD open packet */
static HANDLE afd_socket_open(void)
{
	UNICODE_STRING name; OBJECT_ATTRIBUTES oa; IO_STATUS_BLOCK iosb;
	HANDLE h = 0;
	unsigned char ea[128];
	FILE_FULL_EA_INFORMATION *fea = (FILE_FULL_EA_INFORMATION *) ea;
	AFD_CREATE_PACKET *cp;
	static const char eaname[] = "AfdOpenPacket";
	static const WCHAR tcp[] = L"\\Device\\Tcp";
	int i;

	mmemset(ea, 0, sizeof ea);
	fea->NextEntryOffset = 0;
	fea->Flags = 0;
	fea->EaNameLength = (UCHAR)(sizeof eaname - 1);
	for (i = 0; i < (int)sizeof eaname; i++) fea->EaName[i] = eaname[i];
	cp = (AFD_CREATE_PACKET *)(fea->EaName + sizeof eaname);
	cp->EndpointFlags = 0;
	cp->GroupID = 0;
	cp->AddressFamily = 2;   /* AF_INET */
	cp->SocketType = 1;      /* SOCK_STREAM */
	cp->Protocol = 6;        /* IPPROTO_TCP */
	cp->SizeOfTransportName = sizeof tcp - sizeof(WCHAR);
	mmemcpy(cp->TransportName, tcp, sizeof tcp);
	fea->EaValueLength = (USHORT)(FIELD_OFFSET(AFD_CREATE_PACKET, TransportName)
		+ sizeof tcp);

	RtlInitUnicodeString(&name, FILE_DEVICE_AFD_OPEN);
	InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, 0, 0);
	q3_socket_status = NtCreateFile(&h,
		0xC0000000 | 0x00100000 /*GENERIC_READ|GENERIC_WRITE|SYNCHRONIZE*/,
		&oa, &iosb, 0, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
		0, ea, (ULONG)(FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName)
		+ fea->EaNameLength + 1 + fea->EaValueLength));
	return q3_socket_status >= 0 ? h : 0;
}

static void q3_run(void)
{
	HANDLE bare, sock;
	q3_open_status = q3_socket_status = q3_bind_status = q3_poll_status = STATUS_UNSUCCESSFUL;
	bare = afd_bare_open();
	if (bare) NtClose(bare);
	sock = afd_socket_open();
	if (sock) {
		IO_STATUS_BLOCK iosb;
		unsigned char bindbuf[32];
		AFD_POLL_INFO pi;
		/* AFD_BIND: share type then a TRANSPORT_ADDRESS for 0.0.0.0:0 */
		mmemset(bindbuf, 0, sizeof bindbuf);
		*(ULONG *) bindbuf = AFD_SHARE_UNIQUE;
		/* TRANSPORT_ADDRESS { TAAddressCount=1; { AddressLength; AddressType;
		 * sockaddr } }; a minimal AF_INET sockaddr at 0 */
		*(ULONG *)(bindbuf + 4) = 1;
		*(USHORT *)(bindbuf + 8) = 14;   /* address length */
		*(USHORT *)(bindbuf + 10) = 2;   /* AF_INET */
		q3_bind_status = NtDeviceIoControlFile(sock, 0, 0, 0, &iosb,
			IOCTL_AFD_BIND, bindbuf, sizeof bindbuf, bindbuf, sizeof bindbuf);
		mmemset(&pi, 0, sizeof pi);
		pi.Timeout.QuadPart = -10000000LL;  /* 1 s */
		pi.NumberOfHandles = 1;
		pi.Handles[0].Handle = sock;
		pi.Handles[0].Events = AFD_POLL_SEND | AFD_POLL_LOCAL_CLOSE;
		q3_poll_status = NtDeviceIoControlFile(sock, 0, 0, 0, &iosb,
			IOCTL_AFD_POLL, &pi, sizeof pi, &pi, sizeof pi);
		NtClose(sock);
	}
}

/* ---- q5: ALPC port ----------------------------------------------------- */
typedef struct _ALPC_PORT_ATTRIBUTES {
	ULONG Flags;
	SECURITY_QUALITY_OF_SERVICE SecurityQos;
	SIZE_T MaxMessageLength;
	SIZE_T MemoryBandwidth, MaxPoolUsage, MaxSectionSize;
	SIZE_T MaxViewSize, MaxTotalSectionSize;
	ULONG DupObjectTypes;
	ULONG Reserved;
} ALPC_PORT_ATTRIBUTES;

static NTSTATUS q5_create_status = STATUS_UNSUCCESSFUL;
static NTSTATUS q5_connect_status = STATUS_UNSUCCESSFUL;
static WCHAR q5_portname[] = L"\\RPC Control\\elfsysv-native-host-spike";

static void q5_client(void *arg)
{
	UNICODE_STRING pn; SIZE_T len = 0; LARGE_INTEGER to;
	HANDLE ch = 0;
	(void) arg;
	RtlInitUnicodeString(&pn, q5_portname);
	to.QuadPart = -20000000LL;          /* 2 s: pending connect fails, not hangs */
	q5_connect_status = NtAlpcConnectPort(&ch, &pn, 0, 0, 0, 0, 0, &len, 0, 0, &to);
	if (ch) NtClose(ch);
	NtTerminateProcess((HANDLE) -2, 0);
}

static int q5_run(void)
{
	UNICODE_STRING pn; OBJECT_ATTRIBUTES oa; ALPC_PORT_ATTRIBUTES pa;
	HANDLE port = 0, th = 0; LARGE_INTEGER d; NTSTATUS st;
	RtlInitUnicodeString(&pn, q5_portname);
	InitializeObjectAttributes(&oa, &pn, 0, 0, 0);
	mmemset(&pa, 0, sizeof pa);
	pa.MaxMessageLength = 328;
	pa.SecurityQos.Length = sizeof pa.SecurityQos;
	pa.SecurityQos.ImpersonationLevel = SecurityImpersonation;
	q5_create_status = NtAlpcCreatePort(&port, &oa, &pa);
	if (q5_create_status < 0) return -1;
	st = NtCreateThreadEx(&th, 0x1FFFFF, 0, (HANDLE) -1,
		(PVOID) q5_client, 0, 0, 0, 0, 0, 0);
	if (st >= 0) {
		d.QuadPart = -30000000LL;       /* 3 s guard */
		NtWaitForSingleObject(th, FALSE, &d);
		NtClose(th);
	}
	NtClose(port);
	return 1;
}

/* ---- entry ------------------------------------------------------------- */

/* last whitespace-separated token of the command line, as the NT output path */
static void find_outpath(PPEB peb, WCHAR *dst, int cap)
{
	UNICODE_STRING *cl = &peb->ProcessParameters->CommandLine;
	int n = cl->Length / 2, i, start = 0, j = 0;
	for (i = 0; i < n; i++) if (cl->Buffer[i] == ' ') start = i + 1;
	for (i = start; i < n && j < cap - 1; i++) dst[j++] = cl->Buffer[i];
	dst[j] = 0;
}

static void flush(PPEB peb)
{
	WCHAR path[512]; UNICODE_STRING name; OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK iosb; HANDLE h = 0; LARGE_INTEGER off;
	find_outpath(peb, path, 512);
	RtlInitUnicodeString(&name, path);
	InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, 0, 0);
	if (NtCreateFile(&h, FILE_GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb, 0,
		FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OVERWRITE_IF,
		FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, 0, 0) >= 0) {
		off.QuadPart = 0;
		NtWriteFile(h, 0, 0, 0, &iosb, out, (ULONG) outn, &off, 0);
		NtClose(h);
	}
}

void __stdcall NtProcessStartup(void *param)
{
	PPEB peb = read_peb();
	int q3ok, q4ok, q5ok;
	unsigned code = 0;
	(void) param;

	q6_modules(peb);
	q4ok = q4_run();
	q3_run();
	q5_run();

	q3ok = (q3_open_status >= 0);
	q5ok = (q5_create_status >= 0);

	putkv_i("q4_rtlwaitonaddress", q4ok);
	putkv_x("q3_afd_open_ntstatus", (unsigned) q3_open_status);
	putkv_x("q3_afd_socket_ntstatus", (unsigned) q3_socket_status);
	putkv_x("q3_afd_bind_ntstatus", (unsigned) q3_bind_status);
	putkv_x("q3_afd_poll_ntstatus", (unsigned) q3_poll_status);
	putkv_x("q5_alpc_create_ntstatus", (unsigned) q5_create_status);
	putkv_x("q5_alpc_connect_ntstatus", (unsigned) q5_connect_status);
	putkv_i("q6_module_count", q6_count);
	putkv_i("q6_kernel32_present", q6_kernel32);
	putkv_i("q6_kernelbase_present", q6_kernelbase);
	putkv_i("child_ran", 1);

	flush(peb);

	/* exit code: a summary the parent reads even if the file is gone.
	 * bit0 ran, bit1 q3 open, bit2 q4, bit3 q5 create, bit4 no kernel32 */
	code |= 1u;
	if (q3ok) code |= 2u;
	if (q4ok) code |= 4u;
	if (q5ok) code |= 8u;
	if (!q6_kernel32) code |= 16u;
	NtTerminateProcess((HANDLE) -1, (NTSTATUS) code);
}
