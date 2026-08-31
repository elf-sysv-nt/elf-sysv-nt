/* Characterize how the host places a mapping over an occupied span.
 *
 * WP-32's mapper reserved its span with MAP_FIXED and relied on the host to
 * refuse a second MAP_FIXED that landed on top of the first. Cygwin 3.0.7 did
 * refuse it; 3.6.10 does not, and `loader/map`'s occupied-span control fails
 * there (loader/map/issue/0001, spike/map-and-jump/issue/0002). Before the
 * redo picks a new way to turn an occupied span away, the host's actual
 * behaviour has to be pinned down rather than guessed, which is what this
 * takes.
 *
 * Six questions, each self-contained. Every one reserves its own region and
 * releases it, so the cases do not lean on each other and the order does not
 * matter. The Win32 question is in overlap-winprobe.c so <windows.h> stays out
 * of this translation unit, the same split loader/map keeps.
 *
 * The output is a key=value block and a per-question verdict. It measures; it
 * decides nothing. What the redo does with the answer is the redo's, and the
 * hold on WP-32 stays until an operator lifts it.
 *
 * Usage:
 *   overlap-probe [options]
 *
 * Options:
 *   -v, --verbose   Narrate each question as it runs.
 *   -t, --terse     The key=value block alone.
 *   -V, --version   Print the version and exit.
 *   -h, --help      Print this message and exit.
 */
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>

#define RELEASE "overlap-probe 1.0"

/* Declared in overlap-winprobe.c. Returns 1 if VirtualAlloc(MEM_RESERVE) over
 * the given committed span was refused, 0 if it was allowed, -1 on a probe
 * error. base_out/size_out report where a plain reserve landed for the record. */
int win_reserve_over_occupied(void *addr, size_t len);
int win_reserve_free(uint64_t *base_out, uint64_t *size_out);

static int verbose, terse;
static uint64_t PAGE, SPAN;   /* filled in main */

static void say(const char *fmt, ...)
{
	va_list ap;
	if (!verbose)
		return;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* mmap a region the kernel chooses, so it is known valid and known to have
 * been free. Returns the address or NULL. */
static void *reserve_somewhere(size_t len)
{
	void *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
	               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? NULL : p;
}

/* Does the span [addr, addr+len) appear in /proc/self/maps? */
static int visible_in_maps(uint64_t addr)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char line[512];
	int seen = 0;
	if (!f)
		return -1;
	while (fgets(line, sizeof line, f)) {
		uint64_t lo, hi;
		if (sscanf(line, "%llx-%llx", (unsigned long long *) &lo,
		           (unsigned long long *) &hi) == 2 &&
		    addr >= lo && addr < hi) {
			seen = 1;
			break;
		}
	}
	fclose(f);
	return seen;
}

/* Results, one field per measured fact, printed once at the end. -2 means a
 * question could not run (a prerequisite mmap failed); every real answer is
 * 0 or 1. */
static struct {
	int q1_fixed_over_occupied_allowed;   /* MAP_FIXED landed on the span */
	int q1_fixed_displaced_content;       /* and the sentinel underneath was lost */
	int q2_bare_hint_honored_when_free;   /* no MAP_FIXED, free hint, got==hint */
	int q3_bare_hint_relocates_when_busy; /* no MAP_FIXED, busy hint, got!=hint */
	int q3_original_survived;             /* and the sentinel underneath was kept */
	int q4_reservation_visible;           /* the span shows in /proc/self/maps */
	int q5_win_reserve_refused;           /* VirtualAlloc(MEM_RESERVE) refused it */
	int q6_fixed_over_free_ok;            /* the normal path still works */
	uint64_t win_reserve_base, win_reserve_size;
} r = { -2, -2, -2, -2, -2, -2, -2, -2, 0, 0 };

#define SENTINEL 0xA5A5A5A5A5A5A5A5ULL

/* Q1. MAP_FIXED over an occupied span. This is exactly what elf_map does. */
static void q1_fixed_over_occupied(void)
{
	void *a = reserve_somewhere(SPAN);
	volatile uint64_t *p;
	void *b;
	if (!a) { say("  q1: could not reserve\n"); return; }
	p = (volatile uint64_t *) a;
	*p = SENTINEL;
	b = mmap(a, SPAN, PROT_READ | PROT_WRITE,
	         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (b == MAP_FAILED) {
		r.q1_fixed_over_occupied_allowed = 0;
		say("  q1: MAP_FIXED over the span refused (errno %d)\n", errno);
	} else {
		r.q1_fixed_over_occupied_allowed = (b == a);
		r.q1_fixed_displaced_content = (*p != SENTINEL);
		say("  q1: MAP_FIXED returned %p for hint %p; sentinel %s\n",
		    b, a, *p == SENTINEL ? "survived" : "gone");
	}
	munmap(a, SPAN);
}

/* Q2 and Q3. Bare mmap (no MAP_FIXED) with an address hint: honored when the
 * hint is free, and relocated rather than laid on top when it is occupied.
 * The two are one experiment. First find a free address by reserving and
 * releasing; then hint at it free (Q2), then occupy it and hint at it again
 * (Q3). Nothing else in this single thread allocates between, so the freed
 * address stays free for the free-hint probe. */
static void q2q3_bare_hint(void)
{
	void *scout = reserve_somewhere(SPAN);
	void *hint, *free_got, *occupant, *busy_got;
	volatile uint64_t *p;
	if (!scout) { say("  q2q3: could not scout\n"); return; }
	hint = scout;
	munmap(scout, SPAN);

	free_got = mmap(hint, SPAN, PROT_READ | PROT_WRITE,
	                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (free_got == MAP_FAILED) { say("  q2q3: free-hint mmap failed\n"); return; }
	r.q2_bare_hint_honored_when_free = (free_got == hint);
	say("  q2: free hint %p -> %p (%s)\n", hint, free_got,
	    free_got == hint ? "honored" : "ignored");

	/* free_got now occupies the region. Mark it, hint at it again. */
	p = (volatile uint64_t *) free_got;
	*p = SENTINEL;
	occupant = free_got;
	busy_got = mmap(occupant, SPAN, PROT_READ | PROT_WRITE,
	                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (busy_got == MAP_FAILED) {
		say("  q3: busy-hint mmap failed (errno %d)\n", errno);
	} else {
		r.q3_bare_hint_relocates_when_busy = (busy_got != occupant);
		r.q3_original_survived = (*p == SENTINEL);
		say("  q3: busy hint %p -> %p (%s); sentinel %s\n",
		    occupant, busy_got,
		    busy_got != occupant ? "relocated" : "overlaid",
		    *p == SENTINEL ? "survived" : "gone");
		if (busy_got != occupant) munmap(busy_got, SPAN);
	}
	munmap(occupant, SPAN);
}

/* Q4. Is a live reservation visible in /proc/self/maps? A pre-scan strategy
 * for the redo depends on the answer. */
static void q4_visibility(void)
{
	void *a = reserve_somewhere(SPAN);
	if (!a) { say("  q4: could not reserve\n"); return; }
	r.q4_reservation_visible = visible_in_maps((uint64_t)(uintptr_t) a);
	say("  q4: reservation at %p %s in /proc/self/maps\n",
	    a, r.q4_reservation_visible == 1 ? "visible" :
	       r.q4_reservation_visible == 0 ? "absent" : "unreadable");
	munmap(a, SPAN);
}

/* Q5. Does the Win32 layer still refuse a reserve over an occupied span, even
 * though the POSIX layer above it no longer does? Localizes the divergence. */
static void q5_win_reserve(void)
{
	void *a = reserve_somewhere(SPAN);
	if (!a) { say("  q5: could not reserve\n"); return; }
	r.q5_win_reserve_refused = win_reserve_over_occupied(a, SPAN);
	say("  q5: VirtualAlloc(MEM_RESERVE) over the span %s\n",
	    r.q5_win_reserve_refused == 1 ? "refused" :
	    r.q5_win_reserve_refused == 0 ? "allowed" : "probe error");
	munmap(a, SPAN);
}

/* Q6. Control: MAP_FIXED over a free span still succeeds. */
static void q6_fixed_over_free(void)
{
	void *scout = reserve_somewhere(SPAN);
	void *hint, *got;
	if (!scout) { say("  q6: could not scout\n"); return; }
	hint = scout;
	munmap(scout, SPAN);
	got = mmap(hint, SPAN, PROT_READ | PROT_WRITE,
	           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (got == MAP_FAILED) {
		r.q6_fixed_over_free_ok = 0;
		say("  q6: MAP_FIXED over free span failed (errno %d)\n", errno);
	} else {
		r.q6_fixed_over_free_ok = (got == hint);
		say("  q6: MAP_FIXED over free span -> %p\n", got);
		munmap(got, SPAN);
	}
}

static void p(const char *key, int val)
{
	if (val == -2)
		printf("%s=na\n", key);
	else
		printf("%s=%d\n", key, val);
}

int main(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			printf("Usage:\n  overlap-probe [options]\n\n"
			       "Options:\n"
			       "  -v, --verbose   Narrate each question as it runs.\n"
			       "  -t, --terse     The key=value block alone.\n"
			       "  -V, --version   Print the version and exit.\n"
			       "  -h, --help      Print this message and exit.\n");
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			printf("%s\n", RELEASE);
			return 0;
		} else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
			verbose = 1;
		} else if (!strcmp(a, "-t") || !strcmp(a, "--terse")) {
			terse = 1;
		} else {
			fprintf(stderr, "overlap-probe: unknown option %s\n", a);
			return 2;
		}
	}

	PAGE = (uint64_t) sysconf(_SC_PAGESIZE);
	/* A span wide enough to cross several allocation granules, so an overlap
	 * is a real overlap and not a rounding artifact. Eight 64 KB granules. */
	SPAN = 8 * 0x10000ULL;

	if (!terse) {
		printf("host mmap placement over an occupied span\n\n");
		fflush(stdout);
	}

	q1_fixed_over_occupied();
	q2q3_bare_hint();
	q4_visibility();
	q5_win_reserve();
	q6_fixed_over_free();
	win_reserve_free(&r.win_reserve_base, &r.win_reserve_size);

	if (!terse)
		printf("\n");
	printf("page_size=0x%llx\n", (unsigned long long) PAGE);
	printf("span=0x%llx\n", (unsigned long long) SPAN);
	p("q1_fixed_over_occupied_allowed", r.q1_fixed_over_occupied_allowed);
	p("q1_fixed_displaced_content", r.q1_fixed_displaced_content);
	p("q2_bare_hint_honored_when_free", r.q2_bare_hint_honored_when_free);
	p("q3_bare_hint_relocates_when_busy", r.q3_bare_hint_relocates_when_busy);
	p("q3_original_survived", r.q3_original_survived);
	p("q4_reservation_visible", r.q4_reservation_visible);
	p("q5_win_reserve_refused", r.q5_win_reserve_refused);
	p("q6_fixed_over_free_ok", r.q6_fixed_over_free_ok);
	printf("win_reserve_base=0x%llx\n", (unsigned long long) r.win_reserve_base);
	printf("win_reserve_size=0x%llx\n", (unsigned long long) r.win_reserve_size);
	return 0;
}
