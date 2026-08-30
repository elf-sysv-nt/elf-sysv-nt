/*
 * WP-41: the PE host stub.
 *
 * The image Windows thinks it is running while an ELF program runs. It carries
 * the ELF world's address space and none of its semantics: adopt the window
 * the parent reserved, confirm the process is not running under mitigations
 * the ELF world cannot survive, place the image in the window, build the stack
 * the psABI describes, and get out of the way.
 *
 * It does not reserve the window for itself. The measurement in
 * t/when-2026-08-30.txt found there is no instant inside this image early
 * enough: the kernel places the initial thread's stack and cygwin1.dll lays
 * out its own mappings before the first instruction here runs, and both are
 * served from the lowest free region, which is where a non-PIE image has to
 * go. So the parent reserves the window into a suspended child and this
 * adopts it. DR-0028. The one thing the parent cannot arrange from outside is
 * the initial stack, which is why this is linked with a smaller stack reserve
 * than the toolchain's default -- see ELF_STUB_STACK_RESERVE.
 *
 * The arguments after the file are the program's own argument vector, argv[0]
 * first, because the file being run and the name it runs under are not the
 * same thing: a `#!' chain routinely leaves the script's path in argv[0] while
 * the file entered is the interpreter's. Given no arguments at all, the path
 * stands as argv[0], which is what a hand-typed run wants.
 *
 * Usage:
 *   elfsysv-stub [options] ELF [ARGV0 [ARG]...]
 *
 * Options:
 *   -s N, --stack=N     Bytes for the image's stack. [default: 0x800000]
 *   -w, --self-window   Reserve the window here rather than adopting one, for
 *                       a run with no parent to arm it. It will normally fail,
 *                       and failing visibly is the point.
 *   -n, --dry-run       Do everything but the entry, and report.
 *   -v, --verbose       Report each step.
 *   -V, --version       Print the version and exit.
 *   -h, --help          Print this message and exit.
 *
 * Exit: it does not return. On a refusal, 1, or 2 for usage.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "reserve.h"
#include "../elf/elf_parse.h"
#include "../map/elf_map.h"
#include "../process/process_image.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const char PROG[] = "elfsysv-stub";
static const char RELEASE[] = "elfsysv-stub 1.0";

extern void elf_enter(void *entry, void *sp, uint64_t rdx);
extern void elf_terminate(void);
extern char **environ;

/* The far end of elf_terminate, once the status has crossed into the host
 * ABI. _exit rather than ExitProcess, so the Cygwin process the host still
 * believes it is running is torn down the way Cygwin expects. */
void elf_terminate_host(int status)
{
	_exit(status);
}

static struct {
	unsigned long long stack;
	int self_window, dry_run, verbose;
} opt = { 0x800000, 0, 0, 0 };

static void say(const char *fmt, ...)
{
	va_list ap;
	if (!opt.verbose)
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
	return 1;
}

/* The mitigations the ELF world cannot survive, checked rather than switched
 * off. Neither is something this image opts into: Control Flow Guard is a
 * link-time property and gcc emits no guard tables, and shadow stacks are
 * enabled by the CET-compatible bit in the extended DLL characteristics, which
 * gcc does not set either. So the opt-out is the absence of both marks, and
 * what is left to do at runtime is confirm that the absence held -- a policy
 * can also arrive from outside the image, and neither can be relaxed once the
 * process is running.
 *
 * A shadow stack would fault the first time the ELF image returned through a
 * frame the host did not push. Control Flow Guard would refuse the indirect
 * jump into e_entry. Both are fatal here and neither is subtle, so this
 * refuses rather than trying and finding out. */
static int mitigations_ok(char *why, size_t n)
{
	typedef BOOL (WINAPI *get_policy_fn)(HANDLE, int, PVOID, SIZE_T);
	get_policy_fn get_policy;
	/* Both policies are named by number rather than by the header's
	   enumerator, which this toolchain predates. The values are the
	   kernel's -- ProcessControlFlowGuardPolicy and
	   ProcessUserShadowStackPolicy -- and are not ours to renumber. Each
	   policy is a DWORD of flags whose low bit is the enable. */
	const int policy_cfg = 7, policy_shadow = 19;
	struct { DWORD flags; } cfg, shadow;

	get_policy = (get_policy_fn) (void *) GetProcAddress(
		GetModuleHandleA("kernel32.dll"), "GetProcessMitigationPolicy");
	if (!get_policy) {
		snprintf(why, n, "the host has no GetProcessMitigationPolicy, "
			 "so neither mitigation can be ruled out");
		return 0;
	}

	memset(&cfg, 0, sizeof cfg);
	if (get_policy(GetCurrentProcess(), policy_cfg,
		       &cfg, sizeof cfg) && (cfg.flags & 1)) {
		snprintf(why, n, "Control Flow Guard is enabled, and it would "
			 "refuse the indirect jump into the image entry");
		return 0;
	}

	memset(&shadow, 0, sizeof shadow);
	if (get_policy(GetCurrentProcess(), policy_shadow,
		       &shadow, sizeof shadow) && (shadow.flags & 1)) {
		snprintf(why, n, "user-mode shadow stacks are enabled, and the "
			 "image's first return would fault");
		return 0;
	}
	return 1;
}

/* What the placement needs, carried across elf_window_yield because nothing
 * may allocate between the release and the map. */
struct placement {
	const unsigned char *image;
	size_t               size;
	const elf_parsed    *parsed;
	elf_mapping          mapping;
	elf_map_diag         diag;
	elf_map_err          rc;
};

static int place(void *ctx, uint64_t base, uint64_t size,
                 uint64_t *took_lo, uint64_t *took_hi)
{
	struct placement *p = ctx;

	p->rc = elf_map(p->image, p->size, p->parsed, base, &p->mapping, &p->diag);
	if (p->rc != elf_map_ok)
		return -1;
	(void) size;
	*took_lo = p->mapping.base;
	*took_hi = p->mapping.base + p->mapping.size;
	return 0;
}

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
	buffer = malloc((size_t) length ? (size_t) length : 1);
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

static void usage(FILE *to)
{
	fprintf(to,
"Usage:\n"
"  elfsysv-stub [options] ELF [ARG]...\n"
"\n"
"Options:\n"
"  -s N, --stack=N     Bytes for the image's stack. [default: 0x800000]\n"
"  -w, --self-window   Reserve the window here rather than adopting one.\n"
"  -n, --dry-run       Do everything but the entry, and report.\n"
"  -v, --verbose       Report each step.\n"
"  -V, --version       Print the version and exit.\n"
"  -h, --help          Print this message and exit.\n");
}

int main(int argc, char **argv)
{
	elf_window *w = elf_window_low();
	unsigned char *image;
	size_t size = 0;
	elf_parsed parsed;
	elf_diag pdiag;
	elf_err perr;
	struct placement pl;
	proc_image_params pr;
	proc_layout layout;
	proc_diag ldiag;
	proc_err lerr;
	unsigned char random16[16];
	unsigned char *stack;
	char why[256] = "";
	win_err werr;
	const char *path;
	char *const *prog_argv;
	int i;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "--")) { i++; break; }
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return 0; }
		if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("%s\n", RELEASE);
			return 0;
		}
		if (!strcmp(a, "-s") || !strcmp(a, "--stack")) {
			if (++i >= argc) { usage(stderr); return 2; }
			opt.stack = strtoull(argv[i], NULL, 0);
		} else if (!strncmp(a, "--stack=", 8)) {
			opt.stack = strtoull(a + 8, NULL, 0);
		} else if (!strcmp(a, "-w") || !strcmp(a, "--self-window")) {
			opt.self_window = 1;
		} else if (!strcmp(a, "-n") || !strcmp(a, "--dry-run")) {
			opt.dry_run = 1;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			opt.verbose = 1;
		} else if (a[0] == '-' && a[1]) {
			fprintf(stderr, "%s: unknown option %s\n", PROG, a);
			return 2;
		} else {
			break;
		}
	}
	if (i >= argc) {
		fprintf(stderr, "%s: nothing to run. Give an ELF file.\n", PROG);
		return 2;
	}
	path = argv[i];
	prog_argv = (i + 1 < argc) ? argv + i + 1 : argv + i;

	if (!mitigations_ok(why, sizeof why))
		return refuse("%s", why);

	/* The window comes first, before the file is even read, because
	 * reading it allocates and an allocation with no requested base is
	 * served out of the lowest free region. */
	werr = opt.self_window
		? elf_window_reserve(w, ELF_WINDOW_BASE, ELF_WINDOW_SIZE)
		: elf_window_adopt(w, ELF_WINDOW_BASE, ELF_WINDOW_SIZE);
	if (werr != win_ok)
		return refuse("the low window at 0x%" PRIx64 " is not held (%s). "
			      "A stub is started by its parent, which reserves "
			      "the window into it while it is suspended.",
			      (uint64_t) ELF_WINDOW_BASE, win_err_name(werr));
	say("window 0x%" PRIx64 " for 0x%" PRIx64 " held", w->base, w->size);

	if (!(image = slurp(path, &size)))
		return 1;

	memset(&pdiag, 0, sizeof pdiag);
	if ((perr = elf_parse(image, size, &parsed, &pdiag)) != elf_ok)
		return refuse("%s: %s at %s: %s", path, elf_err_name(perr),
			      pdiag.field ? pdiag.field : "?", pdiag.msg);
	say("parsed: %u PT_LOAD, entry 0x%" PRIx64, parsed.load_count, parsed.e_entry);

	memset(&pl, 0, sizeof pl);
	pl.image = image;
	pl.size = size;
	pl.parsed = &parsed;
	werr = elf_window_yield(w, place, &pl);
	if (pl.rc != elf_map_ok)
		return refuse("%s: %s at %s: %s", path, elf_map_err_name(pl.rc),
			      pl.diag.field ? pl.diag.field : "?", pl.diag.msg);
	if (werr != win_ok && werr != win_err_place)
		say("the window remainder was not re-reserved (%s); the image is "
		    "placed and the brk region is unprotected", win_err_name(werr));
	say("mapped at 0x%" PRIx64 " for 0x%" PRIx64 ", entry 0x%" PRIx64,
	    pl.mapping.base, pl.mapping.size, pl.mapping.entry);

	/* PT_GNU_STACK asks for read and write and not execute, and that is
	 * what this gives it. An image that asked for an executable stack is
	 * refused rather than quietly given a non-executable one, because the
	 * failure that produces is a fault in a trampoline nowhere near here. */
	if (pl.mapping.has_gnu_stack && pl.mapping.stack_exec)
		return refuse("%s asks for an executable stack, which this host "
			      "does not grant", path);
	stack = VirtualAlloc(NULL, (SIZE_T) opt.stack, MEM_RESERVE | MEM_COMMIT,
			     PAGE_READWRITE);
	if (!stack)
		return refuse("no stack for the image");

	memset(&pr, 0, sizeof pr);
	pr.page_size = pl.mapping.page_size;
	pr.clktck = (uint64_t) sysconf(_SC_CLK_TCK);
	pr.uid = (uint32_t) getuid();
	pr.euid = (uint32_t) geteuid();
	pr.gid = (uint32_t) getgid();
	pr.egid = (uint32_t) getegid();
	pr.secure = (pr.uid != pr.euid || pr.gid != pr.egid) ? 1 : 0;
	pr.platform = "x86_64";
	pr.execfn = path;
	{
		/* AT_RANDOM. Not a cryptographic source and not claimed to be
		 * one; WP-2x owns the host entropy call and this is what
		 * stands until it lands. */
		uint64_t a = (uint64_t) time(NULL) ^ (uint64_t) (UINT_PTR) &pr;
		uint64_t b = (uint64_t) GetCurrentProcessId() * 0x9E3779B97F4A7C15ULL;
		memcpy(random16, &a, 8);
		memcpy(random16 + 8, &b, 8);
	}
	pr.random16 = random16;

	memset(&ldiag, 0, sizeof ldiag);
	lerr = proc_build_stack(&pl.mapping, &parsed, prog_argv, environ, &pr,
				(uint64_t) (UINT_PTR) elf_terminate,
				stack, stack + opt.stack, &layout, &ldiag);
	if (lerr != proc_ok)
		return refuse("cannot build the initial stack: %s at %s: %s",
			      proc_err_name(lerr),
			      ldiag.field ? ldiag.field : "?", ldiag.msg);
	say("stack built, entering at 0x%" PRIx64 " with %%rsp 0x%" PRIx64,
	    pl.mapping.entry, layout.sp);

	if (opt.dry_run) {
		printf("stub_window_base=0x%" PRIx64 "\n", w->base);
		printf("stub_map_base=0x%" PRIx64 "\n", pl.mapping.base);
		printf("stub_map_size=0x%" PRIx64 "\n", pl.mapping.size);
		printf("stub_entry=0x%" PRIx64 "\n", pl.mapping.entry);
		printf("stub_sp=0x%" PRIx64 "\n", layout.sp);
		printf("stub_argc=%" PRIu64 "\n", layout.argc);
		printf("stub_result=ready\n");
		return 0;
	}

	fflush(NULL);
	elf_enter((void *) (UINT_PTR) pl.mapping.entry,
		  (void *) (UINT_PTR) layout.sp, layout.rdx);
	return refuse("the image returned, which it has no stack to do");
}
