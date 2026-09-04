/* The NT surface the DrvFs metadata probe needs, declared here rather than
 * taken from a header, because the three classes at the centre of this spike
 * -- FileStatLxInformation, FileCaseSensitiveInformation and
 * FileDispositionInformationEx -- live in ntifs.h, which is a driver header
 * and not in the mingw distribution. Every layout below is written out so the
 * probe measures what it thinks it is asking for; if a field is wrong the
 * status comes back STATUS_INFO_LENGTH_MISMATCH and the transcript says so
 * rather than reporting a plausible number from the wrong offset.
 *
 * Entry points are resolved with GetProcAddress instead of linked against
 * libntdll.a: NtQueryInformationByName is the newest of them and a host
 * without it should report an absent export, not fail to start.
 */
#ifndef LXFS_NT_H
#define LXFS_NT_H

#define WIN32_LEAN_AND_MEAN
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <winternl.h>
#include <ntstatus.h>
#include <stdint.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

/* Information classes. The numbers are the ones ntifs.h assigns; the probe
 * prints the class it used beside each answer so a transcript read on another
 * Windows build can be checked against that build's header. */
#define LXFS_FileStatLxInformation        70
#define LXFS_FileCaseSensitiveInformation 71
#define LXFS_FileDispositionInformationEx 64

/* FILE_STAT_LX_INFORMATION. The six LX fields are the whole reason for the
 * class; everything above them duplicates FileStatInformation. */
typedef struct _LXFS_FILE_STAT_LX_INFORMATION {
	LARGE_INTEGER FileId;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER AllocationSize;
	LARGE_INTEGER EndOfFile;
	ULONG FileAttributes;
	ULONG ReparseTag;
	ULONG NumberOfLinks;
	ACCESS_MASK EffectiveAccess;
	ULONG LxFlags;
	ULONG LxUid;
	ULONG LxGid;
	ULONG LxMode;
	ULONG LxDeviceIdMajor;
	ULONG LxDeviceIdMinor;
} LXFS_FILE_STAT_LX_INFORMATION;

#define LX_FILE_METADATA_HAS_UID       0x1
#define LX_FILE_METADATA_HAS_GID       0x2
#define LX_FILE_METADATA_HAS_MODE      0x4
#define LX_FILE_METADATA_HAS_DEVICE_ID 0x8
#define LX_FILE_CASE_SENSITIVE_DIR     0x10

typedef struct _LXFS_FILE_CASE_SENSITIVE_INFORMATION {
	ULONG Flags;
} LXFS_FILE_CASE_SENSITIVE_INFORMATION;

#ifndef FILE_CS_FLAG_CASE_SENSITIVE_DIR
#define FILE_CS_FLAG_CASE_SENSITIVE_DIR 0x1
#endif

typedef struct _LXFS_FILE_DISPOSITION_INFORMATION_EX {
	ULONG Flags;
} LXFS_FILE_DISPOSITION_INFORMATION_EX;

#define FILE_DISPOSITION_DELETE          0x1
#define FILE_DISPOSITION_POSIX_SEMANTICS 0x2

typedef struct _LXFS_FILE_FULL_EA_INFORMATION {
	ULONG NextEntryOffset;
	UCHAR Flags;
	UCHAR EaNameLength;
	USHORT EaValueLength;
	CHAR EaName[1];
} LXFS_FILE_FULL_EA_INFORMATION;

typedef struct _LXFS_FILE_GET_EA_INFORMATION {
	ULONG NextEntryOffset;
	UCHAR EaNameLength;
	CHAR EaName[1];
} LXFS_FILE_GET_EA_INFORMATION;

/* Reparse tags. LX_SYMLINK is the one constant in this file checked against
 * real source: Cygwin defines it identically at winsup/cygwin/path.cc:1954 and
 * reads it back at :2672. The other three are from ntifs.h as recalled, and
 * the probe reports whichever tag it actually round-tripped. */
#ifndef IO_REPARSE_TAG_LX_SYMLINK
#define IO_REPARSE_TAG_LX_SYMLINK 0xa000001d
#endif
#ifndef IO_REPARSE_TAG_AF_UNIX
#define IO_REPARSE_TAG_AF_UNIX 0x80000023
#endif
#ifndef IO_REPARSE_TAG_LX_FIFO
#define IO_REPARSE_TAG_LX_FIFO 0x80000024
#endif
#ifndef IO_REPARSE_TAG_LX_CHR
#define IO_REPARSE_TAG_LX_CHR 0x80000025
#endif
#ifndef IO_REPARSE_TAG_LX_BLK
#define IO_REPARSE_TAG_LX_BLK 0x80000026
#endif

/* Cygwin's REPARSE_LX_SYMLINK_BUFFER, same shape, same FileType constant of 2
 * that its own comment says to take with a grain of salt. The target is UTF-8
 * and is not NUL terminated. */
typedef struct _LXFS_REPARSE_LX_SYMLINK_BUFFER {
	DWORD ReparseTag;
	WORD ReparseDataLength;
	WORD Reserved;
	struct {
		DWORD FileType;
		char PathBuffer[1];
	} LxSymlinkReparseBuffer;
} LXFS_REPARSE_LX_SYMLINK_BUFFER;

typedef struct _LXFS_REPARSE_BARE_BUFFER {
	DWORD ReparseTag;
	WORD ReparseDataLength;
	WORD Reserved;
	UCHAR DataBuffer[1];
} LXFS_REPARSE_BARE_BUFFER;

#ifndef FSCTL_SET_REPARSE_POINT
#define FSCTL_SET_REPARSE_POINT 0x900a4
#endif
#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT 0x900a8
#endif

typedef NTSTATUS (NTAPI *lxfs_NtQueryInformationFile_t)(
	HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
typedef NTSTATUS (NTAPI *lxfs_NtSetInformationFile_t)(
	HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
typedef NTSTATUS (NTAPI *lxfs_NtQueryInformationByName_t)(
	POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
typedef NTSTATUS (NTAPI *lxfs_NtSetEaFile_t)(
	HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG);
typedef NTSTATUS (NTAPI *lxfs_NtQueryEaFile_t)(
	HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, BOOLEAN, PVOID, ULONG,
	PULONG, BOOLEAN);
typedef NTSTATUS (NTAPI *lxfs_NtFsControlFile_t)(
	HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, ULONG,
	PVOID, ULONG, PVOID, ULONG);
typedef NTSTATUS (NTAPI *lxfs_NtCreateFile_t)(
	PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
	PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);

struct lxfs_nt {
	lxfs_NtQueryInformationFile_t QueryInformationFile;
	lxfs_NtSetInformationFile_t SetInformationFile;
	lxfs_NtQueryInformationByName_t QueryInformationByName;
	lxfs_NtSetEaFile_t SetEaFile;
	lxfs_NtQueryEaFile_t QueryEaFile;
	lxfs_NtFsControlFile_t FsControlFile;
	lxfs_NtCreateFile_t CreateFile;
};

/* Fills nt from ntdll. Returns the number of entry points that did not
 * resolve; the caller reports that rather than dying, because an absent
 * NtQueryInformationByName is itself an answer to q2. */
int lxfs_nt_bind(struct lxfs_nt *nt);

/* Builds "\??\<win32path>" as a UNICODE_STRING over caller storage. buf must
 * hold at least 4 + wcslen(win32path) + 1 wide characters. */
void lxfs_nt_path(UNICODE_STRING *us, wchar_t *buf, size_t buf_wchars,
		  const wchar_t *win32path);

#endif /* LXFS_NT_H */
