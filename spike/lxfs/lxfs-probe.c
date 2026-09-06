/* Does this NTFS volume, through NT's own interfaces, carry the Linux
 * metadata WSL's DrvFs uses?
 *
 * Proposal 0011 § 8 decides that the kernel's on-disk format is WSL's rather
 * than an invention of its own, and its "Not verified" section admits that
 * every NT behaviour underneath that decision is a reading of documentation.
 * This probe turns eight of those readings into measurements: the two
 * FileStatLxInformation paths, the four metadata EAs, an LX symlink, a
 * case-sensitive directory, POSIX delete, and the stat rate.
 *
 * It writes nothing outside the directory handed to it in argv[1], and it
 * leaves that directory populated on purpose: the shell harness reads the
 * tree back from WSL and from Cygwin afterwards, which is q7, and then removes
 * it. A refusal is a legitimate answer everywhere here -- each case prints the
 * NTSTATUS it got and the run continues.
 */
#include "lxfs-nt.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define RELEASE "lxfs-probe 1.0"

#ifndef FILE_DIRECTORY_FILE
#define FILE_DIRECTORY_FILE 0x00000001
#endif
#ifndef FILE_SYNCHRONOUS_IO_NONALERT
#define FILE_SYNCHRONOUS_IO_NONALERT 0x00000020
#endif
#ifndef FILE_NON_DIRECTORY_FILE
#define FILE_NON_DIRECTORY_FILE 0x00000040
#endif
#ifndef FILE_OPEN_REPARSE_POINT
#define FILE_OPEN_REPARSE_POINT 0x00200000
#endif
#ifndef FILE_CREATE
#define FILE_CREATE 2
#endif

static struct lxfs_nt nt;
static int verbose;
static wchar_t root[4096];
static int bench_count = 2000;
static int clean;

static void vnote(const char *fmt, ...)
{
	va_list ap;
	if (!verbose)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "lxfs-probe: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static void st(const char *key, NTSTATUS s)
{
	printf("%s=0x%08lx\n", key, (unsigned long) s);
}

static void num(const char *key, unsigned long long v)
{
	printf("%s=%llu\n", key, v);
}

static void hex(const char *key, unsigned long long v)
{
	printf("%s=0x%llx\n", key, v);
}

static void oct(const char *key, unsigned long v)
{
	printf("%s=0%lo\n", key, v);
}

/* root\name into out. */
static void joinp(wchar_t *out, size_t n, const wchar_t *name)
{
	swprintf(out, n, L"%ls\\%ls", root, name);
}

/* --- the two stat paths ------------------------------------------------- */

static NTSTATUS stat_lx_handle(HANDLE h, LXFS_FILE_STAT_LX_INFORMATION *out)
{
	IO_STATUS_BLOCK iosb;
	memset(out, 0, sizeof *out);
	memset(&iosb, 0, sizeof iosb);
	if (nt.QueryInformationFile == NULL)
		return STATUS_PROCEDURE_NOT_FOUND;
	return nt.QueryInformationFile(h, &iosb, out, sizeof *out,
				       LXFS_FileStatLxInformation);
}

static NTSTATUS stat_lx_name(const wchar_t *win32path,
			     LXFS_FILE_STAT_LX_INFORMATION *out)
{
	wchar_t ntpath[4200];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK iosb;

	memset(out, 0, sizeof *out);
	memset(&iosb, 0, sizeof iosb);
	if (nt.QueryInformationByName == NULL)
		return STATUS_PROCEDURE_NOT_FOUND;
	lxfs_nt_path(&us, ntpath, sizeof ntpath / sizeof ntpath[0], win32path);
	InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
	return nt.QueryInformationByName(&oa, &iosb, out, sizeof *out,
					 LXFS_FileStatLxInformation);
}

/* Prints the six LX fields under a prefix, so q1 and q2 can be compared field
 * by field in the transcript rather than by a single equality bit. */
static void emit_lx(const char *prefix, const LXFS_FILE_STAT_LX_INFORMATION *s)
{
	char key[64];
	snprintf(key, sizeof key, "%s_lx_flags", prefix); hex(key, s->LxFlags);
	snprintf(key, sizeof key, "%s_lx_uid", prefix);   num(key, s->LxUid);
	snprintf(key, sizeof key, "%s_lx_gid", prefix);   num(key, s->LxGid);
	snprintf(key, sizeof key, "%s_lx_mode", prefix);  oct(key, s->LxMode);
	snprintf(key, sizeof key, "%s_lx_dev_major", prefix);
	num(key, s->LxDeviceIdMajor);
	snprintf(key, sizeof key, "%s_lx_dev_minor", prefix);
	num(key, s->LxDeviceIdMinor);
}

/* --- the metadata EAs ---------------------------------------------------- */

struct ea_want {
	const char *name;
	const void *value;
	unsigned value_len;
};

static unsigned ea_entry_size(const struct ea_want *w)
{
	unsigned raw = 8u + (unsigned) strlen(w->name) + 1u + w->value_len;
	return (raw + 3u) & ~3u;
}

static NTSTATUS ea_set(HANDLE h, const struct ea_want *w, unsigned count)
{
	unsigned char buf[512];
	unsigned off = 0, i;
	IO_STATUS_BLOCK iosb;

	memset(buf, 0, sizeof buf);
	for (i = 0; i < count; i++) {
		LXFS_FILE_FULL_EA_INFORMATION *e =
			(LXFS_FILE_FULL_EA_INFORMATION *) (buf + off);
		unsigned nl = (unsigned) strlen(w[i].name);
		unsigned sz = ea_entry_size(&w[i]);

		if (off + sz > sizeof buf)
			return STATUS_BUFFER_OVERFLOW;
		e->Flags = 0;
		e->EaNameLength = (UCHAR) nl;
		e->EaValueLength = (USHORT) w[i].value_len;
		memcpy(e->EaName, w[i].name, nl + 1);
		memcpy(e->EaName + nl + 1, w[i].value, w[i].value_len);
		e->NextEntryOffset = (i + 1 == count) ? 0 : sz;
		off += sz;
	}
	memset(&iosb, 0, sizeof iosb);
	if (nt.SetEaFile == NULL)
		return STATUS_PROCEDURE_NOT_FOUND;
	return nt.SetEaFile(h, &iosb, buf, off);
}

/* Reads every EA on the handle into buf; the caller walks it. */
static NTSTATUS ea_query(HANDLE h, unsigned char *buf, unsigned len)
{
	IO_STATUS_BLOCK iosb;
	memset(buf, 0, len);
	memset(&iosb, 0, sizeof iosb);
	if (nt.QueryEaFile == NULL)
		return STATUS_PROCEDURE_NOT_FOUND;
	return nt.QueryEaFile(h, &iosb, buf, len, FALSE, NULL, 0, NULL, TRUE);
}

/* Finds one EA by name in a queried buffer. NTFS upcases EA names, so the
 * comparison is case-insensitive; returns the value length or -1. */
static int ea_find(const unsigned char *buf, const char *name, void *out,
		   unsigned outlen)
{
	unsigned off = 0;
	for (;;) {
		const LXFS_FILE_FULL_EA_INFORMATION *e =
			(const LXFS_FILE_FULL_EA_INFORMATION *) (buf + off);
		if (e->EaNameLength == strlen(name) &&
		    _strnicmp(e->EaName, name, e->EaNameLength) == 0) {
			unsigned n = e->EaValueLength;
			if (n > outlen)
				n = outlen;
			memcpy(out, e->EaName + e->EaNameLength + 1, n);
			return (int) e->EaValueLength;
		}
		if (e->NextEntryOffset == 0)
			return -1;
		off += e->NextEntryOffset;
	}
}

/* --- reparse points ------------------------------------------------------ */

static NTSTATUS fsctl(HANDLE h, ULONG code, void *in, ULONG inlen,
		      void *out, ULONG outlen, ULONG *written)
{
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;
	memset(&iosb, 0, sizeof iosb);
	if (nt.FsControlFile == NULL)
		return STATUS_PROCEDURE_NOT_FOUND;
	s = nt.FsControlFile(h, NULL, NULL, NULL, &iosb, code,
			     in, inlen, out, outlen);
	if (written != NULL)
		*written = (ULONG) iosb.Information;
	return s;
}

/* Creates an empty file and stamps it with an LX symlink reparse point whose
 * target is the UTF-8 string given. The handle wants write access; nothing
 * here asks for a privilege, which is the point of the case. */
static NTSTATUS make_lx_symlink(const wchar_t *path, const char *target,
				NTSTATUS *create_status)
{
	unsigned char buf[512];
	LXFS_REPARSE_LX_SYMLINK_BUFFER *rp =
		(LXFS_REPARSE_LX_SYMLINK_BUFFER *) buf;
	unsigned tlen = (unsigned) strlen(target);
	unsigned datalen = (unsigned) sizeof(DWORD) + tlen;
	HANDLE h;
	NTSTATUS s;

	*create_status = STATUS_SUCCESS;
	h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			CREATE_NEW,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (h == INVALID_HANDLE_VALUE) {
		*create_status = (NTSTATUS) GetLastError();
		return STATUS_UNSUCCESSFUL;
	}

	memset(buf, 0, sizeof buf);
	rp->ReparseTag = IO_REPARSE_TAG_LX_SYMLINK;
	rp->ReparseDataLength = (WORD) datalen;
	rp->Reserved = 0;
	rp->LxSymlinkReparseBuffer.FileType = 2;
	memcpy(rp->LxSymlinkReparseBuffer.PathBuffer, target, tlen);

	s = fsctl(h, FSCTL_SET_REPARSE_POINT, buf, 8 + datalen, NULL, 0, NULL);
	CloseHandle(h);
	return s;
}

/* The special-file twin of the above: an empty reparse point carrying one of
 * the LX_CHR/LX_BLK/LX_FIFO tags, which is how DrvFs records a device node. */
static NTSTATUS make_lx_special(const wchar_t *path, DWORD tag)
{
	unsigned char buf[16];
	LXFS_REPARSE_BARE_BUFFER *rp = (LXFS_REPARSE_BARE_BUFFER *) buf;
	HANDLE h;
	NTSTATUS s;

	h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			CREATE_NEW,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (h == INVALID_HANDLE_VALUE)
		return STATUS_UNSUCCESSFUL;
	memset(buf, 0, sizeof buf);
	rp->ReparseTag = tag;
	rp->ReparseDataLength = 0;
	rp->Reserved = 0;
	s = fsctl(h, FSCTL_SET_REPARSE_POINT, buf, 8, NULL, 0, NULL);
	CloseHandle(h);
	return s;
}

/* Does this token hold SeCreateSymbolicLinkPrivilege? Context for q4: a tag
 * that needs it would be unusable from an unprivileged kernel, and knowing
 * whether the privilege was merely present is the difference between "did not
 * need it" and "had it anyway". */
static int has_symlink_privilege(void)
{
	HANDLE tok;
	LUID luid;
	PRIVILEGE_SET ps;
	BOOL result = FALSE;

	if (!LookupPrivilegeValueW(NULL, L"SeCreateSymbolicLinkPrivilege",
				   &luid))
		return -1;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
		return -1;
	ps.PrivilegeCount = 1;
	ps.Control = PRIVILEGE_SET_ALL_NECESSARY;
	ps.Privilege[0].Luid = luid;
	ps.Privilege[0].Attributes = 0;
	if (!PrivilegeCheck(tok, &ps, &result))
		result = FALSE;
	CloseHandle(tok);
	return result ? 1 : 0;
}

static int process_is_elevated(void)
{
	HANDLE tok;
	TOKEN_ELEVATION te;
	DWORD n = 0;
	int r = -1;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
		return -1;
	if (GetTokenInformation(tok, TokenElevation, &te, sizeof te, &n))
		r = te.TokenIsElevated ? 1 : 0;
	CloseHandle(tok);
	return r;
}

/* --- the cases ----------------------------------------------------------- */

/* Writes the four metadata EAs onto an open handle. dev is written only when
 * major or minor is non-zero, matching what DrvFs does for a plain file. */
static NTSTATUS write_lx_eas(HANDLE h, ULONG uid, ULONG gid, ULONG mode,
			     ULONG major, ULONG minor)
{
	struct ea_want w[4];
	ULONG dev[2];
	unsigned n = 3;

	w[0].name = "$LXUID"; w[0].value = &uid; w[0].value_len = sizeof uid;
	w[1].name = "$LXGID"; w[1].value = &gid; w[1].value_len = sizeof gid;
	w[2].name = "$LXMOD"; w[2].value = &mode; w[2].value_len = sizeof mode;
	if (major != 0 || minor != 0) {
		dev[0] = major;
		dev[1] = minor;
		w[3].name = "$LXDEV";
		w[3].value = dev;
		w[3].value_len = sizeof dev;
		n = 4;
	}
	return ea_set(h, w, n);
}

static HANDLE open_rw(const wchar_t *path, DWORD disposition)
{
	return CreateFileW(path, GENERIC_READ | GENERIC_WRITE | FILE_WRITE_EA |
			   FILE_READ_EA | FILE_WRITE_ATTRIBUTES,
			   FILE_SHARE_READ | FILE_SHARE_WRITE |
			   FILE_SHARE_DELETE,
			   NULL, disposition, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* q1 and q2 share a specimen, because the interesting comparison is whether
 * the two paths agree on the same file rather than whether each succeeds
 * alone. The specimen carries EAs already, so a stat path that ignored them
 * would be caught here and not only in q3. */
static void case_stat(void)
{
	wchar_t p[4200];
	LXFS_FILE_STAT_LX_INFORMATION a, b;
	NTSTATUS sa, sb;
	HANDLE h;
	int fields_agree = 0;

	joinp(p, sizeof p / sizeof p[0], L"statspec");
	h = open_rw(p, CREATE_ALWAYS);
	if (h == INVALID_HANDLE_VALUE) {
		printf("q1_specimen_created=0\n");
		return;
	}
	printf("q1_specimen_created=1\n");
	st("q1_specimen_ea_status", write_lx_eas(h, 1000, 1000, 0100644, 0, 0));

	sa = stat_lx_handle(h, &a);
	CloseHandle(h);
	sb = stat_lx_name(p, &b);

	num("q1_class", LXFS_FileStatLxInformation);
	num("q1_struct_bytes", sizeof a);
	st("q1_stat_lx_by_handle_status", sa);
	num("q1_stat_lx_by_handle", NT_SUCCESS(sa) ? 1 : 0);
	if (NT_SUCCESS(sa)) {
		emit_lx("q1", &a);
		/* Decimal, not hex: this is the NTFS file reference proposal
		 * 0011 uses for st_ino, it changes every run, and a hex form
		 * with letters in it does not reduce to a plain number for the
		 * regeneration runner that compares these transcripts. */
		num("q1_file_id", (unsigned long long) a.FileId.QuadPart);
		num("q1_nlink", a.NumberOfLinks);
		/* "Present" means the class filled them, which the HAS_ bits in
		 * LxFlags assert; a zeroed LxUid with no HAS_UID bit would be
		 * an absent field wearing a plausible value. */
		num("q1_lx_fields_present",
		    (a.LxFlags & (LX_FILE_METADATA_HAS_UID |
				  LX_FILE_METADATA_HAS_GID |
				  LX_FILE_METADATA_HAS_MODE)) ==
		    (LX_FILE_METADATA_HAS_UID | LX_FILE_METADATA_HAS_GID |
		     LX_FILE_METADATA_HAS_MODE) ? 1 : 0);
	} else {
		num("q1_lx_fields_present", 0);
	}

	st("q2_stat_lx_by_name_status", sb);
	num("q2_stat_lx_by_name", NT_SUCCESS(sb) ? 1 : 0);
	num("q2_entry_present", nt.QueryInformationByName != NULL ? 1 : 0);
	if (NT_SUCCESS(sb)) {
		emit_lx("q2", &b);
		fields_agree = NT_SUCCESS(sa) &&
			a.LxFlags == b.LxFlags && a.LxUid == b.LxUid &&
			a.LxGid == b.LxGid && a.LxMode == b.LxMode &&
			a.FileId.QuadPart == b.FileId.QuadPart;
	}
	num("q2_agrees_with_handle", fields_agree ? 1 : 0);
}

/* q3 is two findings wearing one number, and they are reported apart: whether
 * NtSetEaFile/NtQueryEaFile round-trip the four names, and whether
 * FileStatLxInformation then reports what was written. A file system could do
 * the first and not the second, in which case the EAs are inert storage and
 * the fast stat path is telling the caller nothing. */
static void case_eas(void)
{
	wchar_t p[4200], d[4200];
	unsigned char buf[2048];
	HANDLE h;
	NTSTATUS sset, squery, sdev;
	ULONG uid = 0, gid = 0, mode = 0, dev[2] = { 0, 0 };
	LXFS_FILE_STAT_LX_INFORMATION s;
	int roundtrip;

	joinp(p, sizeof p / sizeof p[0], L"eafile");
	h = open_rw(p, CREATE_ALWAYS);
	if (h == INVALID_HANDLE_VALUE) {
		printf("q3_specimen_created=0\n");
		return;
	}
	printf("q3_specimen_created=1\n");

	sset = write_lx_eas(h, 4242, 4343, 0100751, 0, 0);
	st("q3_ea_set_status", sset);
	squery = ea_query(h, buf, sizeof buf);
	st("q3_ea_query_status", squery);
	if (NT_SUCCESS(squery)) {
		ea_find(buf, "$LXUID", &uid, sizeof uid);
		ea_find(buf, "$LXGID", &gid, sizeof gid);
		ea_find(buf, "$LXMOD", &mode, sizeof mode);
	}
	num("q3_ea_uid_readback", uid);
	num("q3_ea_gid_readback", gid);
	oct("q3_ea_mode_readback", mode);
	roundtrip = NT_SUCCESS(sset) && NT_SUCCESS(squery) &&
		uid == 4242 && gid == 4343 && mode == 0100751;
	num("q3_lx_eas_written", roundtrip ? 1 : 0);

	st("q3_stat_status", stat_lx_handle(h, &s));
	CloseHandle(h);
	emit_lx("q3", &s);
	num("q3_stat_reflects_eas",
	    (s.LxUid == 4242 && s.LxGid == 4343 && s.LxMode == 0100751 &&
	     (s.LxFlags & LX_FILE_METADATA_HAS_UID) != 0) ? 1 : 0);

	/* The device node. $LXDEV alone is not a device: DrvFs wants the
	 * LX_CHR reparse tag as well, so both go on and both are read back. */
	joinp(d, sizeof d / sizeof d[0], L"chardev");
	sdev = make_lx_special(d, IO_REPARSE_TAG_LX_CHR);
	st("q3_dev_reparse_status", sdev);
	h = CreateFileW(d, GENERIC_READ | GENERIC_WRITE | FILE_WRITE_EA |
			FILE_READ_EA, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (h == INVALID_HANDLE_VALUE) {
		num("q3_dev_eas_written", 0);
		num("q3_dev_stat_reflects_eas", 0);
		return;
	}
	st("q3_dev_ea_set_status", write_lx_eas(h, 0, 0, 020666, 1, 3));
	squery = ea_query(h, buf, sizeof buf);
	if (NT_SUCCESS(squery))
		ea_find(buf, "$LXDEV", dev, sizeof dev);
	num("q3_dev_major_readback", dev[0]);
	num("q3_dev_minor_readback", dev[1]);
	num("q3_dev_eas_written",
	    (NT_SUCCESS(squery) && dev[0] == 1 && dev[1] == 3) ? 1 : 0);
	st("q3_dev_stat_status", stat_lx_handle(h, &s));
	CloseHandle(h);
	emit_lx("q3_dev", &s);
	hex("q3_dev_reparse_tag", s.ReparseTag);
	num("q3_dev_stat_reflects_eas",
	    (s.LxDeviceIdMajor == 1 && s.LxDeviceIdMinor == 3 &&
	     (s.LxFlags & LX_FILE_METADATA_HAS_DEVICE_ID) != 0) ? 1 : 0);
}

static void case_symlink(void)
{
	wchar_t p[4200];
	unsigned char buf[1024];
	const LXFS_REPARSE_LX_SYMLINK_BUFFER *rp =
		(const LXFS_REPARSE_LX_SYMLINK_BUFFER *) buf;
	const char *target = "statspec";
	NTSTATUS sset, sget, screate;
	HANDLE h;
	ULONG got = 0;
	char back[512];
	unsigned tlen;

	num("q4_privilege_held", has_symlink_privilege());
	num("q4_process_elevated", process_is_elevated());

	joinp(p, sizeof p / sizeof p[0], L"lxlink");
	sset = make_lx_symlink(p, target, &screate);
	st("q4_create_status", screate);
	st("q4_set_reparse_status", sset);
	num("q4_lx_symlink_created", NT_SUCCESS(sset) ? 1 : 0);
	/* The privilege question, answered rather than assumed: the run asked
	 * for nothing, so a success here means the tag is unprivileged unless
	 * the token happened to carry the privilege already. */
	num("q4_needed_privilege",
	    NT_SUCCESS(sset) ? 0 : (has_symlink_privilege() == 0 ? 1 : 0));

	memset(buf, 0, sizeof buf);
	h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
			FILE_FLAG_OPEN_REPARSE_POINT | FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (h == INVALID_HANDLE_VALUE) {
		num("q4_readback", 0);
		num("q4_target_survives", 0);
		return;
	}
	sget = fsctl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, buf, sizeof buf,
		     &got);
	CloseHandle(h);
	st("q4_get_reparse_status", sget);
	num("q4_readback", NT_SUCCESS(sget) ? 1 : 0);
	if (!NT_SUCCESS(sget)) {
		num("q4_target_survives", 0);
		return;
	}
	hex("q4_tag", rp->ReparseTag);
	num("q4_tag_matches",
	    rp->ReparseTag == IO_REPARSE_TAG_LX_SYMLINK ? 1 : 0);
	num("q4_file_type", rp->LxSymlinkReparseBuffer.FileType);
	tlen = rp->ReparseDataLength >= sizeof(DWORD)
		? rp->ReparseDataLength - (unsigned) sizeof(DWORD) : 0;
	if (tlen >= sizeof back)
		tlen = sizeof back - 1;
	memcpy(back, rp->LxSymlinkReparseBuffer.PathBuffer, tlen);
	back[tlen] = '\0';
	printf("q4_target=%s\n", back);
	num("q4_target_survives", strcmp(back, target) == 0 ? 1 : 0);
}

/* q5 is the one case that could plausibly want an administrator, and if it
 * does the refusal is the finding rather than a reason to go and get one. The
 * two names are created through NtCreateFile without OBJ_CASE_INSENSITIVE,
 * which is how a Linux-facing caller would create them; the coexistence check
 * then enumerates the directory and compares names byte for byte, because
 * asking the file system to open "makefile" would not distinguish a second
 * file from a case-insensitive hit on the first. */
static void case_case_sensitive(void)
{
	wchar_t dir[4200], a[4300], b[4300], pat[4300];
	LXFS_FILE_CASE_SENSITIVE_INFORMATION csi;
	LXFS_FILE_STAT_LX_INFORMATION s;
	IO_STATUS_BLOCK iosb;
	WIN32_FIND_DATAW fd;
	HANDLE h, find;
	NTSTATUS sset;
	int upper = 0, lower = 0;

	joinp(dir, sizeof dir / sizeof dir[0], L"csdir");
	if (!CreateDirectoryW(dir, NULL)) {
		printf("q5_dir_created=0\n");
		return;
	}
	printf("q5_dir_created=1\n");
	num("q5_class", LXFS_FileCaseSensitiveInformation);
	num("q5_process_elevated", process_is_elevated());

	h = CreateFileW(dir, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		printf("q5_dir_opened=0\n");
		return;
	}
	printf("q5_dir_opened=1\n");
	csi.Flags = FILE_CS_FLAG_CASE_SENSITIVE_DIR;
	memset(&iosb, 0, sizeof iosb);
	sset = nt.SetInformationFile(h, &iosb, &csi, sizeof csi,
				     LXFS_FileCaseSensitiveInformation);
	CloseHandle(h);
	st("q5_set_status", sset);
	num("q5_case_sensitive_set", NT_SUCCESS(sset) ? 1 : 0);

	swprintf(a, sizeof a / sizeof a[0], L"%ls\\Makefile", dir);
	swprintf(b, sizeof b / sizeof b[0], L"%ls\\makefile", dir);
	{
		wchar_t ntp[4400];
		UNICODE_STRING us;
		OBJECT_ATTRIBUTES oa;
		HANDLE fh;
		NTSTATUS s1, s2;
		const wchar_t *names[2];
		NTSTATUS out[2];
		int i;

		names[0] = a;
		names[1] = b;
		for (i = 0; i < 2; i++) {
			lxfs_nt_path(&us, ntp,
				     sizeof ntp / sizeof ntp[0], names[i]);
			InitializeObjectAttributes(&oa, &us, 0, NULL, NULL);
			memset(&iosb, 0, sizeof iosb);
			out[i] = nt.CreateFile(&fh, GENERIC_WRITE | SYNCHRONIZE,
					       &oa, &iosb, NULL,
					       FILE_ATTRIBUTE_NORMAL, 0,
					       FILE_CREATE,
					       FILE_NON_DIRECTORY_FILE |
					       FILE_SYNCHRONOUS_IO_NONALERT,
					       NULL, 0);
			if (NT_SUCCESS(out[i]))
				CloseHandle(fh);
		}
		s1 = out[0];
		s2 = out[1];
		st("q5_create_upper_status", s1);
		st("q5_create_lower_status", s2);
	}

	swprintf(pat, sizeof pat / sizeof pat[0], L"%ls\\*", dir);
	find = FindFirstFileW(pat, &fd);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(fd.cFileName, L"Makefile") == 0)
				upper = 1;
			else if (wcscmp(fd.cFileName, L"makefile") == 0)
				lower = 1;
		} while (FindNextFileW(find, &fd));
		FindClose(find);
	}
	num("q5_both_names_coexist", (upper && lower) ? 1 : 0);

	st("q5_dir_stat_status", stat_lx_name(dir, &s));
	hex("q5_dir_lx_flags", s.LxFlags);
	num("q5_dir_flag_reports_case_sensitive",
	    (s.LxFlags & LX_FILE_CASE_SENSITIVE_DIR) ? 1 : 0);
}

/* Linux unlink, asked of NTFS, in the shape a VFS would actually use: one
 * descriptor stays open on the file while a second, DELETE-only handle carries
 * the disposition and is closed. Modelling it that way matters, because the
 * two halves of the answer separate exactly there -- the name survives the
 * SetInformationFile call and leaves the directory when the deleting handle
 * closes, which is what Cygwin's unlink_nt does at syscalls.cc:768 and is not
 * what "the name is removed immediately" would predict.
 *
 * The check that decides the case is not that the call returned success.
 * Windows delete-on-close returns success too. It is that the name became
 * free for a new file while the first descriptor was still reading and
 * writing the old one. */
static void case_posix_delete(void)
{
	wchar_t p[4200];
	LXFS_FILE_DISPOSITION_INFORMATION_EX di;
	IO_STATUS_BLOCK iosb;
	HANDLE keep, del, fresh;
	NTSTATUS s;
	DWORD wrote = 0, attrs;
	const char payload[] = "still here";

	joinp(p, sizeof p / sizeof p[0], L"posixdel");
	keep = CreateFileW(p, GENERIC_READ | GENERIC_WRITE,
			   FILE_SHARE_READ | FILE_SHARE_WRITE |
			   FILE_SHARE_DELETE, NULL, CREATE_ALWAYS,
			   FILE_ATTRIBUTE_NORMAL, NULL);
	if (keep == INVALID_HANDLE_VALUE) {
		printf("q6_specimen_created=0\n");
		return;
	}
	printf("q6_specimen_created=1\n");
	num("q6_class", LXFS_FileDispositionInformationEx);

	del = CreateFileW(p, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE |
			  FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
			  FILE_ATTRIBUTE_NORMAL, NULL);
	if (del == INVALID_HANDLE_VALUE) {
		printf("q6_delete_handle=0\n");
		CloseHandle(keep);
		return;
	}
	printf("q6_delete_handle=1\n");

	di.Flags = FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS;
	memset(&iosb, 0, sizeof iosb);
	s = nt.SetInformationFile(del, &iosb, &di, sizeof di,
				  LXFS_FileDispositionInformationEx);
	st("q6_disposition_status", s);
	num("q6_posix_delete", NT_SUCCESS(s) ? 1 : 0);

	/* Before the deleting handle closes. Recorded because it is the one
	 * place the NT semantics differ from Linux's, and a VFS that expected
	 * the name to be free here would race with itself. */
	attrs = GetFileAttributesW(p);
	num("q6_lookup_error_before_close",
	    attrs == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0);
	num("q6_name_gone_before_close",
	    attrs == INVALID_FILE_ATTRIBUTES &&
	    GetLastError() == ERROR_FILE_NOT_FOUND ? 1 : 0);

	CloseHandle(del);

	attrs = GetFileAttributesW(p);
	num("q6_lookup_error", attrs == INVALID_FILE_ATTRIBUTES ?
	    GetLastError() : 0);
	num("q6_name_gone", attrs == INVALID_FILE_ATTRIBUTES &&
	    GetLastError() == ERROR_FILE_NOT_FOUND ? 1 : 0);

	/* The property proposal 0011 leans on: the name is reusable at once,
	 * with the old file still open underneath it. */
	fresh = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ |
			    FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fresh == INVALID_HANDLE_VALUE) {
		num("q6_name_reusable", 0);
		num("q6_reuse_error", GetLastError());
	} else {
		num("q6_name_reusable", 1);
		num("q6_reuse_error", 0);
		CloseHandle(fresh);
		DeleteFileW(p);
	}

	/* And the descriptor is still a working file, not merely a live
	 * kernel object: write, seek, read back. */
	if (WriteFile(keep, payload, (DWORD) sizeof payload - 1, &wrote,
		      NULL) && wrote == sizeof payload - 1) {
		char back[32];
		DWORD got = 0;
		SetFilePointer(keep, 0, NULL, FILE_BEGIN);
		num("q6_handle_usable",
		    (ReadFile(keep, back, wrote, &got, NULL) && got == wrote &&
		     memcmp(back, payload, wrote) == 0) ? 1 : 0);
	} else {
		num("q6_handle_usable", 0);
	}
	CloseHandle(keep);
}

/* q9. Three NTFS behaviours that bit WSL1's DrvFs and that the VFS design has
 * not met: a rename over a target somebody holds open, a POSIX delete and a
 * truncate of a file with a live section view (git gc over a mapped pack,
 * BerkeleyDB under rpm), and a directory rename while a handle is open
 * beneath it. Each is asked with the call the VFS would use, and the Win32
 * shape beside it where the two differ. */
#define LXFS_FileRenameInformationEx 65
#define FILE_RENAME_REPLACE_IF_EXISTS 0x1
#define FILE_RENAME_POSIX_SEMANTICS   0x2

typedef struct {
	ULONG Flags;
	HANDLE RootDirectory;
	ULONG FileNameLength;
	WCHAR FileName[1];
} LXFS_FILE_RENAME_INFORMATION_EX;

static NTSTATUS posix_rename(HANDLE h, const wchar_t *target_win32)
{
	wchar_t ntpath[4300];
	IO_STATUS_BLOCK iosb;
	LXFS_FILE_RENAME_INFORMATION_EX *ri;
	size_t len, bytes;
	NTSTATUS s;

	wcscpy(ntpath, L"\\??\\");
	wcscat(ntpath, target_win32);
	len = wcslen(ntpath);
	bytes = sizeof *ri + len * sizeof(wchar_t);
	ri = calloc(1, bytes);
	if (!ri)
		return (NTSTATUS) 0xC0000017;
	ri->Flags = FILE_RENAME_REPLACE_IF_EXISTS | FILE_RENAME_POSIX_SEMANTICS;
	ri->RootDirectory = NULL;
	ri->FileNameLength = (ULONG) (len * sizeof(wchar_t));
	memcpy(ri->FileName, ntpath, len * sizeof(wchar_t));
	memset(&iosb, 0, sizeof iosb);
	s = nt.SetInformationFile(h, &iosb, ri, (ULONG) bytes, LXFS_FileRenameInformationEx);
	free(ri);
	return s;
}

static void case_mapped_and_rename(void)
{
	wchar_t src[4200], dst[4200], f[4200], d1[4200], d2[4200], child[4200];
	HANDLE tgt, mover, mapped, view_h, sec, chandle;
	void *view;
	LXFS_FILE_DISPOSITION_INFORMATION_EX di;
	IO_STATUS_BLOCK iosb;
	NTSTATUS s;
	DWORD attrs;

	/* rename over an open target: Win32 replace first, then the POSIX class */
	joinp(src, sizeof src / sizeof src[0], L"ren-src");
	joinp(dst, sizeof dst / sizeof dst[0], L"ren-dst");
	tgt = open_rw(dst, CREATE_ALWAYS);
	mover = open_rw(src, CREATE_ALWAYS);
	num("q9_rename_specimens", (tgt != INVALID_HANDLE_VALUE && mover != INVALID_HANDLE_VALUE) ? 1 : 0);
	if (tgt != INVALID_HANDLE_VALUE && mover != INVALID_HANDLE_VALUE) {
		CloseHandle(mover);
		num("q9_win32_replace_over_open", MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING) ? 1 : 0);
		num("q9_win32_replace_error", MoveFileExW(src, dst, MOVEFILE_REPLACE_EXISTING) ? 0 : GetLastError());
		mover = CreateFileW(src, DELETE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
				    FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (mover != INVALID_HANDLE_VALUE) {
			s = posix_rename(mover, dst);
			st("q9_posix_rename_over_open_status", s);
			num("q9_posix_rename_over_open", NT_SUCCESS(s) ? 1 : 0);
			CloseHandle(mover);
			attrs = GetFileAttributesW(src);
			num("q9_posix_rename_source_gone", attrs == INVALID_FILE_ATTRIBUTES ? 1 : 0);
			/* the old target's handle still reads its own file, as an
			 * unlinked inode would on Linux */
			{
				char buf[8];
				DWORD got = 0;
				SetFilePointer(tgt, 0, NULL, FILE_BEGIN);
				num("q9_old_target_handle_usable",
				    ReadFile(tgt, buf, sizeof buf, &got, NULL) ? 1 : 0);
			}
		} else {
			num("q9_posix_rename_over_open", 0);
		}
	}
	if (tgt != INVALID_HANDLE_VALUE) CloseHandle(tgt);
	DeleteFileW(dst);
	DeleteFileW(src);

	/* a file with a live section view: POSIX delete, then truncate */
	joinp(f, sizeof f / sizeof f[0], L"mapped");
	mapped = open_rw(f, CREATE_ALWAYS);
	view = NULL;
	sec = NULL;
	if (mapped != INVALID_HANDLE_VALUE) {
		DWORD wrote = 0;
		char zero[8192];
		memset(zero, 0x41, sizeof zero);
		WriteFile(mapped, zero, sizeof zero, &wrote, NULL);
		sec = CreateFileMappingW(mapped, NULL, PAGE_READWRITE, 0, 0, NULL);
		view = sec ? MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0) : NULL;
	}
	num("q9_mapped_specimen", view ? 1 : 0);
	if (view) {
		/* delete while mapped: a second handle for DELETE, POSIX semantics */
		view_h = CreateFileW(f, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				     NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (view_h != INVALID_HANDLE_VALUE) {
			di.Flags = FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS;
			memset(&iosb, 0, sizeof iosb);
			s = nt.SetInformationFile(view_h, &iosb, &di, sizeof di, LXFS_FileDispositionInformationEx);
			st("q9_posix_delete_mapped_status", s);
			num("q9_posix_delete_mapped", NT_SUCCESS(s) ? 1 : 0);
			CloseHandle(view_h);
			attrs = GetFileAttributesW(f);
			num("q9_mapped_name_gone", attrs == INVALID_FILE_ATTRIBUTES ? 1 : 0);
			num("q9_view_still_reads", *(volatile char *) view == 0x41 ? 1 : 0);
		} else {
			num("q9_posix_delete_mapped", 0);
		}
		/* truncate while mapped: SetEndOfFile below the view */
		{
			LARGE_INTEGER z;
			z.QuadPart = 4096;
			num("q9_truncate_mapped",
			    (SetFilePointerEx(mapped, z, NULL, FILE_BEGIN) && SetEndOfFile(mapped)) ? 1 : 0);
			num("q9_truncate_mapped_error", GetLastError());
		}
		UnmapViewOfFile(view);
		CloseHandle(sec);
		/* and after the view is gone: does the truncate go through now */
		{
			LARGE_INTEGER z;
			z.QuadPart = 4096;
			num("q9_truncate_after_unmap",
			    (SetFilePointerEx(mapped, z, NULL, FILE_BEGIN) && SetEndOfFile(mapped)) ? 1 : 0);
		}
	}
	if (mapped != INVALID_HANDLE_VALUE) CloseHandle(mapped);
	DeleteFileW(f);

	/* a directory renamed while a handle is open beneath it */
	joinp(d1, sizeof d1 / sizeof d1[0], L"dir-before");
	joinp(d2, sizeof d2 / sizeof d2[0], L"dir-after");
	joinp(child, sizeof child / sizeof child[0], L"dir-before\\child");
	RemoveDirectoryW(d2);
	num("q9_dir_specimen", CreateDirectoryW(d1, NULL) ? 1 : 0);
	chandle = open_rw(child, CREATE_ALWAYS);
	if (chandle != INVALID_HANDLE_VALUE) {
		num("q9_dir_rename_with_open_child", MoveFileExW(d1, d2, 0) ? 1 : 0);
		num("q9_dir_rename_error", GetLastError());
		CloseHandle(chandle);
		num("q9_dir_rename_after_close", MoveFileExW(d1, d2, 0) ? 1 : 0);
		DeleteFileW(child);
		{
			wchar_t child2[4200];
			joinp(child2, sizeof child2 / sizeof child2[0], L"dir-after\\child");
			DeleteFileW(child2);
		}
	}
	RemoveDirectoryW(d1);
	RemoveDirectoryW(d2);
}

/* The tree q7 reads back from WSL and from Cygwin. Nothing here is checked by
 * the probe; the harness compares what the two readers say against these
 * constants, which are also printed so the transcript carries the intent
 * beside the observation. */
static void build_interop_tree(void)
{
	wchar_t dir[4200], p[4300];
	HANDLE h;
	NTSTATUS screate;
	DWORD wrote;

	joinp(dir, sizeof dir / sizeof dir[0], L"tree");
	if (!CreateDirectoryW(dir, NULL) &&
	    GetLastError() != ERROR_ALREADY_EXISTS) {
		printf("q7_tree_built=0\n");
		return;
	}

	swprintf(p, sizeof p / sizeof p[0], L"%ls\\file644", dir);
	h = open_rw(p, CREATE_ALWAYS);
	if (h != INVALID_HANDLE_VALUE) {
		WriteFile(h, "regular\n", 8, &wrote, NULL);
		write_lx_eas(h, 1000, 1000, 0100644, 0, 0);
		CloseHandle(h);
	}

	swprintf(p, sizeof p / sizeof p[0], L"%ls\\file755", dir);
	h = open_rw(p, CREATE_ALWAYS);
	if (h != INVALID_HANDLE_VALUE) {
		WriteFile(h, "#!/bin/sh\n", 10, &wrote, NULL);
		write_lx_eas(h, 1234, 5678, 0100755, 0, 0);
		CloseHandle(h);
	}

	swprintf(p, sizeof p / sizeof p[0], L"%ls\\link", dir);
	make_lx_symlink(p, "file644", &screate);

	swprintf(p, sizeof p / sizeof p[0], L"%ls\\nulldev", dir);
	if (NT_SUCCESS(make_lx_special(p, IO_REPARSE_TAG_LX_CHR))) {
		h = CreateFileW(p, GENERIC_READ | GENERIC_WRITE |
				FILE_WRITE_EA | FILE_READ_EA,
				FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
				OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT |
				FILE_ATTRIBUTE_NORMAL, NULL);
		if (h != INVALID_HANDLE_VALUE) {
			write_lx_eas(h, 0, 0, 020666, 1, 3);
			CloseHandle(h);
		}
	}

	printf("q7_tree_built=1\n");
	printf("q7_want_file644=644 1000 1000\n");
	printf("q7_want_file755=755 1234 5678\n");
	printf("q7_want_link=file644\n");
	printf("q7_want_nulldev=666 1 3\n");
}

/* q8 is a measurement, not a finding: the numbers move with the cache, the
 * disk and whatever else the host is doing, and nothing in the verdict may
 * turn on them. What they are for is scale -- whether the fast stat path is in
 * the same order of magnitude as Cygwin's stat() or an order better. The
 * Cygwin half runs from a separate binary in the harness, since a mingw
 * process has no Cygwin stat() to call. */
static void case_stat_rate(void)
{
	wchar_t dir[4200], p[4300];
	LXFS_FILE_STAT_LX_INFORMATION s;
	LARGE_INTEGER freq, t0, t1;
	HANDLE h;
	int i, ok = 0;
	double ns;

	joinp(dir, sizeof dir / sizeof dir[0], L"bench");
	if (!CreateDirectoryW(dir, NULL) &&
	    GetLastError() != ERROR_ALREADY_EXISTS) {
		num("q8_files", 0);
		return;
	}
	for (i = 0; i < bench_count; i++) {
		swprintf(p, sizeof p / sizeof p[0], L"%ls\\f%06d", dir, i);
		h = CreateFileW(p, GENERIC_WRITE | FILE_WRITE_EA, 0, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE)
			continue;
		write_lx_eas(h, 1000, 1000, 0100644, 0, 0);
		CloseHandle(h);
	}
	vnote("bench tree of %d files built", bench_count);

	/* One warm pass first, then the measured pass, so the number is a
	 * warm-cache stat rate on both sides of the comparison. */
	for (i = 0; i < bench_count; i++) {
		swprintf(p, sizeof p / sizeof p[0], L"%ls\\f%06d", dir, i);
		stat_lx_name(p, &s);
	}
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&t0);
	for (i = 0; i < bench_count; i++) {
		swprintf(p, sizeof p / sizeof p[0], L"%ls\\f%06d", dir, i);
		if (NT_SUCCESS(stat_lx_name(p, &s)))
			ok++;
	}
	QueryPerformanceCounter(&t1);

	ns = (double) (t1.QuadPart - t0.QuadPart) * 1e9 /
		(double) freq.QuadPart / (double) bench_count;
	num("q8_files", bench_count);
	num("q8_byname_ok", ok);
	printf("q8_byname_ns_per_stat=%.0f\n", ns);
}

/* The probe removes its own leavings, because two of the things it creates --
 * the LX_CHR reparse points -- are character devices to Cygwin's rm and it
 * refuses them. DeleteFileW deletes the reparse point rather than following
 * it, which is what is wanted. */
static int clean_tree(const wchar_t *dir)
{
	wchar_t pat[4300], child[4300];
	WIN32_FIND_DATAW fd;
	HANDLE find;
	int failures = 0;

	swprintf(pat, sizeof pat / sizeof pat[0], L"%ls\\*", dir);
	find = FindFirstFileW(pat, &fd);
	if (find != INVALID_HANDLE_VALUE) {
		do {
			if (wcscmp(fd.cFileName, L".") == 0 ||
			    wcscmp(fd.cFileName, L"..") == 0)
				continue;
			swprintf(child, sizeof child / sizeof child[0],
				 L"%ls\\%ls", dir, fd.cFileName);
			if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			    !(fd.dwFileAttributes &
			      FILE_ATTRIBUTE_REPARSE_POINT))
				failures += clean_tree(child);
			else if (!DeleteFileW(child))
				failures++;
		} while (FindNextFileW(find, &fd));
		FindClose(find);
	}
	if (!RemoveDirectoryW(dir))
		failures++;
	return failures;
}

static void usage(void)
{
	printf("Usage: lxfs-probe [options] <directory>\n"
	       "  -n, --count=N   files in the stat-rate tree [default: 2000]\n"
	       "  -v, --verbose   progress to stderr\n"
	       "  -V, --version   print the version and exit\n"
	       "  -h, --help      print this message and exit\n");
}

int main(int argc, char **argv)
{
	const char *dir = NULL;
	int missing, i;

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
			usage();
			return 0;
		} else if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
			printf("%s\n", RELEASE);
			return 0;
		} else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
			verbose = 1;
		} else if (strcmp(a, "-c") == 0 || strcmp(a, "--clean") == 0) {
			clean = 1;
		} else if (strncmp(a, "--count=", 8) == 0) {
			bench_count = atoi(a + 8);
		} else if (strcmp(a, "-n") == 0 && i + 1 < argc) {
			bench_count = atoi(argv[++i]);
		} else if (a[0] == '-') {
			fprintf(stderr, "lxfs-probe: unknown option %s\n", a);
			return 2;
		} else {
			dir = a;
		}
	}
	if (dir == NULL) {
		fprintf(stderr, "lxfs-probe: a directory is required\n");
		return 2;
	}
	if (bench_count < 1 || bench_count > 200000) {
		fprintf(stderr, "lxfs-probe: implausible count\n");
		return 2;
	}
	if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, root,
				sizeof root / sizeof root[0]) == 0) {
		fprintf(stderr, "lxfs-probe: cannot widen the directory\n");
		return 2;
	}

	if (clean) {
		int failures = clean_tree(root);
		if (failures != 0)
			fprintf(stderr,
				"lxfs-probe: %d entries would not delete\n",
				failures);
		return failures == 0 ? 0 : 1;
	}

	missing = lxfs_nt_bind(&nt);
	num("nt_entrypoints_missing", missing);
	if (nt.QueryInformationFile == NULL || nt.SetInformationFile == NULL ||
	    nt.CreateFile == NULL) {
		fprintf(stderr, "lxfs-probe: ntdll is missing the basics\n");
		return 1;
	}

	printf("probe=%s\n", RELEASE);
	{
		wchar_t vol[MAX_PATH];
		wchar_t fsname[64];
		DWORD serial = 0, comp = 0, flags = 0;
		wchar_t rootpath[8];
		char fsn[64];

		if (root[1] == L':') {
			swprintf(rootpath, 8, L"%lc:\\", root[0]);
			if (GetVolumeInformationW(rootpath, vol, MAX_PATH,
						  &serial, &comp, &flags,
						  fsname, 64)) {
				WideCharToMultiByte(CP_UTF8, 0, fsname, -1,
						    fsn, sizeof fsn, NULL,
						    NULL);
				printf("volume_fs=%s\n", fsn);
				hex("volume_flags", flags);
			}
		}
	}

	case_stat();
	case_eas();
	case_symlink();
	case_case_sensitive();
	case_posix_delete();
	case_mapped_and_rename();
	build_interop_tree();
	case_stat_rate();
	return 0;
}
