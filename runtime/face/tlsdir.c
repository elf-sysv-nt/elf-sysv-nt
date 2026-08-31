/*
 * tlsdir.c -- the faced DLL's PE TLS directory (WP-27).
 *
 * WP-22 certified the TLS-callback shape at ABI and unwind width and noted
 * that the loader firing it needs a DLL with a TLS directory, which did not
 * exist then.  The faced DLL exists now, so this unit gives it the directory:
 * `_tls_used` is the symbol the PE linker publishes as the image's TLS data
 * directory entry, and the callback array it names is what the host's own
 * loader walks -- at LoadLibrary for the process, at thread creation and exit
 * for each thread the process makes afterwards.
 *
 * Observation seam.  Like entry.c's stand-in cores, a callback has no work
 * whose completion a test could otherwise see from outside the DLL, and the
 * export surface is closed by face.din, so the record goes where a plain PE
 * test process can read it: one environment variable, written with kernel32
 * alone.  The callback may run under the loader lock before dll_entry has
 * prepared the DLL's own libc, so nothing here touches the CRT -- the counts
 * are formatted by hand.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/*
 * The DLL links with --gc-sections and nothing in the image references this
 * unit -- the loader is its only caller -- so every object here sits in a
 * named section the vendor's linker script keeps (KEEP in cygwin.sc.in),
 * writable and read-only halves apart.
 */
#define TLS_DATA   __attribute__((section(".data_tlsdir")))
#define TLS_RODATA __attribute__((used, section(".rdata_tlsdir")))

/* One count per DllMain-style reason: 0 unused, 1 process attach, 2 thread
 * attach, 3 thread detach.  Process detach stays uncounted because the DLL
 * beneath this face is Cygwin, which does not support being unloaded. */
static TLS_DATA volatile LONG elfsysv_tls_counts[4];

static void elfsysv_tls_record(void)
{
	char buf[64];
	char *p = buf;
	int i;

	for (i = 1; i <= 3; i++) {
		LONG v = elfsysv_tls_counts[i];
		char tmp[12];
		int n = 0;

		if (i > 1)
			*p++ = ' ';
		do {
			tmp[n++] = (char)('0' + v % 10);
			v /= 10;
		} while (v);
		while (n)
			*p++ = tmp[--n];
	}
	*p = '\0';
	SetEnvironmentVariableA("ELFSYSV_TLS_OBSERVED", buf);
}

static void NTAPI elfsysv_tls_observe(PVOID dll, DWORD reason, PVOID reserved)
{
	(void)dll; (void)reserved;
	if (reason >= 1 && reason <= 3)
		InterlockedIncrement((volatile LONG *)&elfsysv_tls_counts[reason]);
	elfsysv_tls_record();
}

/* The directory proper.  The raw-data range is one byte so every field is
 * real; the loader still allocates the per-thread block and runs the list. */
static TLS_DATA char elfsysv_tls_raw[1];
static TLS_DATA ULONG elfsysv_tls_index;

static TLS_RODATA const PIMAGE_TLS_CALLBACK elfsysv_tls_callbacks[] = {
	elfsysv_tls_observe,
	NULL
};

TLS_RODATA const IMAGE_TLS_DIRECTORY64 _tls_used = {
	(ULONGLONG)(ULONG_PTR)elfsysv_tls_raw,
	(ULONGLONG)(ULONG_PTR)(elfsysv_tls_raw + sizeof elfsysv_tls_raw),
	(ULONGLONG)(ULONG_PTR)&elfsysv_tls_index,
	(ULONGLONG)(ULONG_PTR)elfsysv_tls_callbacks,
	0,
	0
};
