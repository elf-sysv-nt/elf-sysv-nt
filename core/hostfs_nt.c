/*
 * hostfs_nt.c -- the NT file store beneath the VFS.  substrate-line: below
 *
 * This is the band of host glue that exists so that nothing above it has to
 * name an NT call: every hostfs.h operation lands here as one or two ntdll
 * calls on a handle, with the LX metadata WSL defined (the $LXUID, $LXGID,
 * $LXMOD and $LXDEV extended attributes; the LX_SYMLINK, LX_FIFO, LX_CHR,
 * LX_BLK and AF_UNIX reparse tags) read and written the way spike 39 measured
 * this volume doing it.  Entry points ntdll exports but winternl.h does not
 * declare are resolved by name once, so a host without one reports an absent
 * call rather than failing to link.
 *
 * Two rules from 0011 § 8 are kept here rather than in the VFS because they
 * are about NT and not about Linux.  Every open asks for all three share modes,
 * so a Linux program never sees a sharing violation from another Linux program.
 * Deletion and rename use NTFS's POSIX semantics, so an open file can lose its
 * name and a rename can replace an open target; where the volume refuses the
 * flag the older disposition is used and the caller's errno says what happened.
 */
#define WIN32_LEAN_AND_MEAN
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <winternl.h>
#include <ntstatus.h>

#include <stdlib.h>
#include <string.h>

#include "hostfs.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

#include "lxerrno.h"

/* ---- the NT surface winternl.h leaves out --------------------------------- */

#define FileBasicInformation_		4
#define FileStandardInformation_	5
#define FileLinkInformation_		11
#define FileDispositionInformation_	13
#define FileEndOfFileInformation_	20
#define FileIdBothDirectoryInformation_	37
#define FileDispositionInformationEx_	64
#define FileRenameInformationEx_	65
#define FileStatLxInformation_		70
#define FileCaseSensitiveInformation_	71

#define FileFsVolumeInformation_	1
#define FileFsAttributeInformation_	5

#define FILE_DISPOSITION_DELETE_		0x1
#define FILE_DISPOSITION_POSIX_SEMANTICS_	0x2
#define FILE_RENAME_REPLACE_IF_EXISTS_		0x1
#define FILE_RENAME_POSIX_SEMANTICS_		0x2
#define FILE_CS_FLAG_CASE_SENSITIVE_DIR_	0x1

#define FILE_SUPPORTS_EXTENDED_ATTRIBUTES_	0x00800000
#define FILE_SUPPORTS_POSIX_UNLINK_RENAME_	0x00000400

#ifndef FSCTL_SET_REPARSE_POINT
#define FSCTL_SET_REPARSE_POINT 0x900a4
#endif
#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT 0x900a8
#endif
#define TAG_LX_SYMLINK	0xa000001dU
#define TAG_AF_UNIX	0x80000023U
#define TAG_LX_FIFO	0x80000024U
#define TAG_LX_CHR	0x80000025U
#define TAG_LX_BLK	0x80000026U
#define TAG_SYMLINK	0xa000000cU
#define TAG_MOUNT_POINT	0xa0000003U

#define STATUS_IO_REPARSE_TAG_NOT_HANDLED_	((NTSTATUS)0xC0000279L)
#define STATUS_DIRECTORY_NOT_EMPTY_		((NTSTATUS)0xC0000101L)
#define STATUS_CANNOT_DELETE_			((NTSTATUS)0xC0000121L)
#define STATUS_NOT_A_REPARSE_POINT_		((NTSTATUS)0xC0000275L)
#define STATUS_FILE_IS_A_DIRECTORY_		((NTSTATUS)0xC00000BAL)
#define STATUS_NOT_A_DIRECTORY_			((NTSTATUS)0xC0000103L)
#define STATUS_TOO_MANY_LINKS_			((NTSTATUS)0xC0000265L)
#define STATUS_DISK_FULL_			((NTSTATUS)0xC000007FL)
#define STATUS_EAS_NOT_SUPPORTED_		((NTSTATUS)0xC000004FL)
#define STATUS_INVALID_INFO_CLASS_		((NTSTATUS)0xC0000003L)
#define STATUS_NOT_SUPPORTED_			((NTSTATUS)0xC00000BBL)
#define STATUS_NOT_SAME_DEVICE_			((NTSTATUS)0xC00000D4L)
#define STATUS_NO_MORE_FILES_			((NTSTATUS)0x80000006L)
#define STATUS_NO_SUCH_FILE_			((NTSTATUS)0xC000000FL)
#define STATUS_MEDIA_WRITE_PROTECTED_		((NTSTATUS)0xC00000A2L)
#define STATUS_FILE_CORRUPT_ERROR_		((NTSTATUS)0xC0000102L)
#define STATUS_NAME_TOO_LONG_			((NTSTATUS)0xC0000106L)
#define STATUS_INVALID_PARAMETER_		((NTSTATUS)0xC000000DL)

typedef struct {
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
} FILE_STAT_LX_INFORMATION_;

#define LX_HAS_UID	0x1
#define LX_HAS_GID	0x2
#define LX_HAS_MODE	0x4
#define LX_HAS_DEV	0x8
#define LX_CASE_DIR	0x10

typedef struct {
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	ULONG FileAttributes;
} FILE_BASIC_INFORMATION_;

typedef struct {
	LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFORMATION_;

typedef struct {
	ULONG Flags;
} FILE_FLAGS_INFORMATION_;	/* disposition-ex and case-sensitive share the shape */

typedef struct {
	BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFORMATION_;

typedef struct {
	ULONG Flags;
	HANDLE RootDirectory;
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_RENAME_INFORMATION_EX_;

typedef struct {
	BOOLEAN ReplaceIfExists;
	HANDLE RootDirectory;
	ULONG FileNameLength;
	WCHAR FileName[1];
} FILE_LINK_INFORMATION_;

typedef struct {
	ULONG NextEntryOffset;
	ULONG FileIndex;
	LARGE_INTEGER CreationTime;
	LARGE_INTEGER LastAccessTime;
	LARGE_INTEGER LastWriteTime;
	LARGE_INTEGER ChangeTime;
	LARGE_INTEGER EndOfFile;
	LARGE_INTEGER AllocationSize;
	ULONG FileAttributes;
	ULONG FileNameLength;
	ULONG EaSize;			/* the reparse tag, for a reparse point */
	CCHAR ShortNameLength;
	WCHAR ShortName[12];
	LARGE_INTEGER FileId;
	WCHAR FileName[1];
} FILE_ID_BOTH_DIR_INFORMATION_;

typedef struct {
	ULONG NextEntryOffset;
	UCHAR Flags;
	UCHAR EaNameLength;
	USHORT EaValueLength;
	CHAR EaName[1];
} FILE_FULL_EA_INFORMATION_;

typedef struct {
	LARGE_INTEGER VolumeCreationTime;
	ULONG VolumeSerialNumber;
	ULONG VolumeLabelLength;
	BOOLEAN SupportsObjects;
	WCHAR VolumeLabel[1];
} FILE_FS_VOLUME_INFORMATION_;

typedef struct {
	ULONG FileSystemAttributes;
	LONG MaximumComponentNameLength;
	ULONG FileSystemNameLength;
	WCHAR FileSystemName[1];
} FILE_FS_ATTRIBUTE_INFORMATION_;

typedef struct {
	DWORD ReparseTag;
	WORD ReparseDataLength;
	WORD Reserved;
	union {
		struct { DWORD FileType; char PathBuffer[1]; } lx;
		UCHAR bare[1];
	} u;
} REPARSE_LX_;

typedef NTSTATUS (NTAPI *fn_NtCreateFile)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
	PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
typedef NTSTATUS (NTAPI *fn_NtClose)(HANDLE);
typedef NTSTATUS (NTAPI *fn_NtQueryInformationFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
typedef NTSTATUS (NTAPI *fn_NtSetInformationFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
typedef NTSTATUS (NTAPI *fn_NtQueryVolumeInformationFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
typedef NTSTATUS (NTAPI *fn_NtReadFile)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG);
typedef NTSTATUS (NTAPI *fn_NtWriteFile)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, PLARGE_INTEGER, PULONG);
typedef NTSTATUS (NTAPI *fn_NtQueryDirectoryFile)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG, BOOLEAN, PUNICODE_STRING, BOOLEAN);
typedef NTSTATUS (NTAPI *fn_NtSetEaFile)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG);
typedef NTSTATUS (NTAPI *fn_NtFsControlFile)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, ULONG, PVOID, ULONG, PVOID, ULONG);
typedef NTSTATUS (NTAPI *fn_NtFlushBuffersFile)(HANDLE, PIO_STATUS_BLOCK);
typedef NTSTATUS (NTAPI *fn_NtQuerySystemTime)(PLARGE_INTEGER);

static struct {
	int bound;
	fn_NtCreateFile CreateFile;
	fn_NtClose Close;
	fn_NtQueryInformationFile QueryInformationFile;
	fn_NtSetInformationFile SetInformationFile;
	fn_NtQueryVolumeInformationFile QueryVolumeInformationFile;
	fn_NtReadFile ReadFile;
	fn_NtWriteFile WriteFile;
	fn_NtQueryDirectoryFile QueryDirectoryFile;
	fn_NtSetEaFile SetEaFile;
	fn_NtFsControlFile FsControlFile;
	fn_NtFlushBuffersFile FlushBuffersFile;
	fn_NtQuerySystemTime QuerySystemTime;
} nt;

static int bind_nt(void)
{
	HMODULE m;
	if (nt.bound) return 0;
	m = GetModuleHandleW(L"ntdll.dll");
	if (!m) return -ENOSYS;
#define B(f) nt.f = (fn_Nt##f)(void *)GetProcAddress(m, "Nt" #f); if (!nt.f) return -ENOSYS
	B(CreateFile); B(Close); B(QueryInformationFile); B(SetInformationFile);
	B(QueryVolumeInformationFile); B(ReadFile); B(WriteFile); B(QueryDirectoryFile);
	B(SetEaFile); B(FsControlFile); B(FlushBuffersFile); B(QuerySystemTime);
#undef B
	nt.bound = 1;
	return 0;
}

/* ---- status to errno ------------------------------------------------------ */

static int errno_of(NTSTATUS s)
{
	switch ((ULONG)s) {
	case STATUS_SUCCESS: return 0;
	case STATUS_OBJECT_NAME_NOT_FOUND:
	case STATUS_OBJECT_PATH_NOT_FOUND:
	case (ULONG)STATUS_NO_SUCH_FILE_:
	case STATUS_OBJECT_NAME_INVALID: return ENOENT;
	case STATUS_OBJECT_NAME_COLLISION: return EEXIST;
	case STATUS_ACCESS_DENIED: return EACCES;
	case STATUS_SHARING_VIOLATION: return EBUSY;
	case (ULONG)STATUS_CANNOT_DELETE_: return EBUSY;
	case (ULONG)STATUS_DIRECTORY_NOT_EMPTY_: return ENOTEMPTY;
	case (ULONG)STATUS_FILE_IS_A_DIRECTORY_: return EISDIR;
	case (ULONG)STATUS_NOT_A_DIRECTORY_: return ENOTDIR;
	case (ULONG)STATUS_TOO_MANY_LINKS_: return EMLINK;
	case (ULONG)STATUS_DISK_FULL_: return ENOSPC;
	case (ULONG)STATUS_MEDIA_WRITE_PROTECTED_: return EROFS;
	case (ULONG)STATUS_NOT_SAME_DEVICE_: return EXDEV;
	case (ULONG)STATUS_NAME_TOO_LONG_: return ENAMETOOLONG;
	case (ULONG)STATUS_EAS_NOT_SUPPORTED_:
	case (ULONG)STATUS_NOT_SUPPORTED_:
	case (ULONG)STATUS_INVALID_INFO_CLASS_: return ENOTSUP;
	case (ULONG)STATUS_NOT_A_REPARSE_POINT_: return EINVAL;
	case (ULONG)STATUS_INVALID_PARAMETER_: return EINVAL;
	case STATUS_NO_MEMORY:
	case STATUS_INSUFFICIENT_RESOURCES: return ENOMEM;
	case STATUS_INVALID_HANDLE: return EBADF;
	case STATUS_PENDING: return EAGAIN;
	case (ULONG)STATUS_FILE_CORRUPT_ERROR_: return EIO;
	case (ULONG)STATUS_IO_REPARSE_TAG_NOT_HANDLED_: return ELOOP;
	default: return EIO;
	}
}

/* ---- names ----------------------------------------------------------------- */

/* UTF-8 to UTF-16 with the WSL escape: a byte NTFS refuses in a name becomes
 * U+F000 plus itself.  Returns the wide length, or -1 for a name too long or
 * malformed. */
static int name_to_wide(const char *name, WCHAR *out, size_t cap)
{
	size_t i = 0, o = 0;
	while (name[i]) {
		unsigned char c = (unsigned char)name[i];
		uint32_t cp;
		int n;
		if (c < 0x80) { cp = c; n = 1; }
		else if ((c & 0xe0) == 0xc0) { cp = c & 0x1f; n = 2; }
		else if ((c & 0xf0) == 0xe0) { cp = c & 0x0f; n = 3; }
		else if ((c & 0xf8) == 0xf0) { cp = c & 0x07; n = 4; }
		else return -1;
		for (int k = 1; k < n; k++) {
			unsigned char d = (unsigned char)name[i + k];
			if ((d & 0xc0) != 0x80) return -1;
			cp = (cp << 6) | (d & 0x3f);
		}
		i += (size_t)n;
		if (cp < 0x20 || cp == '"' || cp == '*' || cp == ':' || cp == '<' ||
		    cp == '>' || cp == '?' || cp == '|' || cp == '\\')
			cp = 0xf000 + cp;
		if (cp >= 0x10000) {
			if (o + 2 > cap) return -1;
			cp -= 0x10000;
			out[o++] = (WCHAR)(0xd800 + (cp >> 10));
			out[o++] = (WCHAR)(0xdc00 + (cp & 0x3ff));
		} else {
			if (o + 1 > cap) return -1;
			out[o++] = (WCHAR)cp;
		}
	}
	return (int)o;
}

/* The reverse, for names read back from a directory. */
static int name_from_wide(const WCHAR *w, size_t wlen, char *out, size_t cap)
{
	size_t i = 0, o = 0;
	while (i < wlen) {
		uint32_t cp = w[i++];
		if (cp >= 0xd800 && cp < 0xdc00 && i < wlen && w[i] >= 0xdc00 && w[i] < 0xe000) {
			cp = 0x10000 + ((cp - 0xd800) << 10) + (w[i] - 0xdc00);
			i++;
		}
		if (cp >= 0xf000 && cp <= 0xf0ff) cp -= 0xf000;
		if (cp < 0x80) {
			if (o + 1 >= cap) return -1;
			out[o++] = (char)cp;
		} else if (cp < 0x800) {
			if (o + 2 >= cap) return -1;
			out[o++] = (char)(0xc0 | (cp >> 6));
			out[o++] = (char)(0x80 | (cp & 0x3f));
		} else if (cp < 0x10000) {
			if (o + 3 >= cap) return -1;
			out[o++] = (char)(0xe0 | (cp >> 12));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			out[o++] = (char)(0x80 | (cp & 0x3f));
		} else {
			if (o + 4 >= cap) return -1;
			out[o++] = (char)(0xf0 | (cp >> 18));
			out[o++] = (char)(0x80 | ((cp >> 12) & 0x3f));
			out[o++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			out[o++] = (char)(0x80 | (cp & 0x3f));
		}
	}
	out[o] = 0;
	return (int)o;
}

/* ---- time ------------------------------------------------------------------ */

#define EPOCH_DIFF_100NS 116444736000000000LL

static int64_t ns_of(LARGE_INTEGER t)
{
	if (t.QuadPart == 0) return 0;
	return (t.QuadPart - EPOCH_DIFF_100NS) * 100;
}

static LARGE_INTEGER ft_of(int64_t ns)
{
	LARGE_INTEGER t;
	t.QuadPart = ns / 100 + EPOCH_DIFF_100NS;
	return t;
}

int64_t hfs_now(void)
{
	LARGE_INTEGER t;
	if (bind_nt() != 0) return 0;
	nt.QuerySystemTime(&t);
	return ns_of(t);
}

/* ---- extended attributes ----------------------------------------------------- */

/* Build a FILE_FULL_EA_INFORMATION chain for the LX attributes in which.
 * Returns the byte length. */
static ULONG build_lx_eas(unsigned char *buf, size_t cap, uint32_t which,
			  uint32_t uid, uint32_t gid, uint32_t mode,
			  uint32_t major, uint32_t minor)
{
	struct { const char *name; const void *val; unsigned len; } w[4];
	uint32_t dev[2] = { major, minor };
	unsigned n = 0, i;
	ULONG off = 0;

	if (which & HFS_LX_UID) { w[n].name = "$LXUID"; w[n].val = &uid; w[n].len = 4; n++; }
	if (which & HFS_LX_GID) { w[n].name = "$LXGID"; w[n].val = &gid; w[n].len = 4; n++; }
	if (which & HFS_LX_MODE) { w[n].name = "$LXMOD"; w[n].val = &mode; w[n].len = 4; n++; }
	if (which & HFS_LX_DEV) { w[n].name = "$LXDEV"; w[n].val = dev; w[n].len = 8; n++; }
	for (i = 0; i < n; i++) {
		FILE_FULL_EA_INFORMATION_ *e = (FILE_FULL_EA_INFORMATION_ *)(buf + off);
		unsigned nl = (unsigned)strlen(w[i].name);
		ULONG sz = (8u + nl + 1u + w[i].len + 3u) & ~3u;
		if (off + sz > cap) return 0;
		memset(e, 0, sz);
		e->EaNameLength = (UCHAR)nl;
		e->EaValueLength = (USHORT)w[i].len;
		memcpy(e->EaName, w[i].name, nl + 1);
		memcpy(e->EaName + nl + 1, w[i].val, w[i].len);
		e->NextEntryOffset = (i + 1 == n) ? 0 : sz;
		off += sz;
	}
	return off;
}

int hfs_set_lx(hfs_h h, uint32_t which, uint32_t uid, uint32_t gid,
	       uint32_t mode, uint32_t major, uint32_t minor)
{
	unsigned char buf[256];
	IO_STATUS_BLOCK iosb;
	ULONG len;
	int r = bind_nt();
	if (r) return r;
	len = build_lx_eas(buf, sizeof buf, which, uid, gid, mode, major, minor);
	if (!len) return -EINVAL;
	return -errno_of(nt.SetEaFile((HANDLE)(uintptr_t)h, &iosb, buf, len));
}

/* ---- stat ------------------------------------------------------------------ */

static uint32_t kind_of(ULONG attrs, ULONG tag)
{
	if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
		switch (tag) {
		case TAG_LX_SYMLINK: return HFS_KIND_LXLINK;
		case TAG_LX_FIFO: return HFS_KIND_FIFO;
		case TAG_LX_CHR: return HFS_KIND_CHR;
		case TAG_LX_BLK: return HFS_KIND_BLK;
		case TAG_AF_UNIX: return HFS_KIND_SOCK;
		default: break;
		}
		if (tag == TAG_SYMLINK || tag == TAG_MOUNT_POINT)
			return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? HFS_KIND_DIR : HFS_KIND_FILE;
		return HFS_KIND_OTHER;
	}
	return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? HFS_KIND_DIR : HFS_KIND_FILE;
}

static void stat_of(const FILE_STAT_LX_INFORMATION_ *x, struct hfs_stat *st)
{
	memset(st, 0, sizeof *st);
	st->ino = (uint64_t)x->FileId.QuadPart;
	st->size = (uint64_t)x->EndOfFile.QuadPart;
	st->blocks = ((uint64_t)x->AllocationSize.QuadPart + 511) / 512;
	st->nlink = x->NumberOfLinks;
	st->lxflags = x->LxFlags & (LX_HAS_UID | LX_HAS_GID | LX_HAS_MODE | LX_HAS_DEV | LX_CASE_DIR);
	st->mode = x->LxMode;
	st->uid = x->LxUid;
	st->gid = x->LxGid;
	st->rdev_major = x->LxDeviceIdMajor;
	st->rdev_minor = x->LxDeviceIdMinor;
	st->atime = ns_of(x->LastAccessTime);
	st->mtime = ns_of(x->LastWriteTime);
	st->ctime = ns_of(x->ChangeTime);
	st->btime = ns_of(x->CreationTime);
	st->reparse_tag = (x->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? x->ReparseTag : 0;
	st->kind = kind_of(x->FileAttributes, x->ReparseTag);
}

static int stat_handle(HANDLE h, struct hfs_stat *st)
{
	FILE_STAT_LX_INFORMATION_ x;
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;
	memset(&x, 0, sizeof x);
	s = nt.QueryInformationFile(h, &iosb, &x, sizeof x, FileStatLxInformation_);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	stat_of(&x, st);
	return 0;
}

int hfs_stat(hfs_h h, struct hfs_stat *st)
{
	int r = bind_nt();
	if (r) return r;
	return stat_handle((HANDLE)(uintptr_t)h, st);
}

/* ---- open ------------------------------------------------------------------ */

static NTSTATUS create(HANDLE dir, const WCHAR *w, int wlen, ACCESS_MASK access,
		       ULONG disposition, ULONG options, ULONG attrs,
		       void *ea, ULONG ealen, HANDLE *out, ULONG *info)
{
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;

	us.Buffer = (PWSTR)w;
	us.Length = (USHORT)(wlen * sizeof(WCHAR));
	us.MaximumLength = us.Length;
	InitializeObjectAttributes(&oa, &us, 0, dir, NULL);
	memset(&iosb, 0, sizeof iosb);
	*out = NULL;
	s = nt.CreateFile(out, access | SYNCHRONIZE, &oa, &iosb, NULL, attrs,
			  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			  disposition, options | FILE_SYNCHRONOUS_IO_NONALERT, ea, ealen);
	if (info) *info = (ULONG)iosb.Information;
	return s;
}

static ACCESS_MASK access_of(unsigned flags)
{
	ACCESS_MASK a = FILE_READ_ATTRIBUTES | FILE_READ_EA;
	if (flags & HFS_O_READ) a |= FILE_READ_DATA;		/* FILE_LIST_DIRECTORY is the same bit */
	if (flags & HFS_O_WRITE) a |= FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
	if (flags & HFS_O_ATTR) a |= FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
	if (flags & HFS_O_DELETE) a |= DELETE;
	if (flags & HFS_O_DIR) a |= FILE_TRAVERSE;
	return a;
}

int hfs_open_root(const char *path, hfs_h *out)
{
	WCHAR w[4096 + 8];
	int n;
	HANDLE h;
	NTSTATUS s;
	int r = bind_nt();
	if (r) return r;
	wcscpy(w, L"\\??\\");
	n = MultiByteToWideChar(CP_UTF8, 0, path, -1, w + 4, 4096);
	if (n <= 0) return -EINVAL;
	n = (int)wcslen(w);
	s = create(NULL, w, n, FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | FILE_READ_EA | FILE_WRITE_ATTRIBUTES,
		   FILE_OPEN, FILE_DIRECTORY_FILE, 0, NULL, 0, &h, NULL);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	*out = (hfs_h)(uintptr_t)h;
	return 0;
}

int hfs_openat(hfs_h dir, const char *name, unsigned flags, uint32_t mode,
	       uint32_t uid, uint32_t gid, hfs_h *out, struct hfs_stat *st)
{
	WCHAR w[520];
	unsigned char ea[256];
	ULONG ealen = 0, disposition, options = 0, info = 0;
	HANDLE h;
	NTSTATUS s;
	struct hfs_stat local;
	int n, r = bind_nt();

	if (r) return r;
	*out = 0;
	n = name_to_wide(name, w, 255);
	if (n < 0) return -ENAMETOOLONG;
	if (n == 0) return -ENOENT;

	if (flags & HFS_O_CREATE) {
		disposition = (flags & HFS_O_EXCL) ? FILE_CREATE
			    : (flags & HFS_O_TRUNC) ? FILE_OVERWRITE_IF : FILE_OPEN_IF;
		ealen = build_lx_eas(ea, sizeof ea, HFS_LX_UID | HFS_LX_GID | HFS_LX_MODE,
				     uid, gid, mode, 0, 0);
	} else {
		disposition = (flags & HFS_O_TRUNC) ? FILE_OVERWRITE : FILE_OPEN;
	}
	if (flags & HFS_O_DIR) options |= FILE_DIRECTORY_FILE;
	if (flags & HFS_O_NODIR) options |= FILE_NON_DIRECTORY_FILE;
	if (flags & HFS_O_REPARSE) options |= FILE_OPEN_REPARSE_POINT;

	s = create((HANDLE)(uintptr_t)dir, w, n, access_of(flags), disposition, options,
		   FILE_ATTRIBUTE_NORMAL, ealen ? ea : NULL, ealen, &h, &info);
	if (s == STATUS_IO_REPARSE_TAG_NOT_HANDLED_ && !(flags & HFS_O_REPARSE)) {
		/* an LX reparse point: hand back the object itself */
		s = create((HANDLE)(uintptr_t)dir, w, n, FILE_READ_ATTRIBUTES | FILE_READ_EA | (access_of(flags) & DELETE),
			   FILE_OPEN, FILE_OPEN_REPARSE_POINT, 0, NULL, 0, &h, &info);
		if (!NT_SUCCESS(s)) return -errno_of(s);
		r = stat_handle(h, st ? st : &local);
		if (r) { nt.Close(h); return r; }
		*out = (hfs_h)(uintptr_t)h;
		return HFS_REPARSE;
	}
	if (!NT_SUCCESS(s)) {
		/* FILE_OVERWRITE on a directory says "is a directory" in NT's
		 * own words; the VFS asked for a file and gets EISDIR. */
		return -errno_of(s);
	}
	if (st) {
		r = stat_handle(h, st);
		if (r) { nt.Close(h); return r; }
		/* a truncating open that found an existing file keeps its EAs;
		 * a created one has the ones we asked for */
	}
	*out = (hfs_h)(uintptr_t)h;
	return 0;
}

int hfs_reopen(hfs_h h, unsigned flags, hfs_h *out, struct hfs_stat *st)
{
	HANDLE nh;
	NTSTATUS s;
	ULONG options = 0;
	int r = bind_nt();
	if (r) return r;
	*out = 0;
	if (flags & HFS_O_DIR) options |= FILE_DIRECTORY_FILE;
	if (flags & HFS_O_NODIR) options |= FILE_NON_DIRECTORY_FILE;
	if (flags & HFS_O_REPARSE) options |= FILE_OPEN_REPARSE_POINT;
	/* an empty name relative to a handle names the object itself */
	s = create((HANDLE)(uintptr_t)h, L"", 0, access_of(flags), FILE_OPEN, options, 0, NULL, 0, &nh, NULL);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	if (st) {
		r = stat_handle(nh, st);
		if (r) { nt.Close(nh); return r; }
	}
	*out = (hfs_h)(uintptr_t)nh;
	return 0;
}

void hfs_close(hfs_h h)
{
	if (h && bind_nt() == 0)
		nt.Close((HANDLE)(uintptr_t)h);
}

int hfs_statat(hfs_h dir, const char *name, struct hfs_stat *st)
{
	hfs_h h;
	int r = hfs_openat(dir, name, HFS_O_REPARSE, 0, 0, 0, &h, st);
	if (r < 0) return r;
	hfs_close(h);
	return 0;
}

/* ---- bytes ------------------------------------------------------------------ */

int64_t hfs_pread(hfs_h h, void *buf, size_t len, uint64_t off)
{
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER pos;
	NTSTATUS s;
	int r = bind_nt();
	if (r) return r;
	if (len > 0x7fffffff) len = 0x7fffffff;
	pos.QuadPart = (LONGLONG)off;
	memset(&iosb, 0, sizeof iosb);
	s = nt.ReadFile((HANDLE)(uintptr_t)h, NULL, NULL, NULL, &iosb, buf, (ULONG)len, &pos, NULL);
	if (s == STATUS_END_OF_FILE) return 0;
	if (!NT_SUCCESS(s)) return -errno_of(s);
	return (int64_t)iosb.Information;
}

int64_t hfs_pwrite(hfs_h h, const void *buf, size_t len, uint64_t off, int append)
{
	IO_STATUS_BLOCK iosb;
	LARGE_INTEGER pos;
	NTSTATUS s;
	int r = bind_nt();
	if (r) return r;
	if (len > 0x7fffffff) len = 0x7fffffff;
	if (append) { pos.HighPart = -1; pos.LowPart = 0xffffffff; }	/* FILE_WRITE_TO_END_OF_FILE */
	else pos.QuadPart = (LONGLONG)off;
	memset(&iosb, 0, sizeof iosb);
	s = nt.WriteFile((HANDLE)(uintptr_t)h, NULL, NULL, NULL, &iosb, (PVOID)buf, (ULONG)len, &pos, NULL);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	return (int64_t)iosb.Information;
}

int hfs_truncate(hfs_h h, uint64_t size)
{
	FILE_END_OF_FILE_INFORMATION_ e;
	IO_STATUS_BLOCK iosb;
	int r = bind_nt();
	if (r) return r;
	e.EndOfFile.QuadPart = (LONGLONG)size;
	return -errno_of(nt.SetInformationFile((HANDLE)(uintptr_t)h, &iosb, &e, sizeof e, FileEndOfFileInformation_));
}

int hfs_fsync(hfs_h h)
{
	IO_STATUS_BLOCK iosb;
	int r = bind_nt();
	if (r) return r;
	return -errno_of(nt.FlushBuffersFile((HANDLE)(uintptr_t)h, &iosb));
}

int hfs_set_times(hfs_h h, int64_t atime, int64_t mtime)
{
	FILE_BASIC_INFORMATION_ b;
	IO_STATUS_BLOCK iosb;
	int64_t now = 0;
	int r = bind_nt();
	if (r) return r;
	memset(&b, 0, sizeof b);		/* a zero field is left alone */
	if (atime == HFS_TIME_NOW || mtime == HFS_TIME_NOW) now = hfs_now();
	if (atime == HFS_TIME_NOW) b.LastAccessTime = ft_of(now);
	else if (atime != HFS_TIME_OMIT) b.LastAccessTime = ft_of(atime);
	if (mtime == HFS_TIME_NOW) b.LastWriteTime = ft_of(now);
	else if (mtime != HFS_TIME_OMIT) b.LastWriteTime = ft_of(mtime);
	if (b.LastAccessTime.QuadPart == 0 && b.LastWriteTime.QuadPart == 0) return 0;
	return -errno_of(nt.SetInformationFile((HANDLE)(uintptr_t)h, &iosb, &b, sizeof b, FileBasicInformation_));
}

/* ---- names: create, remove, rename, link ------------------------------------- */

int hfs_mkdir(hfs_h dir, const char *name, uint32_t mode, uint32_t uid, uint32_t gid,
	      int case_sensitive)
{
	WCHAR w[520];
	unsigned char ea[256];
	ULONG ealen;
	HANDLE h;
	NTSTATUS s;
	int n, r = bind_nt();
	if (r) return r;
	n = name_to_wide(name, w, 255);
	if (n <= 0) return n ? -ENAMETOOLONG : -ENOENT;
	ealen = build_lx_eas(ea, sizeof ea, HFS_LX_UID | HFS_LX_GID | HFS_LX_MODE, uid, gid, mode, 0, 0);
	s = create((HANDLE)(uintptr_t)dir, w, n, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, FILE_CREATE,
		   FILE_DIRECTORY_FILE, FILE_ATTRIBUTE_DIRECTORY, ea, ealen, &h, NULL);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	if (case_sensitive) {
		FILE_FLAGS_INFORMATION_ f = { FILE_CS_FLAG_CASE_SENSITIVE_DIR_ };
		IO_STATUS_BLOCK iosb;
		nt.SetInformationFile(h, &iosb, &f, sizeof f, FileCaseSensitiveInformation_);
	}
	nt.Close(h);
	return 0;
}

int hfs_set_case_sensitive(hfs_h dirh)
{
	FILE_FLAGS_INFORMATION_ f;
	IO_STATUS_BLOCK iosb;
	int r = bind_nt();
	if (r) return r;
	f.Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR_;
	return -errno_of(nt.SetInformationFile((HANDLE)(uintptr_t)dirh, &iosb, &f, sizeof f, FileCaseSensitiveInformation_));
}

int hfs_unlink(hfs_h dir, const char *name, int is_dir)
{
	WCHAR w[520];
	HANDLE h;
	NTSTATUS s;
	IO_STATUS_BLOCK iosb;
	FILE_FLAGS_INFORMATION_ dx;
	int n, r = bind_nt();
	if (r) return r;
	n = name_to_wide(name, w, 255);
	if (n <= 0) return n ? -ENAMETOOLONG : -ENOENT;
	s = create((HANDLE)(uintptr_t)dir, w, n, DELETE | FILE_READ_ATTRIBUTES, FILE_OPEN,
		   FILE_OPEN_REPARSE_POINT | (is_dir ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE),
		   0, NULL, 0, &h, NULL);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	dx.Flags = FILE_DISPOSITION_DELETE_ | FILE_DISPOSITION_POSIX_SEMANTICS_;
	s = nt.SetInformationFile(h, &iosb, &dx, sizeof dx, FileDispositionInformationEx_);
	if (s == STATUS_INVALID_PARAMETER_ || s == STATUS_NOT_SUPPORTED_ || s == STATUS_INVALID_INFO_CLASS_) {
		FILE_DISPOSITION_INFORMATION_ d = { TRUE };
		s = nt.SetInformationFile(h, &iosb, &d, sizeof d, FileDispositionInformation_);
	}
	nt.Close(h);
	return -errno_of(s);
}

int hfs_rename(hfs_h olddir, const char *oldname, hfs_h newdir,
	       const char *newname, int noreplace)
{
	WCHAR w[520], nw[520];
	HANDLE h;
	NTSTATUS s;
	IO_STATUS_BLOCK iosb;
	unsigned char buf[sizeof(FILE_RENAME_INFORMATION_EX_) + 520 * sizeof(WCHAR)];
	FILE_RENAME_INFORMATION_EX_ *ri = (FILE_RENAME_INFORMATION_EX_ *)buf;
	int n, m, r = bind_nt();
	if (r) return r;
	n = name_to_wide(oldname, w, 255);
	if (n <= 0) return n ? -ENAMETOOLONG : -ENOENT;
	m = name_to_wide(newname, nw, 255);
	if (m <= 0) return m ? -ENAMETOOLONG : -ENOENT;
	s = create((HANDLE)(uintptr_t)olddir, w, n, DELETE | FILE_READ_ATTRIBUTES, FILE_OPEN,
		   FILE_OPEN_REPARSE_POINT, 0, NULL, 0, &h, NULL);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	memset(buf, 0, sizeof buf);
	ri->Flags = FILE_RENAME_POSIX_SEMANTICS_ | (noreplace ? 0 : FILE_RENAME_REPLACE_IF_EXISTS_);
	ri->RootDirectory = (HANDLE)(uintptr_t)newdir;
	ri->FileNameLength = (ULONG)(m * sizeof(WCHAR));
	memcpy(ri->FileName, nw, (size_t)m * sizeof(WCHAR));
	s = nt.SetInformationFile(h, &iosb, ri, (ULONG)(sizeof *ri + (size_t)m * sizeof(WCHAR)), FileRenameInformationEx_);
	nt.Close(h);
	return -errno_of(s);
}

int hfs_link(hfs_h h, hfs_h dir, const char *name)
{
	WCHAR nw[520];
	IO_STATUS_BLOCK iosb;
	unsigned char buf[sizeof(FILE_LINK_INFORMATION_) + 520 * sizeof(WCHAR)];
	FILE_LINK_INFORMATION_ *li = (FILE_LINK_INFORMATION_ *)buf;
	int m, r = bind_nt();
	if (r) return r;
	m = name_to_wide(name, nw, 255);
	if (m <= 0) return m ? -ENAMETOOLONG : -ENOENT;
	memset(buf, 0, sizeof buf);
	li->ReplaceIfExists = FALSE;
	li->RootDirectory = (HANDLE)(uintptr_t)dir;
	li->FileNameLength = (ULONG)(m * sizeof(WCHAR));
	memcpy(li->FileName, nw, (size_t)m * sizeof(WCHAR));
	return -errno_of(nt.SetInformationFile((HANDLE)(uintptr_t)h, &iosb, li,
			(ULONG)(sizeof *li + (size_t)m * sizeof(WCHAR)), FileLinkInformation_));
}

/* ---- reparse points: symlinks and special files --------------------------------- */

static NTSTATUS set_reparse(HANDLE h, void *buf, ULONG len)
{
	IO_STATUS_BLOCK iosb;
	memset(&iosb, 0, sizeof iosb);
	return nt.FsControlFile(h, NULL, NULL, NULL, &iosb, FSCTL_SET_REPARSE_POINT, buf, len, NULL, 0);
}

/* Create an empty reparse object with the LX attributes, then stamp it. */
static int make_reparse(hfs_h dir, const char *name, uint32_t mode, uint32_t uid,
			uint32_t gid, uint32_t major, uint32_t minor, void *rp, ULONG rplen)
{
	WCHAR w[520];
	unsigned char ea[256];
	ULONG ealen;
	HANDLE h;
	NTSTATUS s;
	int n;
	n = name_to_wide(name, w, 255);
	if (n <= 0) return n ? -ENAMETOOLONG : -ENOENT;
	ealen = build_lx_eas(ea, sizeof ea, HFS_LX_UID | HFS_LX_GID | HFS_LX_MODE |
			     ((major || minor) ? HFS_LX_DEV : 0), uid, gid, mode, major, minor);
	s = create((HANDLE)(uintptr_t)dir, w, n, FILE_WRITE_DATA | FILE_READ_DATA | FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES,
		   FILE_CREATE, FILE_OPEN_REPARSE_POINT | FILE_NON_DIRECTORY_FILE,
		   FILE_ATTRIBUTE_NORMAL, ea, ealen, &h, NULL);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	s = set_reparse(h, rp, rplen);
	if (!NT_SUCCESS(s)) {
		FILE_DISPOSITION_INFORMATION_ d = { TRUE };
		IO_STATUS_BLOCK iosb;
		nt.SetInformationFile(h, &iosb, &d, sizeof d, FileDispositionInformation_);
		nt.Close(h);
		return -errno_of(s);
	}
	nt.Close(h);
	return 0;
}

int hfs_symlink(hfs_h dir, const char *name, const char *target, uint32_t uid, uint32_t gid)
{
	unsigned char buf[8 + 4 + 4096];
	REPARSE_LX_ *rp = (REPARSE_LX_ *)buf;
	size_t tlen = strlen(target);
	int r = bind_nt();
	if (r) return r;
	if (tlen == 0 || tlen > 4095) return -ENAMETOOLONG;
	memset(buf, 0, sizeof buf);
	rp->ReparseTag = TAG_LX_SYMLINK;
	rp->ReparseDataLength = (WORD)(4 + tlen);
	rp->u.lx.FileType = 2;
	memcpy(rp->u.lx.PathBuffer, target, tlen);
	return make_reparse(dir, name, 0120777, uid, gid, 0, 0, buf, (ULONG)(8 + 4 + tlen));
}

int hfs_mknod(hfs_h dir, const char *name, uint32_t kind, uint32_t mode,
	      uint32_t uid, uint32_t gid, uint32_t major, uint32_t minor)
{
	unsigned char buf[16];
	REPARSE_LX_ *rp = (REPARSE_LX_ *)buf;
	int r = bind_nt();
	if (r) return r;
	memset(buf, 0, sizeof buf);
	switch (kind) {
	case HFS_KIND_FIFO: rp->ReparseTag = TAG_LX_FIFO; break;
	case HFS_KIND_CHR: rp->ReparseTag = TAG_LX_CHR; break;
	case HFS_KIND_BLK: rp->ReparseTag = TAG_LX_BLK; break;
	case HFS_KIND_SOCK: rp->ReparseTag = TAG_AF_UNIX; break;
	default: return -EINVAL;
	}
	return make_reparse(dir, name, mode, uid, gid, major, minor, buf, 8);
}

int64_t hfs_readlink(hfs_h h, char *buf, size_t cap)
{
	unsigned char rb[8 + 4 + 4096];
	REPARSE_LX_ *rp = (REPARSE_LX_ *)rb;
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;
	size_t tlen;
	int r = bind_nt();
	if (r) return r;
	memset(&iosb, 0, sizeof iosb);
	s = nt.FsControlFile((HANDLE)(uintptr_t)h, NULL, NULL, NULL, &iosb, FSCTL_GET_REPARSE_POINT,
			     NULL, 0, rb, sizeof rb);
	if (!NT_SUCCESS(s)) return -errno_of(s);
	if (rp->ReparseTag != TAG_LX_SYMLINK || rp->ReparseDataLength < 4) return -EINVAL;
	tlen = rp->ReparseDataLength - 4;
	memcpy(buf, rp->u.lx.PathBuffer, tlen < cap ? tlen : cap);
	return (int64_t)tlen;
}

/* ---- directories ------------------------------------------------------------- */

struct hfs_dir {
	HANDLE h;
	size_t off;			/* next unread entry in buf, or filled at end */
	size_t filled;
	int exhausted;
	_Alignas(8) unsigned char buf[65536];	/* NT wants the entries 8-aligned */
};

struct hfs_dir *hfs_dir_open(hfs_h h)
{
	struct hfs_dir *d;
	if (bind_nt()) return NULL;
	d = calloc(1, sizeof *d);
	if (!d) return NULL;
	d->h = (HANDLE)(uintptr_t)h;
	return d;
}

void hfs_dir_close(struct hfs_dir *d)
{
	free(d);
}

/* Fill the buffer from the host; restart rewinds.  0, or -errno; sets
 * exhausted at the end rather than failing. */
static int dir_fill(struct hfs_dir *d, int restart)
{
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;
	memset(&iosb, 0, sizeof iosb);
	d->off = d->filled = 0;
	s = nt.QueryDirectoryFile(d->h, NULL, NULL, NULL, &iosb, d->buf, sizeof d->buf,
				  FileIdBothDirectoryInformation_, FALSE, NULL, restart ? TRUE : FALSE);
	if (s == STATUS_NO_MORE_FILES_) { d->exhausted = 1; return 0; }
	if (!NT_SUCCESS(s)) return -errno_of(s);
	d->filled = 1;			/* the chain's end is the entry with no next */
	return 0;
}

int hfs_dir_read(struct hfs_dir *d, struct hfs_dirent *ents, size_t cap, int restart)
{
	size_t n = 0;
	int r;
	if (!d) return -EBADF;
	if (restart) {
		d->exhausted = 0;
		r = dir_fill(d, 1);
		if (r) return r;
	} else if (!d->filled && !d->exhausted) {
		r = dir_fill(d, 0);
		if (r) return r;
	}
	while (n < cap && !d->exhausted) {
		const FILE_ID_BOTH_DIR_INFORMATION_ *e;
		size_t wl;
		int dot;
		if (!d->filled) {
			r = dir_fill(d, 0);
			if (r) return r;
			continue;
		}
		e = (const FILE_ID_BOTH_DIR_INFORMATION_ *)(d->buf + d->off);
		wl = e->FileNameLength / sizeof(WCHAR);
		dot = (wl == 1 && e->FileName[0] == L'.') || (wl == 2 && e->FileName[0] == L'.' && e->FileName[1] == L'.');
		if (!dot) {
			struct hfs_dirent *o = &ents[n];
			if (name_from_wide(e->FileName, wl, o->name, sizeof o->name) >= 0) {
				o->ino = (uint64_t)e->FileId.QuadPart;
				o->kind = kind_of(e->FileAttributes,
						  (e->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? e->EaSize : 0);
				n++;
			}
		}
		if (e->NextEntryOffset == 0) d->filled = 0;	/* batch consumed */
		else d->off += e->NextEntryOffset;
	}
	return (int)n;
}

/* ---- the volume ---------------------------------------------------------------- */

int hfs_volume_is_lx_capable(hfs_h h)
{
	unsigned char buf[sizeof(FILE_FS_ATTRIBUTE_INFORMATION_) + 64 * sizeof(WCHAR)];
	FILE_FS_ATTRIBUTE_INFORMATION_ *a = (FILE_FS_ATTRIBUTE_INFORMATION_ *)buf;
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;
	int r = bind_nt();
	if (r) return r;
	memset(buf, 0, sizeof buf);
	s = nt.QueryVolumeInformationFile((HANDLE)(uintptr_t)h, &iosb, buf, sizeof buf, FileFsAttributeInformation_);
	if (!NT_SUCCESS(s) && s != STATUS_BUFFER_OVERFLOW) return -errno_of(s);
	if (!(a->FileSystemAttributes & FILE_SUPPORTS_EXTENDED_ATTRIBUTES_)) return 0;
	if (!(a->FileSystemAttributes & FILE_SUPPORTS_POSIX_UNLINK_RENAME_)) return 0;
	return 1;
}

uint64_t hfs_volume_serial(hfs_h h)
{
	unsigned char buf[sizeof(FILE_FS_VOLUME_INFORMATION_) + 64 * sizeof(WCHAR)];
	FILE_FS_VOLUME_INFORMATION_ *v = (FILE_FS_VOLUME_INFORMATION_ *)buf;
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;
	if (bind_nt()) return 0;
	memset(buf, 0, sizeof buf);
	s = nt.QueryVolumeInformationFile((HANDLE)(uintptr_t)h, &iosb, buf, sizeof buf, FileFsVolumeInformation_);
	if (!NT_SUCCESS(s) && s != STATUS_BUFFER_OVERFLOW) return 0;
	return v->VolumeSerialNumber;
}
