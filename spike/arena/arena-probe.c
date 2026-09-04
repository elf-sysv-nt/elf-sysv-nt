/* Can the kernel own the whole user address range as one placeholder and
 * replace pieces of it with section views?
 *
 * Proposal 0011 section 4.4 builds the N substrate's address space on that
 * one sentence, and its "Not verified" section admits the sentence is a
 * reading of documentation: "that placeholders accept a section view at 64 KB
 * inside a reservation of tens of terabytes". This is the measurement. Every
 * answer goes to stdout as key=value; the script beside it turns those into a
 * transcript.
 *
 * The calls are taken through ntdll rather than through kernel32 so that a
 * refusal comes back as an NTSTATUS, which is the thing worth recording. They
 * are resolved by name at start-up, so a host missing NtAllocateVirtualMemoryEx
 * reports that instead of failing to link.
 */
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include "arena-probe.h"

#ifndef MEM_RESERVE_PLACEHOLDER
#define MEM_RESERVE_PLACEHOLDER  0x00040000
#endif
#ifndef MEM_REPLACE_PLACEHOLDER
#define MEM_REPLACE_PLACEHOLDER  0x00004000
#endif
#ifndef MEM_PRESERVE_PLACEHOLDER
#define MEM_PRESERVE_PLACEHOLDER 0x00000002
#endif

#define RELEASE "arena-probe 1.0"

#define GRANULE 0x10000u
#define PAGE    0x1000u

typedef LONG (NTAPI *nt_alloc_ex_fn)(HANDLE, PVOID *, PSIZE_T, ULONG, ULONG, void *, ULONG);
typedef LONG (NTAPI *nt_free_fn)(HANDLE, PVOID *, PSIZE_T, ULONG);
typedef LONG (NTAPI *nt_create_section_fn)(PHANDLE, ULONG, void *, PLARGE_INTEGER, ULONG, ULONG, HANDLE);
typedef LONG (NTAPI *nt_map_ex_fn)(HANDLE, HANDLE, PVOID *, PLARGE_INTEGER, PSIZE_T, ULONG, ULONG, void *, ULONG);
typedef LONG (NTAPI *nt_unmap_ex_fn)(HANDLE, PVOID, ULONG);

static nt_alloc_ex_fn       nt_alloc_ex;
static nt_free_fn           nt_free;
static nt_create_section_fn nt_create_section;
static nt_map_ex_fn         nt_map_ex;
static nt_unmap_ex_fn       nt_unmap_ex;

static int verbose;

static void say(const char *fmt, ...)
{
	if (!verbose)
		return;
	va_list ap;
	va_start(ap, fmt);
	fputs("arena-probe: ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

/* An NTSTATUS is a number, and the runner reduces every number to N before it
 * compares two transcripts. So the status also gets a name, and the name is
 * what a rerun is actually held to: a refusal that changes its reason changes
 * a word, not a hex digit. Anything unnamed reports as unknown and is worth
 * looking up before the transcript is trusted.
 */
static const char *status_name(long st)
{
	switch ((unsigned long) st) {
	case 0x00000000UL: return "STATUS_SUCCESS";
	case 0x40000003UL: return "STATUS_IMAGE_NOT_AT_BASE";
	case 0xC0000005UL: return "STATUS_ACCESS_VIOLATION";
	case 0xC000000DUL: return "STATUS_INVALID_PARAMETER";
	case 0xC0000017UL: return "STATUS_NO_MEMORY";
	case 0xC0000018UL: return "STATUS_CONFLICTING_ADDRESSES";
	case 0xC000001FUL: return "STATUS_INVALID_VIEW_SIZE";
	case 0xC0000022UL: return "STATUS_ACCESS_DENIED";
	case 0xC000009AUL: return "STATUS_INSUFFICIENT_RESOURCES";
	case 0xC00000A0UL: return "STATUS_MAPPED_FILE_SIZE_ZERO";
	case 0xC00000BBUL: return "STATUS_NOT_SUPPORTED";
	case 0xC0000141UL: return "STATUS_INVALID_ADDRESS";
	case 0xC0000220UL: return "STATUS_MAPPED_ALIGNMENT";
	case 0xC0000225UL: return "STATUS_NOT_FOUND";
	case 0xC0000466UL: return "STATUS_FILE_NOT_AVAILABLE";
	default:           return "unknown";
	}
}

static void report_status(const char *key, long st)
{
	printf("%s=0x%08lX %s\n", key, (unsigned long) st, status_name(st));
}

static void yesno(const char *key, int v)
{
	printf("%s=%s\n", key, v ? "yes" : "no");
}

static long reserve_placeholder(void **base, uint64_t size)
{
	PVOID  b = NULL;
	SIZE_T n = (SIZE_T) size;
	long st = nt_alloc_ex(GetCurrentProcess(), &b, &n,
	                      MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0);
	*base = (st >= 0) ? b : NULL;
	return st;
}

static long release_whole(void *base)
{
	PVOID  b = base;
	SIZE_T n = 0;
	return nt_free(GetCurrentProcess(), &b, &n, MEM_RELEASE);
}

/* Carve `size` bytes out of a standing placeholder at `at`, leaving what is on
 * either side of it alone. This is the operation the arena rests on: NT frees a
 * reservation whole, and MEM_PRESERVE_PLACEHOLDER is the exception that lets a
 * piece come out of the middle without the rest going with it. */
static long split_off(void *at, uint64_t size)
{
	PVOID  b = at;
	SIZE_T n = (SIZE_T) size;
	return nt_free(GetCurrentProcess(), &b, &n, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
}

static long map_over(HANDLE section, void *at, uint64_t off, uint64_t size, ULONG prot)
{
	PVOID         b = at;
	SIZE_T        n = (SIZE_T) size;
	LARGE_INTEGER o;
	o.QuadPart = (LONGLONG) off;
	return nt_map_ex(section, GetCurrentProcess(), &b, &o, &n,
	                 MEM_REPLACE_PLACEHOLDER, prot, NULL, 0);
}

/* The region's state as a word. Deliberately not the MEM_ constant and
 * deliberately not the address: the runner drops a line carrying either,
 * because a live address-space survey moves every run. What does not move is
 * whether the neighbour of a replaced piece is still a placeholder. */
static const char *region_word(void *at)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(at, &mbi, sizeof mbi))
		return "unqueryable";

	const char *state = "unknown-state";
	if (mbi.State == 0x10000)      state = "free";
	else if (mbi.State == 0x2000)  state = "reserved";
	else if (mbi.State == 0x1000)  state = "committed";

	if (mbi.State == 0x10000)
		return "free";

	static char word[48];
	const char *type = "unknown-type";
	if (mbi.Type == 0x20000)         type = "private";
	else if (mbi.Type == 0x40000)    type = "mapped";
	else if (mbi.Type == 0x1000000)  type = "image";
	snprintf(word, sizeof word, "%s-%s", state, type);
	return word;
}

/* How wide the region at `at` actually is, as a word. A split that returns
 * success has not necessarily done what was asked: NT is free to round the
 * request up to its allocation granularity and report no error, and a probe
 * that read the status alone would record a 4 KB split that never happened. */
static const char *region_size_word(void *at)
{
	MEMORY_BASIC_INFORMATION mbi;
	if (!VirtualQuery(at, &mbi, sizeof mbi))
		return "unqueryable";
	if (mbi.RegionSize == PAGE)
		return "page";
	if (mbi.RegionSize == GRANULE)
		return "granule";
	return "wider";
}

/* q1. Walk the spans upward, each one reserved and released on its own, so a
 * refusal at 64 TB says something about 64 TB rather than about the 8 TB still
 * held. The largest that sticks is the measurement; whether anything past a
 * terabyte sticks at all is the finding. */
static const uint64_t spans[] = {
	1ULL << 30,          /* 1 GB */
	64ULL << 30,         /* 64 GB */
	1ULL << 40,          /* 1 TB */
	8ULL << 40,          /* 8 TB */
	64ULL << 40,         /* 64 TB */
	127ULL << 40,        /* 127 TB, all but a sliver of the user range NT hands
	                      * out on x64. Section 4.4 asks for the whole of it,
	                      * not for a large piece, so the ladder ends where the
	                      * proposal's claim does. */
};
static const char *span_names[] = { "1g", "64g", "1t", "8t", "64t", "127t" };
#define NSPANS (sizeof spans / sizeof spans[0])

#define SECTION_RIGHTS 0x000F001FUL
#define SEC_COMMIT_    0x08000000UL

static long make_pagefile_section(HANDLE *h, uint64_t size, ULONG prot)
{
	LARGE_INTEGER max;
	max.QuadPart = (LONGLONG) size;
	*h = NULL;
	return nt_create_section(h, SECTION_RIGHTS, NULL, &max, prot, SEC_COMMIT_, NULL);
}

static long make_file_section(HANDLE *h, HANDLE file, ULONG prot)
{
	*h = NULL;
	return nt_create_section(h, SECTION_RIGHTS, NULL, NULL, prot, SEC_COMMIT_, file);
}

/* A scratch file with a recognisable pattern in it, deleted when its handle
 * closes. The path never reaches the transcript: it is a fresh temp name every
 * run and would be noise in a document that is supposed to reproduce. */
static HANDLE make_scratch_file(uint64_t size, unsigned char seed)
{
	wchar_t dir[MAX_PATH], path[MAX_PATH];
	if (!GetTempPathW(MAX_PATH, dir))
		return INVALID_HANDLE_VALUE;
	if (!GetTempFileNameW(dir, L"arn", 0, path))
		return INVALID_HANDLE_VALUE;

	HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE,
	                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	                       NULL, CREATE_ALWAYS,
	                       FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (f == INVALID_HANDLE_VALUE)
		return f;

	unsigned char *buf = malloc((size_t) size);
	if (!buf) {
		CloseHandle(f);
		return INVALID_HANDLE_VALUE;
	}
	for (uint64_t i = 0; i < size; i++)
		buf[i] = (unsigned char) ((i * 31u + seed) & 0xff);
	DWORD wrote = 0;
	BOOL ok = WriteFile(f, buf, (DWORD) size, &wrote, NULL);
	free(buf);
	if (!ok || wrote != size) {
		CloseHandle(f);
		return INVALID_HANDLE_VALUE;
	}
	FlushFileBuffers(f);
	return f;
}

static int file_bytes_match(const unsigned char *p, uint64_t n, unsigned char seed)
{
	for (uint64_t i = 0; i < n; i++)
		if (p[i] != (unsigned char) ((i * 31u + seed) & 0xff))
			return 0;
	return 1;
}

int main(int argc, char **argv)
{
	unsigned long pages = 2048;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--verbose") || !strcmp(argv[i], "-v")) { verbose = 1; continue; }
		if (!strcmp(argv[i], "--pages") && i + 1 < argc) { pages = strtoul(argv[++i], NULL, 10); continue; }
		if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
			puts("usage: arena-probe [-v|--verbose] [--pages N] [--version]");
			return 0;
		}
		fprintf(stderr, "arena-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	if (pages < 1000)
		pages = 1000;

	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	nt_alloc_ex       = (nt_alloc_ex_fn)       (void *) GetProcAddress(nt, "NtAllocateVirtualMemoryEx");
	nt_free           = (nt_free_fn)           (void *) GetProcAddress(nt, "NtFreeVirtualMemory");
	nt_create_section = (nt_create_section_fn) (void *) GetProcAddress(nt, "NtCreateSection");
	nt_map_ex         = (nt_map_ex_fn)         (void *) GetProcAddress(nt, "NtMapViewOfSectionEx");
	nt_unmap_ex       = (nt_unmap_ex_fn)       (void *) GetProcAddress(nt, "NtUnmapViewOfSectionEx");

	printf("probe=%s\n", RELEASE);
	yesno("nt_allocate_ex_present", nt_alloc_ex != NULL);
	yesno("nt_map_view_ex_present", nt_map_ex != NULL);
	if (!nt_alloc_ex || !nt_free || !nt_create_section || !nt_map_ex) {
		printf("q1_placeholder_reserved=unavailable\n");
		return 3;
	}

	/* ---- q1: how much of the user range will NT hold as one placeholder ---- */

	int      largest = -1;
	int      refused = -1;
	long     refusal_status = 0;
	for (unsigned i = 0; i < NSPANS; i++) {
		void *b = NULL;
		long st = reserve_placeholder(&b, spans[i]);
		printf("q1_span_%s=%s\n", span_names[i], st >= 0 ? "accepted" : "refused");
		say("span %s: %s", span_names[i], status_name(st));
		if (st >= 0) {
			largest = (int) i;
			release_whole(b);
		} else if (refused < 0) {
			refused = (int) i;
			refusal_status = st;
		}
	}
	yesno("q1_placeholder_reserved", largest >= 0);
	printf("q1_largest_span=%s\n", largest >= 0 ? span_names[largest] : "none");
	printf("q1_largest_span_bytes=%llu\n",
	       largest >= 0 ? (unsigned long long) spans[largest] : 0ULL);
	printf("q1_multi_terabyte=%s\n", (largest >= 3) ? "accepted" : "refused");
	printf("q1_first_refusal_span=%s\n", refused >= 0 ? span_names[refused] : "none");
	report_status("q1_first_refusal_status", refused >= 0 ? refusal_status : 0);

	if (largest < 0) {
		printf("q2_split_placeholder=unreached\n");
		return 3;
	}

	/* The arena the rest of the questions are asked inside. Largest first, and
	 * down the list if the span that was accepted a moment ago is not there any
	 * more -- another process moves too, and the questions below do not care how
	 * big the arena is, only that it is one. */
	void    *arena = NULL;
	uint64_t arena_size = 0;
	for (int i = largest; i >= 0 && !arena; i--) {
		if (reserve_placeholder(&arena, spans[i]) >= 0)
			arena_size = spans[i];
	}
	if (!arena) {
		printf("q2_split_placeholder=unreached\n");
		return 3;
	}
	printf("q_arena_bytes=%llu\n", (unsigned long long) arena_size);
	say("arena of %llu bytes", (unsigned long long) arena_size);

	uintptr_t A = (uintptr_t) arena;
	void *off_a = (void *) (A + (64u << 20));            /* q2, q3 */
	void *off_b = (void *) (A + (128u << 20));           /* q4 */
	void *off_c = (void *) (A + (192u << 20));           /* q5, data */
	void *off_d = (void *) (A + (256u << 20));           /* q5, execute */
	void *off_e = (void *) (A + (64u << 20) + GRANULE);  /* q8, the neighbour */
	void *below = (void *) (A + (64u << 20) - GRANULE);
	void *above = (void *) (A + (64u << 20) + 2 * GRANULE);

	/* ---- q2: take a 64 KB piece out of the middle, leave the rest ---- */

	long st = split_off(off_a, GRANULE);
	yesno("q2_split_placeholder", st >= 0);
	report_status("q2_split_status", st);
	printf("q2_piece_state=%s\n", region_word(off_a));
	printf("q2_piece_width=%s\n", st >= 0 ? region_size_word(off_a) : "none");

	/* ---- q3: replace that piece with a pagefile-backed view ---- */

	int q3_ok = 0, q3_rw = 0;
	HANDLE sec = NULL;
	if (st >= 0) {
		long cs = make_pagefile_section(&sec, GRANULE, PAGE_READWRITE);
		report_status("q3_section_status", cs);
		if (cs >= 0) {
			long ms = map_over(sec, off_a, 0, GRANULE, PAGE_READWRITE);
			report_status("q3_map_status", ms);
			q3_ok = (ms >= 0);
			if (q3_ok) {
				volatile uint64_t *p = (volatile uint64_t *) off_a;
				p[0] = 0x4152454e41303031ULL;
				p[(GRANULE / 8) - 1] = 0x4c415354574f5244ULL;
				q3_rw = (p[0] == 0x4152454e41303031ULL &&
				         p[(GRANULE / 8) - 1] == 0x4c415354574f5244ULL);
			}
		}
	}
	yesno("q3_view_replaced_64k", q3_ok);
	yesno("q3_readback_64k", q3_rw);
	printf("q3_region_state=%s\n", region_word(off_a));

	/* ---- q4: the same at 4 KB, which is where the congruence rule comes from ----
	 * Three separate refusals are possible and they are not the same refusal, so
	 * each is asked on its own: a 4 KB-sized split, a 4 KB-aligned split, and a
	 * 4 KB view laid over a placeholder that is a full granule wide. */

	long s4_size = split_off(off_b, PAGE);
	printf("q4_split_4k_size=%s\n",
	       s4_size < 0 ? "refused" : region_size_word(off_b));
	report_status("q4_split_4k_size_status", s4_size);

	void *at_align = (void *) ((uintptr_t) off_b + (16u << 20) + PAGE);
	long s4_align = split_off(at_align, PAGE);
	printf("q4_split_4k_align=%s\n",
	       s4_align < 0 ? "refused" : region_size_word(at_align));
	report_status("q4_split_4k_align_status", s4_align);

	/* The view itself, laid over whatever the size split actually produced. If
	 * the split rounded to a granule, this asks whether a 4 KB view can replace
	 * part of one; if it really did carve a page out, it asks the question the
	 * congruence rule turns on directly. */
	int q4_view = 0, q4_rw = 0;
	long v4_st = 0;
	{
		HANDLE s4 = NULL;
		if (s4_size >= 0 && make_pagefile_section(&s4, GRANULE, PAGE_READWRITE) >= 0) {
			v4_st = map_over(s4, off_b, 0, PAGE, PAGE_READWRITE);
			q4_view = (v4_st >= 0);
			if (q4_view) {
				volatile uint64_t *p = (volatile uint64_t *) off_b;
				p[0] = 0x504147453441524eULL;
				p[(PAGE / 8) - 1] = 0x454e4450414745ULL;
				q4_rw = (p[0] == 0x504147453441524eULL &&
				         p[(PAGE / 8) - 1] == 0x454e4450414745ULL);
			}
		}
		if (s4)
			CloseHandle(s4);
	}
	yesno("q4_view_replaced_4k", q4_view);
	report_status("q4_view_replaced_4k_status", v4_st);
	yesno("q4_readback_4k", q4_rw);
	printf("q4_view_width=%s\n", q4_view ? region_size_word(off_b) : "none");
	/* The granule either side of a page-wide view is the thing that decides
	 * whether this is usable: a 4 KB replacement inside a granule is only
	 * interesting if what is left of that granule is still a placeholder. */
	printf("q4_rest_of_granule=%s\n", region_word((void *) ((uintptr_t) off_b + PAGE)));
	printf("q4_granularity=%s\n", q4_view ? "4k" : "64k");

	/* ---- q5: a view of a real file, which is where ELF segments come from ---- */

	int q5_ok = 0, q5_match = 0, q5_exec = 0, q5_4k = 0, q5_4k_match = 0;
	int q5_off = 0, q5_off_match = 0;
	long q5_st = 0, q5_exec_st = 0, q5_4k_st = 0, q5_off_st = 0;
	HANDLE scratch = make_scratch_file(256u * 1024u, 0x5a);
	yesno("q5_scratch_file", scratch != INVALID_HANDLE_VALUE);
	if (scratch != INVALID_HANDLE_VALUE) {
		HANDLE fs = NULL;
		long cs = make_file_section(&fs, scratch, PAGE_READWRITE);
		report_status("q5_section_status", cs);
		if (cs >= 0 && split_off(off_c, GRANULE) >= 0) {
			q5_st = map_over(fs, off_c, 0, GRANULE, PAGE_READWRITE);
			q5_ok = (q5_st >= 0);
			if (q5_ok)
				q5_match = file_bytes_match((const unsigned char *) off_c, GRANULE, 0x5a);
		}
		/* Same file, a section whose maximum protection carries execute, which
		 * is the shape section 4.4 wants for a PT_LOAD that is executable. */
		HANDLE xs = NULL;
		long xcs = make_file_section(&xs, scratch, PAGE_EXECUTE_READ);
		report_status("q5_exec_section_status", xcs);
		if (xcs >= 0 && split_off(off_d, GRANULE) >= 0) {
			q5_exec_st = map_over(xs, off_d, 0, GRANULE, PAGE_EXECUTE_READ);
			q5_exec = (q5_exec_st >= 0);
		}
		/* A 4 KB file view, asked exactly as q4 asked it: split a page, replace
		 * that page. And then the harder one -- the same view at a section
		 * offset that is 4 KB aligned and nothing more, which is the shape a
		 * PT_LOAD has when its p_offset is not congruent to 64 KB. That second
		 * answer is what decides whether ELF segments have to be congruent. */
		if (cs >= 0) {
			void *at = (void *) ((uintptr_t) off_d + (16u << 20));
			if (split_off(at, PAGE) >= 0) {
				q5_4k_st = map_over(fs, at, 0, PAGE, PAGE_READWRITE);
				q5_4k = (q5_4k_st >= 0);
				if (q5_4k)
					q5_4k_match = (*(const unsigned char *) at == 0x5a);
			}
			void *at2 = (void *) ((uintptr_t) off_d + (32u << 20));
			if (split_off(at2, PAGE) >= 0) {
				q5_off_st = map_over(fs, at2, PAGE, PAGE, PAGE_READWRITE);
				q5_off = (q5_off_st >= 0);
				if (q5_off)
					q5_off_match = (*(const unsigned char *) at2 ==
					                (unsigned char) ((PAGE * 31u + 0x5a) & 0xff));
			}
		}
	}
	yesno("q5_file_backed_view", q5_ok);
	report_status("q5_map_status", q5_st);
	yesno("q5_file_contents_match", q5_match);
	yesno("q5_file_backed_exec_view", q5_exec);
	report_status("q5_exec_map_status", q5_exec_st);
	yesno("q5_file_backed_4k", q5_4k);
	report_status("q5_4k_map_status", q5_4k_st);
	yesno("q5_file_4k_contents_match", q5_4k_match);
	yesno("q5_file_backed_4k_offset", q5_off);
	report_status("q5_4k_offset_map_status", q5_off_st);
	yesno("q5_file_4k_offset_contents_match", q5_off_match);
	printf("q5_granularity=%s\n", q5_4k ? "4k" : (q5_ok ? "64k" : "none"));
	printf("q5_offset_granularity=%s\n", q5_off ? "4k" : "64k");

	/* ---- q8: is the placeholder around a replaced piece still an arena? ---- */

	int q8_resplit = 0, q8_second_view = 0, q8_intact = 0;
	long re_st = split_off(off_e, GRANULE);
	q8_resplit = (re_st >= 0);
	report_status("q8_resplit_status", re_st);
	if (q8_resplit) {
		HANDLE s2 = NULL;
		if (make_pagefile_section(&s2, GRANULE, PAGE_READWRITE) >= 0) {
			long ms = map_over(s2, off_e, 0, GRANULE, PAGE_READWRITE);
			q8_second_view = (ms >= 0);
			report_status("q8_second_map_status", ms);
			if (q8_second_view)
				*(volatile uint64_t *) off_e = 0x4e4549474842554fULL;
		}
	}
	if (q3_ok) {
		volatile uint64_t *p = (volatile uint64_t *) off_a;
		q8_intact = (p[0] == 0x4152454e41303031ULL &&
		             p[(GRANULE / 8) - 1] == 0x4c415354574f5244ULL);
	}
	yesno("q8_resplit_after_replace", q8_resplit);
	yesno("q8_second_view_placed", q8_second_view);
	yesno("q8_first_view_undisturbed", q8_intact);
	printf("q8_neighbour_below=%s\n", region_word(below));
	printf("q8_neighbour_above=%s\n", region_word(above));
	printf("q8_replaced_piece=%s\n", region_word(off_a));
	yesno("q8_placeholder_survives_neighbours",
	      q8_resplit && q8_second_view && q8_intact);

	/* ---- q6 and q7: lazy commit through a vectored handler ---- */

	struct fault_result f;
	measure_lazy_commit(&f, pages, verbose);
	yesno("q6_reservation_made", f.reserved);
	yesno("q6_lazy_commit_veh", f.veh_worked && f.readback_ok);
	yesno("q6_readback_after_continue", f.readback_ok);
	report_status("q6_reserve_status", (long) f.status);
	printf("q7_pages=%lu\n", f.pages);
	printf("q7_fault_ns=%.0f\n", f.fault_ns);
	printf("q7_fault_ns_p90=%.0f\n", f.fault_ns_p90);
	printf("q7_control_ns=%.0f\n", f.control_ns);
	printf("q7_control_ns_p90=%.0f\n", f.control_ns_p90);

	return 0;
}
