/* Declarations the ntdll-only child needs and winternl.h does not carry: the
 * loader list at its real width, the AFD open packet and IOCTLs, the ALPC and
 * thread and synchronisation prototypes. Field names follow the published
 * interfaces (PEB loader data, msafd/wepoll AFD, the ALPC port API); nothing
 * is copied from a GPL implementation.
 */
#ifndef NTCHILD_H
#define NTCHILD_H

#include "ntshared.h"

/* ---- loader walk (real widths, not winternl's Reserved padding) -------- */

typedef struct _MY_LIST_ENTRY { struct _MY_LIST_ENTRY *Flink, *Blink; } MY_LIST_ENTRY;

typedef struct _MY_PEB_LDR_DATA {
	ULONG Length;
	BOOLEAN Initialized;
	PVOID SsHandle;
	MY_LIST_ENTRY InLoadOrderModuleList;
	MY_LIST_ENTRY InMemoryOrderModuleList;
	MY_LIST_ENTRY InInitializationOrderModuleList;
} MY_PEB_LDR_DATA;

typedef struct _MY_LDR_ENTRY {
	MY_LIST_ENTRY InLoadOrderLinks;
	MY_LIST_ENTRY InMemoryOrderLinks;
	MY_LIST_ENTRY InInitializationOrderLinks;
	PVOID DllBase;
	PVOID EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
} MY_LDR_ENTRY;

/* ---- AFD, from the msafd/wepoll interface and phnt's ntafd.h ----------- */

/* The open packet afd.sys expects in the EaBuffer of NtCreateFile, wrapped in
 * a FILE_FULL_EA_INFORMATION whose EaName is the literal "AfdOpenPacketXX".
 * The modern (Win8+) shape carries the address family, socket type and
 * protocol before the transport device name, which a TDI-mode create appends
 * as a counted, NUL-terminated string (\Device\Tcp for TCP over IPv4). */
typedef struct _AFD_OPEN_PACKET {
	ULONG EndpointFlags;
	ULONG GroupID;
	LONG  AddressFamily;
	LONG  SocketType;
	LONG  Protocol;
	ULONG TransportDeviceNameLength;   /* bytes, excludes the NUL */
	WCHAR TransportDeviceName[1];
} AFD_OPEN_PACKET;

/* The TDI address the bind/connect/getsockname IOCTLs carry for AF_INET. The
 * IP part is byte-packed: in_addr sits unaligned at offset 2, sin_zero at 6,
 * so the whole thing is 14 bytes, not a padded 16. */
#pragma pack(push, 1)
typedef struct _TDI_ADDRESS_IP {
	USHORT sin_port;   /* network byte order */
	ULONG  in_addr;    /* network byte order */
	UCHAR  sin_zero[8];
} TDI_ADDRESS_IP;
#pragma pack(pop)
typedef struct _TA_ADDRESS {
	USHORT AddressLength;
	USHORT AddressType;
	UCHAR  Address[1];
} TA_ADDRESS;
typedef struct _TRANSPORT_ADDRESS {
	LONG TAAddressCount;
	TA_ADDRESS Address[1];
} TRANSPORT_ADDRESS;
#define TDI_ADDRESS_TYPE_IP 2

typedef struct _AFD_LISTEN_DATA {
	BOOLEAN UseSAN;
	ULONG   Backlog;
	BOOLEAN UseDelayedAcceptance;
} AFD_LISTEN_DATA;

typedef struct _AFD_ACCEPT_DATA {
	ULONG  UseSAN;
	ULONG  SequenceNumber;
	HANDLE ListenHandle;   /* handle of a fresh, unbound endpoint */
} AFD_ACCEPT_DATA;

/* the counted buffer AFD_SEND_INFO/AFD_RECV_INFO point at; note len before
 * buf, the reverse of Winsock's WSABUF */
typedef struct _AFD_WSABUF { UINT len; PCHAR buf; } AFD_WSABUF;
typedef struct _AFD_SEND_RECV_INFO {
	AFD_WSABUF *BufferArray;
	ULONG BufferCount;
	ULONG AfdFlags;
	ULONG TdiFlags;
} AFD_SEND_RECV_INFO;

typedef struct _AFD_POLL_HANDLE_INFO {
	HANDLE   Handle;
	ULONG    PollEvents;
	NTSTATUS Status;
} AFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
	LARGE_INTEGER Timeout;
	ULONG NumberOfHandles;
	ULONG Exclusive;
	AFD_POLL_HANDLE_INFO Handles[4];
} AFD_POLL_INFO;

/* AFD IOCTL codes (FILE_DEVICE_NETWORK, operation<<2 | method) */
#define IOCTL_AFD_BIND            0x00012003
#define IOCTL_AFD_CONNECT         0x00012007
#define IOCTL_AFD_START_LISTEN    0x0001200B
#define IOCTL_AFD_WAIT_FOR_LISTEN 0x0001200C
#define IOCTL_AFD_ACCEPT          0x00012010
#define IOCTL_AFD_RECV            0x00012017
#define IOCTL_AFD_SEND            0x0001201F
#define IOCTL_AFD_POLL            0x00012024
#define IOCTL_AFD_GET_SOCK_NAME   0x0001202F

/* modern afd.sys AFD_POLL event bits (wepoll) */
#define AFD_POLL_RECEIVE       0x0001
#define AFD_POLL_SEND          0x0004
#define AFD_POLL_DISCONNECT    0x0008
#define AFD_POLL_ABORT         0x0010
#define AFD_POLL_LOCAL_CLOSE   0x0020
#define AFD_POLL_CONNECT       0x0040
#define AFD_POLL_ACCEPT        0x0080
#define AFD_POLL_CONNECT_FAIL  0x0100

#define AFD_SHARE_UNIQUE   0
#define AFD_SHARE_WILDCARD 2
#define TDI_RECEIVE_NORMAL 0x20

/* ---- prototypes the child links from ntdll ----------------------------- */

NTSTATUS NTAPI NtCreateThreadEx(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, HANDLE,
	PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
NTSTATUS NTAPI NtCreateEvent(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, ULONG, BOOLEAN);
NTSTATUS NTAPI RtlWaitOnAddress(volatile const void *, const void *, SIZE_T,
	const LARGE_INTEGER *);
NTSTATUS NTAPI RtlWakeAddressSingle(const void *);
NTSTATUS NTAPI NtAlpcCreatePort(PHANDLE, POBJECT_ATTRIBUTES, PVOID);
NTSTATUS NTAPI NtAlpcConnectPort(PHANDLE, PUNICODE_STRING, POBJECT_ATTRIBUTES,
	PVOID, ULONG, PVOID, PVOID, PSIZE_T, PVOID, PVOID, PLARGE_INTEGER);
NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);

#endif /* NTCHILD_H */
