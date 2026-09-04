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
#ifndef STATUS_PENDING
#define STATUS_PENDING ((NTSTATUS) 0x00000103)
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

/* ---- q3: AFD sockets and readiness ------------------------------------- */
/* A loopback TCP round trip inside this one ntdll-only process: two endpoints
 * created through the AFD open packet, bound, listened, connected, accepted,
 * a message sent across, and IOCTL_AFD_POLL read for readiness on each edge.
 * The open packet's EaName is the literal "AfdOpenPacketXX"; the create runs
 * in TDI form with \Device\Tcp as the transport, which is the shape this
 * build's afd.sys accepts. The connect uses the modern AFD_CONNECT_JOIN_INFO
 * (two endpoint handles ahead of the address), not the older AFD_CONNECT_INFO
 * -- afd.sys refuses the latter here with STATUS_INVALID_PARAMETER. */
static NTSTATUS q3a_create = STATUS_UNSUCCESSFUL, q3b_bind = STATUS_UNSUCCESSFUL;
static NTSTATUS q3b_getsock = STATUS_UNSUCCESSFUL, q3c_listen = STATUS_UNSUCCESSFUL;
static NTSTATUS q3c_wait = STATUS_UNSUCCESSFUL, q3c_accept = STATUS_UNSUCCESSFUL;
static volatile NTSTATUS q3c_connect = STATUS_UNSUCCESSFUL;
static NTSTATUS q3c_send = STATUS_UNSUCCESSFUL, q3c_recv = STATUS_UNSUCCESSFUL;
static NTSTATUS q3d_before_st = STATUS_UNSUCCESSFUL;
static USHORT q3b_port;
static long q3c_send_bytes, q3c_recv_bytes;
static int q3c_recv_match;
static ULONG q3d_notready_ev, q3d_read_ev, q3d_write_ev;
static ULONG q3e_after1_ev, q3e_after2_ev, q3e_drained_ev;

static HANDLE g_client;
static USHORT g_port_be;

static ULONG afd_build_ea(unsigned char *buf)
{
	FILE_FULL_EA_INFORMATION *fea = (FILE_FULL_EA_INFORMATION *) buf;
	static const char nm[] = "AfdOpenPacketXX";   /* 15 chars, NUL after */
	static const WCHAR tcp[] = L"\\Device\\Tcp";
	AFD_OPEN_PACKET *op;
	ULONG namelen = 15, valuelen, i;
	fea->NextEntryOffset = 0;
	fea->Flags = 0;
	fea->EaNameLength = (UCHAR) namelen;
	for (i = 0; i < namelen; i++) fea->EaName[i] = nm[i];
	fea->EaName[namelen] = 0;
	op = (AFD_OPEN_PACKET *)(fea->EaName + namelen + 1);
	op->EndpointFlags = 0;
	op->GroupID = 0;
	op->AddressFamily = 2;   /* AF_INET */
	op->SocketType = 1;      /* SOCK_STREAM */
	op->Protocol = 6;        /* IPPROTO_TCP */
	op->TransportDeviceNameLength = sizeof tcp - sizeof(WCHAR);
	mmemcpy(op->TransportDeviceName, tcp, sizeof tcp);
	valuelen = FIELD_OFFSET(AFD_OPEN_PACKET, TransportDeviceName) + sizeof tcp;
	fea->EaValueLength = (USHORT) valuelen;
	return FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + namelen + 1 + valuelen;
}

static HANDLE afd_create(NTSTATUS *st)
{
	UNICODE_STRING name; OBJECT_ATTRIBUTES oa; IO_STATUS_BLOCK iosb;
	HANDLE h = 0; unsigned char ea[96]; ULONG ealen;
	mmemset(ea, 0, sizeof ea);
	ealen = afd_build_ea(ea);
	RtlInitUnicodeString(&name, FILE_DEVICE_AFD_OPEN);
	InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_INHERIT, 0, 0);
	*st = NtCreateFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &iosb,
		0, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF, 0, ea, ealen);
	return (*st >= 0) ? h : 0;
}

/* an AFD IOCTL driven to completion through its own event; the endpoint is not
 * opened for synchronous file I/O, so a pending request is waited on here */
static NTSTATUS afd_call(HANDLE h, ULONG code, void *in, ULONG il,
                         void *o, ULONG ol, IO_STATUS_BLOCK *iosb)
{
	HANDLE ev = 0; NTSTATUS st; LARGE_INTEGER to;
	NtCreateEvent(&ev, EVENT_ALL_ACCESS, 0, 1 /*SynchronizationEvent*/, 0);
	st = NtDeviceIoControlFile(h, ev, 0, 0, iosb, code, in, il, o, ol);
	if (st == STATUS_PENDING) {
		to.QuadPart = -50000000LL;   /* 5 s: a stuck request fails, not hangs */
		NtWaitForSingleObject(ev, FALSE, &to);
		st = iosb->Status;
	}
	if (ev) NtClose(ev);
	return st;
}

/* lay a TRANSPORT_ADDRESS for addr_be:port_be into dst; return its 22 bytes */
static ULONG afd_put_addr(unsigned char *dst, USHORT port_be, ULONG addr_be)
{
	TRANSPORT_ADDRESS *ta = (TRANSPORT_ADDRESS *) dst; TDI_ADDRESS_IP *ip;
	ta->TAAddressCount = 1;
	ta->Address[0].AddressLength = 14;
	ta->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
	ip = (TDI_ADDRESS_IP *) ta->Address[0].Address;
	ip->sin_port = port_be;
	ip->in_addr = addr_be;
	mmemset(ip->sin_zero, 0, 8);
	return 4 + 4 + 14;
}

/* bind h to addr_be:0; when port_out is set, read the assigned port back */
static NTSTATUS afd_bind(HANDLE h, ULONG addr_be, USHORT *port_out)
{
	unsigned char bb[80], gb[80]; IO_STATUS_BLOCK iosb; NTSTATUS st; ULONG n;
	mmemset(bb, 0, sizeof bb);
	*(ULONG *) bb = AFD_SHARE_WILDCARD;
	n = 4 + afd_put_addr(bb + 4, 0, addr_be);
	st = afd_call(h, IOCTL_AFD_BIND, bb, n, bb, sizeof bb, &iosb);
	if (st < 0 || !port_out) return st;
	mmemset(gb, 0, sizeof gb);
	q3b_getsock = afd_call(h, IOCTL_AFD_GET_SOCK_NAME, 0, 0, gb, sizeof gb, &iosb);
	/* out: ActivityCount(4) count(4) len(2) type(2) port(2) addr(4) ... */
	*port_out = *(USHORT *)(gb + 12);
	return st;
}

/* poll one endpoint for the given events with a short timeout; return the
 * signalled mask afd.sys writes back */
static ULONG afd_poll(HANDLE h, ULONG events, LONGLONG timeout, NTSTATUS *st)
{
	AFD_POLL_INFO pi; IO_STATUS_BLOCK iosb;
	mmemset(&pi, 0, sizeof pi);
	pi.Timeout.QuadPart = timeout;
	pi.NumberOfHandles = 1;
	pi.Exclusive = 0;
	pi.Handles[0].Handle = h;
	pi.Handles[0].PollEvents = events;
	*st = afd_call(h, IOCTL_AFD_POLL, &pi, sizeof pi, &pi, sizeof pi, &iosb);
	/* on a timeout afd.sys returns NumberOfHandles==0 and leaves the handle's
	 * PollEvents holding the stale request mask; a zero count is "not ready" */
	return (pi.NumberOfHandles >= 1) ? pi.Handles[0].PollEvents : 0;
}

static void q3_connect_thread(void *arg)
{
	unsigned char cb[96]; IO_STATUS_BLOCK iosb; ULONG n;
	(void) arg;
	mmemset(cb, 0, sizeof cb);
	/* AFD_CONNECT_JOIN_INFO: BOOLEAN SanActive; HANDLE Root; HANDLE Connect;
	 * TRANSPORT_ADDRESS RemoteAddress -- the address begins at offset 24 */
	n = 24 + afd_put_addr(cb + 24, g_port_be, 0x0100007F);
	q3c_connect = afd_call(g_client, IOCTL_AFD_CONNECT, cb, n, 0, 0, &iosb);
	NtTerminateProcess((HANDLE) -2, 0);
}

static void q3_run(void)
{
	NTSTATUS st; HANDLE L, A = 0; IO_STATUS_BLOCK iosb; HANDLE cth = 0;
	LARGE_INTEGER d; USHORT port_be = 0;

	L = afd_create(&q3a_create);
	g_client = afd_create(&st);
	if (!L || !g_client) return;

	/* q3b: bind the listener to 127.0.0.1:0 and read the assigned port back */
	q3b_bind = afd_bind(L, 0x0100007F, &port_be);
	q3b_port = (USHORT)((port_be >> 8) | (port_be << 8));
	afd_bind(g_client, 0x00000000, 0);   /* client on the wildcard address */

	{
		AFD_LISTEN_DATA ld;
		mmemset(&ld, 0, sizeof ld);
		ld.Backlog = 5;
		q3c_listen = afd_call(L, IOCTL_AFD_START_LISTEN, &ld, sizeof ld, 0, 0, &iosb);
	}

	/* the connect runs on a second thread while this one waits to accept */
	g_port_be = port_be;
	NtCreateThreadEx(&cth, 0x1FFFFF, 0, (HANDLE) -1,
		(PVOID) q3_connect_thread, 0, 0, 0, 0, 0, 0);

	{
		unsigned char rad[64];
		mmemset(rad, 0, sizeof rad);
		q3c_wait = afd_call(L, IOCTL_AFD_WAIT_FOR_LISTEN, 0, 0, rad, sizeof rad, &iosb);
		A = afd_create(&st);
		if (A) {
			AFD_ACCEPT_DATA ad;
			mmemset(&ad, 0, sizeof ad);
			ad.SequenceNumber = *(ULONG *) rad;   /* first field of the reply */
			ad.ListenHandle = A;
			q3c_accept = afd_call(L, IOCTL_AFD_ACCEPT, &ad, sizeof ad, 0, 0, &iosb);
		}
	}
	d.QuadPart = -20000000LL;
	NtWaitForSingleObject(cth, FALSE, &d);
	NtClose(cth);

	/* q3d: the connected client is writable; the fresh server side has no data
	 * yet, so a receive poll on it is not ready before the peer sends */
	q3d_write_ev = afd_poll(g_client, AFD_POLL_SEND, -2000000LL, &st);
	q3d_notready_ev = afd_poll(A, AFD_POLL_RECEIVE, -2000000LL, &q3d_before_st);

	/* q3c: send from the client */
	{
		AFD_WSABUF wb; AFD_SEND_RECV_INFO si;
		static char msg[] = "ping-AFD";
		wb.len = 8; wb.buf = msg;
		mmemset(&si, 0, sizeof si);
		si.BufferArray = &wb; si.BufferCount = 1;
		q3c_send = afd_call(g_client, IOCTL_AFD_SEND, &si, sizeof si, 0, 0, &iosb);
		q3c_send_bytes = (long) iosb.Information;
	}
	d.QuadPart = -2000000LL;
	NtDelayExecution(FALSE, &d);

	/* q3d/q3e: the server side is now readable; the same poll repeated returns
	 * the same readiness while the data sits unread (level-triggered) */
	q3d_read_ev = afd_poll(A, AFD_POLL_RECEIVE, -2000000LL, &st);
	q3e_after1_ev = afd_poll(A, AFD_POLL_RECEIVE, -2000000LL, &st);
	q3e_after2_ev = afd_poll(A, AFD_POLL_RECEIVE, -2000000LL, &st);

	/* q3c: receive on the server side and check the bytes crossed intact */
	{
		AFD_WSABUF wb; AFD_SEND_RECV_INFO ri; char rbuf[64];
		static const char exp[] = "ping-AFD"; int i, ok = 1;
		mmemset(rbuf, 0, sizeof rbuf);
		wb.len = sizeof rbuf; wb.buf = rbuf;
		mmemset(&ri, 0, sizeof ri);
		ri.BufferArray = &wb; ri.BufferCount = 1; ri.TdiFlags = TDI_RECEIVE_NORMAL;
		q3c_recv = afd_call(A, IOCTL_AFD_RECV, &ri, sizeof ri, 0, 0, &iosb);
		q3c_recv_bytes = (long) iosb.Information;
		for (i = 0; i < 8; i++) if (rbuf[i] != exp[i]) ok = 0;
		q3c_recv_match = ok;
	}

	/* q3e: once drained the receive poll reports not ready again */
	q3e_drained_ev = afd_poll(A, AFD_POLL_RECEIVE, -2000000LL, &st);

	if (A) NtClose(A);
	NtClose(g_client);
	NtClose(L);
}

/* q3f: re-walk the loader list after all the AFD work, to confirm the socket
 * round trip pulled nothing new in behind it */
static int q3f_count, q3f_kernel32, q3f_kernelbase;
static void q3f_modules(PPEB peb)
{
	MY_PEB_LDR_DATA *ldr = (MY_PEB_LDR_DATA *) peb->Ldr;
	MY_LIST_ENTRY *head = &ldr->InLoadOrderModuleList;
	MY_LIST_ENTRY *cur = head->Flink;
	while (cur != head && q3f_count < 64) {
		MY_LDR_ENTRY *e = (MY_LDR_ENTRY *) cur;
		if (wide_is(&e->BaseDllName, "kernel32.dll")) q3f_kernel32 = 1;
		if (wide_is(&e->BaseDllName, "kernelbase.dll")) q3f_kernelbase = 1;
		q3f_count++;
		cur = cur->Flink;
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
	q3f_modules(peb);

	q3ok = (q3a_create >= 0);
	q5ok = (q5_create_status >= 0);

	putkv_i("q4_rtlwaitonaddress", q4ok);
	/* q3a-q3f: the AFD socket round trip and its readiness poll */
	putkv_x("q3a_create_ntstatus", (unsigned) q3a_create);
	putkv_x("q3b_bind_ntstatus", (unsigned) q3b_bind);
	putkv_x("q3b_getsockname_ntstatus", (unsigned) q3b_getsock);
	putkv_i("q3b_port", q3b_port);
	putkv_x("q3c_listen_ntstatus", (unsigned) q3c_listen);
	putkv_x("q3c_connect_ntstatus", (unsigned) q3c_connect);
	putkv_x("q3c_wait_ntstatus", (unsigned) q3c_wait);
	putkv_x("q3c_accept_ntstatus", (unsigned) q3c_accept);
	putkv_x("q3c_send_ntstatus", (unsigned) q3c_send);
	putkv_i("q3c_send_bytes", q3c_send_bytes);
	putkv_x("q3c_recv_ntstatus", (unsigned) q3c_recv);
	putkv_i("q3c_recv_bytes", q3c_recv_bytes);
	putkv_i("q3c_recv_match", q3c_recv_match);
	putkv_x("q3d_poll_before_ntstatus", (unsigned) q3d_before_st);
	putkv_x("q3d_poll_notready_ev", q3d_notready_ev);
	putkv_x("q3d_poll_read_ready_ev", q3d_read_ev);
	putkv_x("q3d_poll_write_ready_ev", q3d_write_ev);
	putkv_x("q3e_poll_after1_ev", q3e_after1_ev);
	putkv_x("q3e_poll_after2_ev", q3e_after2_ev);
	putkv_x("q3e_poll_drained_ev", q3e_drained_ev);
	putkv_x("q5_alpc_create_ntstatus", (unsigned) q5_create_status);
	putkv_x("q5_alpc_connect_ntstatus", (unsigned) q5_connect_status);
	putkv_i("q6_module_count", q6_count);
	putkv_i("q6_kernel32_present", q6_kernel32);
	putkv_i("q6_kernelbase_present", q6_kernelbase);
	putkv_i("q3f_module_count", q3f_count);
	putkv_i("q3f_kernel32_present", q3f_kernel32);
	putkv_i("q3f_kernelbase_present", q3f_kernelbase);
	putkv_i("child_ran", 1);

	flush(peb);

	/* exit code: a summary the parent reads even if the file is gone.
	 * bit0 ran, bit1 q3 endpoint, bit2 q4, bit3 q5 create, bit4 no kernel32 */
	code |= 1u;
	if (q3ok) code |= 2u;
	if (q4ok) code |= 4u;
	if (q5ok) code |= 8u;
	if (!q6_kernel32) code |= 16u;
	NtTerminateProcess((HANDLE) -1, (NTSTATUS) code);
}
