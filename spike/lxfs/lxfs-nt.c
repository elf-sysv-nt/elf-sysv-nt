/* Binding half of the DrvFs metadata probe: everything that reaches into
 * ntdll, kept apart from the cases so lxfs-probe.c reads as a list of
 * questions rather than as a list of GetProcAddress calls.
 */
#include "lxfs-nt.h"
#include <wchar.h>

int lxfs_nt_bind(struct lxfs_nt *nt)
{
	HMODULE dll = GetModuleHandleW(L"ntdll.dll");
	int missing = 0;

	memset(nt, 0, sizeof *nt);
	if (dll == NULL)
		return 7;

#define BIND(field, name) \
	do { \
		nt->field = (lxfs_Nt##name##_t) (void *) \
			GetProcAddress(dll, "Nt" #name); \
		if (nt->field == NULL) \
			missing++; \
	} while (0)

	BIND(QueryInformationFile, QueryInformationFile);
	BIND(SetInformationFile, SetInformationFile);
	BIND(QueryInformationByName, QueryInformationByName);
	BIND(SetEaFile, SetEaFile);
	BIND(QueryEaFile, QueryEaFile);
	BIND(FsControlFile, FsControlFile);
	BIND(CreateFile, CreateFile);
#undef BIND

	return missing;
}

void lxfs_nt_path(UNICODE_STRING *us, wchar_t *buf, size_t buf_wchars,
		  const wchar_t *win32path)
{
	size_t n;

	buf[0] = L'\0';
	wcsncat(buf, L"\\??\\", buf_wchars - 1);
	wcsncat(buf, win32path, buf_wchars - wcslen(buf) - 1);
	n = wcslen(buf);
	us->Buffer = buf;
	us->Length = (USHORT) (n * sizeof(wchar_t));
	us->MaximumLength = (USHORT) ((n + 1) * sizeof(wchar_t));
}
