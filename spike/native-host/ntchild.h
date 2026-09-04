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

/* ---- AFD, from the published msafd/wepoll interface -------------------- */

typedef struct _AFD_CREATE_PACKET {
	ULONG EndpointFlags;
	ULONG GroupID;
	ULONG AddressFamily;
	ULONG SocketType;
	ULONG Protocol;
	ULONG SizeOfTransportName;
	WCHAR TransportName[1];
} AFD_CREATE_PACKET;

typedef struct _AFD_POLL_HANDLE_INFO {
	HANDLE Handle;
	ULONG Events;
	NTSTATUS Status;
} AFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
	LARGE_INTEGER Timeout;
	ULONG NumberOfHandles;
	ULONG Exclusive;
	AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO;

/* well-known AFD IOCTL codes (FILE_DEVICE_NETWORK, METHOD_NEITHER) */
#define IOCTL_AFD_BIND 0x00012003
#define IOCTL_AFD_POLL 0x00012024

#define AFD_POLL_RECEIVE       0x0001
#define AFD_POLL_SEND          0x0004
#define AFD_POLL_CONNECT       0x0010
#define AFD_POLL_ACCEPT        0x0080
#define AFD_POLL_LOCAL_CLOSE   0x0008

#define AFD_SHARE_UNIQUE 0
#define AFD_ENDPOINT_MESSAGE_ORIENTED 0x10

/* ---- prototypes the child links from ntdll ----------------------------- */

NTSTATUS NTAPI NtCreateThreadEx(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, HANDLE,
	PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
NTSTATUS NTAPI RtlWaitOnAddress(volatile const void *, const void *, SIZE_T,
	const LARGE_INTEGER *);
NTSTATUS NTAPI RtlWakeAddressSingle(const void *);
NTSTATUS NTAPI NtAlpcCreatePort(PHANDLE, POBJECT_ATTRIBUTES, PVOID);
NTSTATUS NTAPI NtAlpcConnectPort(PHANDLE, PUNICODE_STRING, POBJECT_ATTRIBUTES,
	PVOID, ULONG, PVOID, PVOID, PSIZE_T, PVOID, PVOID, PLARGE_INTEGER);
NTSTATUS NTAPI NtTerminateProcess(HANDLE, NTSTATUS);

#endif /* NTCHILD_H */
