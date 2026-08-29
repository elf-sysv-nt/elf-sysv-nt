/*
 * Spike 2: can a PE stub map a static ELF image and transfer control to it?
 *
 * This is the stub. It reads an ET_EXEC file, reserves the address range the
 * program headers ask for, commits and protects one region per PT_LOAD,
 * builds the stack the psABI describes, and jumps to e_entry. Then it reads
 * back what the image wrote and says whether each part of that worked.
 *
 * It is not a loader and does not pretend to be one. There is no dynamic
 * linking here, by the terms of the question. There is also no bounds
 * checking worth the name: this parses a file it generated itself, and
 * parsing attacker-shaped input is WP-31, which gets fuzzed. Do not lift this
 * file into the tree; lift the findings.
 *
 * Usage:
 *   map-and-jump-stub [options] ELF
 *
 * Options:
 *   -s N, --stack=N       Bytes for the image's stack. [default: 0x40000]
 *   -x, --no-fault-probe  Trust VirtualQuery instead of touching the pages.
 *   -e, --expect-refusal  Pass when the reservation is refused, and only then.
 *   -t, --terse           The key=value block alone.
 *   -q, --quiet           Errors only.
 *   -v, --verbose         Report every page range as it is committed.
 *   -d, --debug           Trace execution; implies --verbose.
 *   -V, --version         Print the version and exit.
 *   -h, --help            Print this message and exit.
 *
 * Each option is also settable as MAP_AND_JUMP_STUB_<OPTION>, and the option
 * wins over the variable.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char PROG[] = "map-and-jump-stub";
static const char RELEASE[] = "map-and-jump-stub 1.0";

/* Cygwin ships no elf.h, and pulling one in would be a dependency on a
   layout this project intends to own anyway. Only what is read is declared. */
typedef struct {
	unsigned char e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

#define ET_EXEC		2
#define EM_X86_64	62
#define PT_LOAD		1
#define PT_GNU_STACK	0x6474e551
#define PF_X		1
#define PF_W		2
#define PF_R		4

#define AT_NULL			0
#define AT_PAGESZ		6
#define AT_SPIKE_HANDSHAKE	0x7000
#define AT_SPIKE_RODATA		0x7001
#define AT_SPIKE_BSS		0x7002

/* Mirrors the block make-elf.py puts at the head of the writable segment. */
struct handshake {
	uint64_t in_magic, host_rsp;
	uint64_t out_magic, out_argc, out_rodata, out_bss, out_rip, out_pagesz;
	uint64_t out_argv0;
};

#define IN_MAGIC	UINT64_C(0x0123456789ABCDEF)
#define MAGIC_KEY	UINT64_C(0x9E3779B97F4A7C15)
#define RODATA_WORD	UINT64_C(0xC0FFEE0FBADC0DE5)
#define ARGV0		"map-and-jump"

extern void elf_enter(void *entry, void *elf_rsp, void *handshake);
extern unsigned abi_probe(void *entry, void *elf_rsp, void *handshake,
			  unsigned *xmm_mask);
extern int probe_store(void *addr);
extern int probe_exec(void *addr);
extern char probe_store_end[], probe_store_fault[];
extern char probe_exec_end[], probe_exec_fault[];

static struct {
	unsigned long long stack;
	int fault_probe, expect_refusal, terse, quiet, verbose, debug;
	const char *path;
} opt = { 0x40000, 1, 0, 0, 0, 0, 0, NULL };

/* Results, collected as they are measured and printed once at the end so
   that the key=value block is the same set of names whatever happened. */
static struct {
	const char *reason;
	uint64_t entry, span_first, span_last, reserved_at, granule_waste, free_run;
	unsigned phnum, loads, shared_pages;
	int reserved, returned, store_faulted, exec_faulted, probed;
	unsigned gpr_mask, xmm_mask;
	struct handshake seen;
	uint64_t text_first, text_last;
} got;

static void note(const char *fmt, ...)
{
	va_list ap;
	if (opt.quiet)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "%s: ", PROG);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static int refuse(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "%s: ", PROG);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	return 2;
}

static void say(const char *fmt, ...)
{
	va_list ap;
	if (opt.terse || opt.quiet)
		return;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static const char *last_error(void)
{
	static char text[256];
	DWORD code = GetLastError();
	char *end;
	snprintf(text, sizeof text, "error %lu: ", (unsigned long) code);
	end = text + strlen(text);
	if (!FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
			    FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code, 0,
			    end, (DWORD)(sizeof text - (size_t)(end - text)), NULL))
		snprintf(end, sizeof text - (size_t)(end - text), "(no text)");
	end = text + strlen(text);
	while (end > text && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == '.'))
		*--end = '\0';
	return text;
}

/* Armed only around a probe. A vectored handler that swallowed every access
   violation would turn a real defect in this stub into a clean transcript,
   which is the one failure mode a spike cannot afford. */
static volatile LONG armed;	/* 0 none, 1 the store probe, 2 the execute probe */

static LONG CALLBACK probe_handler(EXCEPTION_POINTERS *ep)
{
	CONTEXT *cx = ep->ContextRecord;
	UINT_PTR rip = (UINT_PTR) cx->Rip;

	if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;

	if (armed == 1 && rip >= (UINT_PTR)(void *) probe_store &&
	    rip < (UINT_PTR) probe_store_end) {
		cx->Rip = (DWORD64)(UINT_PTR) probe_store_fault;
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	/* The execute probe faults at its target, so %rip is outside the probe
	   rather than inside it and the armed flag is the only witness. */
	if (armed == 2) {
		cx->Rip = (DWORD64)(UINT_PTR) probe_exec_fault;
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

static const char *protection_name(DWORD protect)
{
	switch (protect & 0xff) {
	case PAGE_NOACCESS:		return "NOACCESS";
	case PAGE_READONLY:		return "READONLY";
	case PAGE_READWRITE:		return "READWRITE";
	case PAGE_WRITECOPY:		return "WRITECOPY";
	case PAGE_EXECUTE:		return "EXECUTE";
	case PAGE_EXECUTE_READ:		return "EXECUTE_READ";
	case PAGE_EXECUTE_READWRITE:	return "EXECUTE_READWRITE";
	case PAGE_EXECUTE_WRITECOPY:	return "EXECUTE_WRITECOPY";
	case 0:				return "(none)";
	default:			return "(other)";
	}
}

static DWORD protection_for(uint32_t flags)
{
	switch (flags & (PF_R | PF_W | PF_X)) {
	case PF_R:			return PAGE_READONLY;
	case PF_R | PF_W:		return PAGE_READWRITE;
	case PF_R | PF_X:		return PAGE_EXECUTE_READ;
	case PF_R | PF_W | PF_X:	return PAGE_EXECUTE_READWRITE;
	case PF_X:			return PAGE_EXECUTE;
	default:			return PAGE_NOACCESS;
	}
}

static const char *state_name(DWORD state)
{
	return state == MEM_FREE ? "MEM_FREE" :
	       state == MEM_RESERVE ? "MEM_RESERVE" : "MEM_COMMIT";
}

/* Shown whenever a reservation is refused. A refusal on its own says only
   that the address was unavailable; what a loader has to know is what was
   standing there, because the answer decides whether the collision is with
   something movable, something Cygwin owns, or something Windows placed.
 *
 * Taken before the attempt and held, not taken after it. The first version of
 * this walked the span once the failure was in hand and reported a 1.3 MB
 * mapping at the very address that had been free a moment earlier: printing
 * the diagnostic had allocated into the hole the diagnostic was describing.
 * A measurement that the act of measuring perturbs is worse than none. */
static char span_map[16384];

static void survey_span(uint64_t from, uint64_t to)
{
	MEMORY_BASIC_INFORMATION m;
	uint64_t at = from;
	size_t used = 0;

	used += (size_t) snprintf(span_map + used, sizeof span_map - used,
		"      base                size         state        type      module\n");
	while (at < to && used + 256 < sizeof span_map &&
	       VirtualQuery((void *)(UINT_PTR) at, &m, sizeof m)) {
		char name[MAX_PATH];
		const char *kind = m.State == MEM_FREE ? "" :
			m.Type == MEM_IMAGE ? "IMAGE" :
			m.Type == MEM_MAPPED ? "MAPPED" : "PRIVATE";
		name[0] = '\0';
		if (m.State != MEM_FREE && m.Type == MEM_IMAGE)
			GetModuleFileNameA((HMODULE) m.AllocationBase, name, sizeof name);
		used += (size_t) snprintf(span_map + used, sizeof span_map - used,
			"      0x%016" PRIx64 "  0x%09" PRIx64 "  %-12s %-9s %s\n",
			(uint64_t)(UINT_PTR) m.BaseAddress, (uint64_t) m.RegionSize,
			state_name(m.State), kind, name);
		at = (uint64_t)(UINT_PTR) m.BaseAddress + m.RegionSize;
	}
}

static const char *flag_text(uint32_t flags)
{
	static char text[4];
	text[0] = (flags & PF_R) ? 'r' : '-';
	text[1] = (flags & PF_W) ? 'w' : '-';
	text[2] = (flags & PF_X) ? 'x' : '-';
	text[3] = '\0';
	return text;
}

static void usage(FILE *to)
{
	fprintf(to,
"Usage:\n"
"  map-and-jump-stub [options] ELF\n"
"\n"
"Options:\n"
"  -s N, --stack=N       Bytes for the image's stack. [default: 0x40000]\n"
"  -w, --where           Print this stub's own image base and exit.\n"
"  -x, --no-fault-probe  Trust VirtualQuery instead of touching the pages.\n"
"  -e, --expect-refusal  Pass when the reservation is refused, and only then.\n"
"  -t, --terse           The key=value block alone.\n"
"  -q, --quiet           Errors only.\n"
"  -v, --verbose         Report every page range as it is committed.\n"
"  -d, --debug           Trace execution; implies --verbose.\n"
"  -V, --version         Print the version and exit.\n"
"  -h, --help            Print this message and exit.\n"
"\n"
"Each option is also settable as MAP_AND_JUMP_STUB_<OPTION>.\n");
}

static const char *from_env(const char *name)
{
	static char key[64];
	snprintf(key, sizeof key, "MAP_AND_JUMP_STUB_%s", name);
	return getenv(key);
}

static int flag_from_env(const char *name)
{
	const char *value = from_env(name);
	return value && !strcmp(value, "1");
}

/* Returns 0 to carry on, or an exit status. */
static int parse(int argc, char **argv, int *status)
{
	const char *value;
	int i;

	if ((value = from_env("STACK")) != NULL)
		opt.stack = strtoull(value, NULL, 0);
	if (flag_from_env("NO_FAULT_PROBE"))
		opt.fault_probe = 0;
	opt.expect_refusal = flag_from_env("EXPECT_REFUSAL");
	opt.terse = flag_from_env("TERSE");
	opt.quiet = flag_from_env("QUIET");
	opt.verbose = flag_from_env("VERBOSE");
	opt.debug = flag_from_env("DEBUG");

	for (i = 1; i < argc; i++) {
		char *arg = argv[i];
		if (!strcmp(arg, "--")) { i++; break; }
		if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
			usage(stdout);
			*status = 0;
			return 1;
		}
		if (!strcmp(arg, "-V") || !strcmp(arg, "--version")) {
			printf("%s\n", RELEASE);
			*status = 0;
			return 1;
		}
		if (!strcmp(arg, "-w") || !strcmp(arg, "--where")) {
			printf("case_module_base=0x%" PRIx64 "\n",
			       (uint64_t)(UINT_PTR) GetModuleHandle(NULL));
			*status = 0;
			return 1;
		}
		if (!strcmp(arg, "-s") || !strcmp(arg, "--stack")) {
			if (++i >= argc) { *status = refuse("%s wants a value", arg); return 1; }
			opt.stack = strtoull(argv[i], NULL, 0);
		} else if (!strncmp(arg, "--stack=", 8)) {
			opt.stack = strtoull(arg + 8, NULL, 0);
		} else if (!strcmp(arg, "-x") || !strcmp(arg, "--no-fault-probe")) {
			opt.fault_probe = 0;
		} else if (!strcmp(arg, "-e") || !strcmp(arg, "--expect-refusal")) {
			opt.expect_refusal = 1;
		} else if (!strcmp(arg, "-t") || !strcmp(arg, "--terse")) {
			opt.terse = 1;
		} else if (!strcmp(arg, "-q") || !strcmp(arg, "--quiet")) {
			opt.quiet = 1;
		} else if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
			opt.verbose++;
		} else if (!strcmp(arg, "-d") || !strcmp(arg, "--debug")) {
			opt.debug = 1;
			opt.verbose++;
		} else if (arg[0] == '-' && arg[1]) {
			*status = refuse("unknown option %s", arg);
			return 1;
		} else {
			break;
		}
	}
	if (i >= argc) {
		*status = refuse("nothing to map. Give an ELF file.");
		return 1;
	}
	opt.path = argv[i++];
	if (i != argc) {
		*status = refuse("takes one argument, got %s too", argv[i]);
		return 1;
	}
	if (opt.stack < 0x10000) {
		*status = refuse("--stack wants at least 0x10000, got 0x%llx", opt.stack);
		return 1;
	}
	return 0;
}

#define PAGE_SIZE UINT64_C(0x1000)
#define page_down(a) ((a) & ~(PAGE_SIZE - 1))
#define page_up(a) (((a) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define MAX_LOADS 16

static unsigned char *slurp(const char *path, size_t *size)
{
	unsigned char *buffer;
	long length;
	FILE *file = fopen(path, "rb");

	if (!file) {
		refuse("cannot read %s", path);
		return NULL;
	}
	if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0) {
		refuse("cannot size %s", path);
		fclose(file);
		return NULL;
	}
	rewind(file);
	buffer = malloc((size_t) length);
	if (!buffer || fread(buffer, 1, (size_t) length, file) != (size_t) length) {
		refuse("cannot load %s", path);
		free(buffer);
		fclose(file);
		return NULL;
	}
	fclose(file);
	*size = (size_t) length;
	return buffer;
}

static int check(const char *what, int ok, const char *detail)
{
	say("    %-32s %-8s %s\n", what, ok ? "ok" : "FAILED", detail ? detail : "");
	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	size_t size = 0;
	unsigned char *image;
	Elf64_Ehdr *eh;
	Elf64_Phdr *phdr, *load[MAX_LOADS], *text = NULL, *ro = NULL, *rw = NULL;
	unsigned nload = 0, i, j;
	SYSTEM_INFO sysinfo;
	MEMORY_BASIC_INFORMATION mbi;
	uint64_t first = UINT64_MAX, last = 0, reserve_base, reserve_size;
	uint64_t granule, hs_addr, ro_addr, bss_addr;
	struct handshake *hs;
	unsigned char *stack = NULL, *strings;
	uint64_t *vec, argv0_word = 0;
	PVOID handler = NULL;
	DWORD before_state = MEM_FREE;
	uint64_t before_run = 0;
	char reserve_error[256] = "";
	int status = 0, failures = 0;

	if (parse(argc, argv, &status))
		return status;
	if (!(image = slurp(opt.path, &size)))
		return 1;

	eh = (Elf64_Ehdr *) image;
	if (size < sizeof *eh || memcmp(eh->e_ident, "\177ELF", 4))
		return refuse("%s is not an ELF file", opt.path);
	if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1)
		return refuse("%s is not little-endian ELF64", opt.path);
	if (eh->e_type != ET_EXEC)
		return refuse("%s is not ET_EXEC; this spike does not relocate", opt.path);
	if (eh->e_machine != EM_X86_64)
		return refuse("%s is not x86-64", opt.path);
	if (eh->e_phentsize != sizeof(Elf64_Phdr))
		return refuse("%s has a %u-byte program header", opt.path, eh->e_phentsize);

	phdr = (Elf64_Phdr *) (image + eh->e_phoff);
	for (i = 0; i < eh->e_phnum; i++) {
		if (phdr[i].p_type != PT_LOAD)
			continue;
		if (nload == MAX_LOADS)
			return refuse("%s carries more than %d PT_LOADs", opt.path, MAX_LOADS);
		load[nload++] = &phdr[i];
		if (phdr[i].p_vaddr < first)
			first = phdr[i].p_vaddr;
		if (phdr[i].p_vaddr + phdr[i].p_memsz > last)
			last = phdr[i].p_vaddr + phdr[i].p_memsz;
		if (phdr[i].p_flags & PF_X)
			text = &phdr[i];
		else if (phdr[i].p_flags & PF_W)
			rw = &phdr[i];
		else
			ro = &phdr[i];
	}
	if (!nload)
		return refuse("%s has no PT_LOAD", opt.path);
	if (!text || !ro || !rw)
		return refuse("%s wants one executable, one read-only and one "
			      "writable PT_LOAD; this is the specimen shape", opt.path);

	got.entry = eh->e_entry;
	got.phnum = eh->e_phnum;
	got.loads = nload;
	got.span_first = first;
	got.span_last = last;
	got.text_first = text->p_vaddr;
	got.text_last = text->p_vaddr + text->p_memsz;
	hs_addr = rw->p_vaddr;
	ro_addr = ro->p_vaddr;
	bss_addr = rw->p_vaddr + rw->p_filesz;

	GetSystemInfo(&sysinfo);
	granule = sysinfo.dwAllocationGranularity;
	reserve_base = first & ~(granule - 1);
	reserve_size = page_up(last) - reserve_base;
	got.reserved_at = reserve_base;
	got.granule_waste = first - reserve_base;

	/* Everything that touches the address space happens before the first
	   line of output. printf allocates, and Windows satisfies an
	   allocation with no requested base from the lowest free hole, which
	   in a process whose image sits high is exactly the hole the image
	   wants. Printing first would make the stub compete with itself. */
	if (VirtualQuery((void *)(UINT_PTR) reserve_base, &mbi, sizeof mbi)) {
		before_state = mbi.State;
		before_run = mbi.RegionSize;
		if (mbi.State == MEM_FREE)
			got.free_run = mbi.RegionSize;
	}
	survey_span(reserve_base, reserve_base + reserve_size);
	got.reserved = VirtualAlloc((void *)(UINT_PTR) reserve_base,
				    (SIZE_T) reserve_size, MEM_RESERVE,
				    PAGE_NOACCESS) != NULL;
	if (!got.reserved)
		snprintf(reserve_error, sizeof reserve_error, "%s", last_error());

	say("== the image\n\n");
	say("    file                             %s\n", opt.path);
	say("    entry                            0x%016" PRIx64 "\n", got.entry);
	say("    program headers                  %u, of which %u PT_LOAD\n",
	    got.phnum, got.loads);
	say("    span                             0x%016" PRIx64 " .. 0x%016" PRIx64
	    "   (%" PRIu64 " bytes)\n", first, last, last - first);
	say("    this stub is loaded at           0x%016" PRIx64 "\n",
	    (uint64_t)(UINT_PTR) GetModuleHandle(NULL));

	say("\n== the reservation\n\n");
	say("    allocation granularity           0x%" PRIx64 "\n", granule);
	say("    span starts at                   0x%016" PRIx64 "\n", first);
	say("    so the reservation starts at     0x%016" PRIx64 "\n", reserve_base);
	say("    which is rounding away           %" PRIu64 " bytes below the image\n",
	    got.granule_waste);
	say("    the image wants                  0x%" PRIx64 " bytes there\n", reserve_size);
	say("    state there beforehand           %s, 0x%" PRIx64 " bytes of it\n",
	    state_name(before_state), before_run);

	if (!got.reserved) {
		got.reason = "the reservation was refused";
		say("    reserved                         no -- %s\n", reserve_error);
		say("\n    what stood in that range, surveyed before the attempt\n\n%s",
		    span_map);
		if (opt.expect_refusal) {
			say("\n    Refusal is what this case asked for.\n");
			goto report;
		}
		failures++;
		goto report;
	}
	say("    reserved                         yes, 0x%" PRIx64 " bytes\n", reserve_size);
	if (opt.expect_refusal) {
		say("\n    This case asked for a refusal and did not get one.\n");
		failures++;
		goto report;
	}

	/* Commit every segment writable, fill it, and only then set the
	   protections. Doing it in two passes rather than one is not tidiness:
	   two segments can share a page, and a segment protected read-only
	   before its neighbour is copied in makes the copy fault. */
	for (i = 0; i < nload; i++) {
		uint64_t from = page_down(load[i]->p_vaddr);
		uint64_t to = page_up(load[i]->p_vaddr + load[i]->p_memsz);
		for (j = 0; j < i; j++)
			if (from < page_up(load[j]->p_vaddr + load[j]->p_memsz) &&
			    page_down(load[j]->p_vaddr) < to)
				got.shared_pages++;
		if (!VirtualAlloc((void *)(UINT_PTR) from, (SIZE_T)(to - from),
				  MEM_COMMIT, PAGE_READWRITE)) {
			say("    commit of segment %u failed -- %s\n", i, last_error());
			got.reason = "a segment would not commit";
			failures++;
			goto report;
		}
		if (opt.verbose)
			say("    committed 0x%" PRIx64 " .. 0x%" PRIx64 " for segment %u\n",
			    from, to, i);
		memcpy((void *)(UINT_PTR) load[i]->p_vaddr,
		       image + load[i]->p_offset, (size_t) load[i]->p_filesz);
		/* Nothing zeroes .bss here. Windows hands back committed pages
		   already zeroed, and the transcript checks that rather than
		   assuming it. */
	}

	say("\n== the segments\n\n");
	say("     #  vaddr               filesz     memsz      elf  asked for          "
	    "reported back\n");
	for (i = 0; i < nload; i++) {
		uint64_t from = page_down(load[i]->p_vaddr);
		uint64_t to = page_up(load[i]->p_vaddr + load[i]->p_memsz);
		DWORD want = protection_for(load[i]->p_flags), old = 0;
		const char *reported = "(unqueryable)";
		if (!VirtualProtect((void *)(UINT_PTR) from, (SIZE_T)(to - from),
				    want, &old)) {
			say("    protection of segment %u failed -- %s\n", i, last_error());
			got.reason = "a segment would not take its protection";
			failures++;
			goto report;
		}
		if (VirtualQuery((void *)(UINT_PTR) load[i]->p_vaddr, &mbi, sizeof mbi))
			reported = protection_name(mbi.Protect);
		say("     %u  0x%016" PRIx64 "  0x%08" PRIx64 " 0x%08" PRIx64 " %s  %-18s %s\n",
		    i, load[i]->p_vaddr, load[i]->p_filesz, load[i]->p_memsz,
		    flag_text(load[i]->p_flags), protection_name(want), reported);
		if (strcmp(protection_name(want), reported))
			failures++;
	}
	say("\n    segments sharing a page          %u\n", got.shared_pages);

	if (opt.fault_probe) {
		unsigned char *planted = (unsigned char *)(UINT_PTR)(hs_addr + 0x70);
		handler = AddVectoredExceptionHandler(1, probe_handler);
		if (!handler) {
			got.reason = "no vectored exception handler";
			failures++;
			goto report;
		}
		got.probed = 1;

		armed = 1;
		got.store_faulted = probe_store((void *)(UINT_PTR) text->p_vaddr);
		armed = 0;

		*planted = 0xC3;		/* ret, in a page that must not run it */
		armed = 2;
		got.exec_faulted = probe_exec(planted);
		armed = 0;
		*planted = 0;

		RemoveVectoredExceptionHandler(handler);
		say("\n== the probes\n\n");
		say("    a store into the text segment    %s\n",
		    got.store_faulted ? "faulted" : "LANDED");
		say("    a call into the data segment     %s\n",
		    got.exec_faulted ? "faulted" : "RAN");
		if (!got.store_faulted || !got.exec_faulted)
			failures++;
	}

	/* PT_GNU_STACK says read and write and not execute, and that is what
	   this asks for. Nothing here honors a p_memsz stack size request,
	   because no linker emits one and the size is the stub's business. */
	stack = VirtualAlloc(NULL, (SIZE_T) opt.stack, MEM_RESERVE | MEM_COMMIT,
			     PAGE_READWRITE);
	if (!stack) {
		got.reason = "no stack";
		failures++;
		goto report;
	}
	strings = stack + opt.stack - 64;
	memcpy(strings, ARGV0, sizeof ARGV0);
	memcpy(&argv0_word, ARGV0, 8);
	vec = (uint64_t *)((UINT_PTR)(strings - 256) & ~(UINT_PTR) 15);

	/* argc, argv, its terminator, an empty envp, then auxv. This is the
	   shape WP-40 owns; what it is doing here is carrying three spike-local
	   keys, because handing addresses to the image any other way would mean
	   the image had to know something the loader had not told it. */
	vec[0] = 1;
	vec[1] = (uint64_t)(UINT_PTR) strings;
	vec[2] = 0;
	vec[3] = 0;
	vec[4] = AT_PAGESZ;		vec[5] = PAGE_SIZE;
	vec[6] = AT_SPIKE_HANDSHAKE;	vec[7] = hs_addr;
	vec[8] = AT_SPIKE_RODATA;	vec[9] = ro_addr;
	vec[10] = AT_SPIKE_BSS;		vec[11] = bss_addr;
	vec[12] = AT_NULL;		vec[13] = 0;

	hs = (struct handshake *)(UINT_PTR) hs_addr;
	memset(hs, 0, sizeof *hs);
	hs->in_magic = IN_MAGIC;

	say("\n== the transfer\n\n");
	say("    stack                            0x%016" PRIx64 ", %llu bytes\n",
	    (uint64_t)(UINT_PTR) stack, opt.stack);
	say("    entered with %%rsp at             0x%016" PRIx64 "\n",
	    (uint64_t)(UINT_PTR) vec);
	fflush(stdout);

	got.gpr_mask = abi_probe((void *)(UINT_PTR) got.entry, vec, hs, &got.xmm_mask);
	got.returned = 1;
	got.seen = *hs;

	say("    control came back                yes\n\n");
	failures += check("the image ran at all",
			  got.seen.out_magic == (IN_MAGIC ^ MAGIC_KEY), NULL);
	failures += check("it read the read-only segment",
			  got.seen.out_rodata == RODATA_WORD, NULL);
	failures += check("bss past p_filesz was zero",
			  got.seen.out_bss == 0, NULL);
	failures += check("it found argc on the stack",
			  got.seen.out_argc == 1, NULL);
	failures += check("it found argv[0]",
			  got.seen.out_argv0 == argv0_word, NULL);
	failures += check("it walked envp into auxv",
			  got.seen.out_pagesz == PAGE_SIZE, NULL);
	failures += check("it ran inside its own text",
			  got.seen.out_rip >= got.text_first &&
			  got.seen.out_rip < got.text_last, NULL);
	failures += check("callee-saved GPRs came back",
			  got.gpr_mask == 0, NULL);
	failures += check("callee-saved XMMs came back",
			  got.xmm_mask == 0, NULL);

report:
	if (got.reserved) {
		/* Idempotent in the only sense available to a process that is
		   about to exit: the range is handed back so a second run in
		   the same process would find it free. */
		VirtualFree((void *)(UINT_PTR) reserve_base, 0, MEM_RELEASE);
		if (stack)
			VirtualFree(stack, 0, MEM_RELEASE);
	}

	if (!opt.quiet) {
		printf("%scase_module_base=0x%" PRIx64 "\n", opt.terse ? "" : "\n",
		       (uint64_t)(UINT_PTR) GetModuleHandle(NULL));
		printf("case_reserved=%d\n", got.reserved);
		printf("case_entry=0x%" PRIx64 "\n", got.entry);
		printf("case_span_first=0x%" PRIx64 "\n", got.span_first);
		printf("case_span_last=0x%" PRIx64 "\n", got.span_last);
		printf("case_reserved_at=0x%" PRIx64 "\n", got.reserved_at);
		printf("case_granule_waste=%" PRIu64 "\n", got.granule_waste);
		printf("case_span_bytes=0x%" PRIx64 "\n", got.span_last - got.span_first);
		printf("case_free_run=0x%" PRIx64 "\n", got.free_run);
		printf("case_shared_pages=%u\n", got.shared_pages);
		printf("case_returned=%d\n", got.returned);
		printf("case_store_faulted=%d\n", got.store_faulted);
		printf("case_exec_faulted=%d\n", got.exec_faulted);
		printf("case_probed=%d\n", got.probed);
		printf("case_gpr_mask=0x%02x\n", got.gpr_mask);
		printf("case_xmm_mask=0x%03x\n", got.xmm_mask);
		printf("case_out_magic=0x%016" PRIx64 "\n", got.seen.out_magic);
		printf("case_out_argc=%" PRIu64 "\n", got.seen.out_argc);
		printf("case_out_rodata=0x%016" PRIx64 "\n", got.seen.out_rodata);
		printf("case_out_bss=0x%016" PRIx64 "\n", got.seen.out_bss);
		printf("case_out_rip=0x%016" PRIx64 "\n", got.seen.out_rip);
		printf("case_out_pagesz=%" PRIu64 "\n", got.seen.out_pagesz);
		printf("case_failures=%d\n", failures);
		printf("case_result=%s\n", failures ? "fail" : "pass");
	}
	free(image);
	return failures ? 1 : 0;
}
