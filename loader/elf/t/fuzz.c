/* WP-T1 fuzz target: feed the WP-31 parser a great many malformed, truncated,
 * and randomly mutated images and hold it to two properties on every one:
 *
 *   1. It does not crash and does not perform undefined behaviour. Each case
 *      is placed so its last byte abuts a guard page (see harness.h), so a
 *      read past the end faults and the run dies; the binary is built with
 *      -fsanitize=undefined -fsanitize-undefined-trap-on-error, so an integer
 *      overflow, a bad shift, or a misaligned access traps as well. A clean
 *      run to completion is the evidence that neither happened.
 *
 *   2. Every rejection names a field. A nonzero return with a null field or an
 *      empty message is a defect and stops the run. Every acceptance is
 *      re-checked against the invariants the parser promises its callers:
 *      each reported offset and span is re-proven to lie in the image. An
 *      acceptance that violates one is the parser trusting a field it should
 *      have bounded, and it stops the run too.
 *
 * Inputs come two ways, interleaved: a random mutation of one of the committed
 * corpus seeds (the productive case, since a nearly-valid image reaches the
 * deep walks), and a purely random buffer (which sweeps the shallow rejects
 * and the occasional lucky header). The seed is fixed by default so a crash
 * reproduces; pass --seed to explore further.
 *
 * Usage:
 *   fuzz [options]
 *
 * Options:
 *   -n N, --count=N     cases to run [default: 1000000]
 *   -s HEX, --seed=HEX  64-bit PRNG seed [default: 0x9e3779b97f4a7c15]
 *   --corpus=DIR        directory of seed images [default: corpus]
 *   --cap=N             largest case in bytes [default: 16384]
 *   -q, --quiet         final line only
 *   -h, --help          print this message and exit
 *
 * Exit: 0 all cases clean, 1 a rejection lacked a diagnostic or an acceptance
 * violated an invariant, 2 usage or setup failure. A crash (SIGSEGV/SIGILL)
 * is the sanitizer or guard page catching a real defect.
 */
#include "../elf_parse.h"
#include "../elf_types.h"

#include <sys/mman.h>
#include <unistd.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* splitmix64: small, fast, and deterministic across machines. */
static uint64_t rng_state;
static uint64_t rng(void)
{
	uint64_t z = (rng_state += 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return z ^ (z >> 31);
}
static uint64_t rng_below(uint64_t n) { return n ? rng() % n : 0; }


/* One guarded arena, reused across cases: the guard page never moves, and a
 * case of length n is placed ending at it with a single memcpy, so a case
 * costs no syscalls. */
static unsigned char *g_guard;   /* first byte of the guard page */
static size_t g_cap;             /* most bytes a case may hold */

static int arena_init(size_t cap)
{
	long pg = sysconf(_SC_PAGESIZE);
	size_t page = (pg > 0) ? (size_t)pg : 4096;
	size_t datapages = (cap + page - 1) / page;
	size_t maplen;
	unsigned char *m, *guard;
	if (datapages == 0) datapages = 1;
	maplen = (datapages + 1) * page;
	m = (unsigned char *)mmap(NULL, maplen, PROT_READ | PROT_WRITE,
	                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m == MAP_FAILED) return -1;
	guard = m + datapages * page;
	if (mprotect(guard, page, PROT_NONE) != 0) return -1;
	g_guard = guard;
	g_cap = datapages * page;
	return 0;
}
static unsigned char *arena_place(const unsigned char *data, size_t n)
{
	unsigned char *p = g_guard - n;
	if (n) memcpy(p, data, n);
	return p;
}

/* Seed corpus, loaded once. */
#define MAX_SEEDS 64
static unsigned char *seed_buf[MAX_SEEDS];
static size_t seed_len[MAX_SEEDS];
static int seed_count;

static void load_seeds(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *de;
	char path[1024];
	if (!d) return;
	while ((de = readdir(d)) && seed_count < MAX_SEEDS) {
		size_t l = strlen(de->d_name);
		FILE *f;
		long sz;
		if (l < 4 || strcmp(de->d_name + l - 4, ".elf") != 0) continue;
		snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
		f = fopen(path, "rb");
		if (!f) continue;
		fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
		if (sz > 0 && (size_t)sz <= g_cap) {
			unsigned char *b = (unsigned char *)malloc((size_t)sz);
			if (b && fread(b, 1, (size_t)sz, f) == (size_t)sz) {
				seed_buf[seed_count] = b;
				seed_len[seed_count] = (size_t)sz;
				seed_count++;
			} else {
				free(b);
			}
		}
		fclose(f);
	}
	closedir(d);
}

/* Build one case into scratch (length returned). Half mutate a seed, half are
 * random; the mix keeps both the deep walks and the shallow rejects busy. */
static size_t make_case(unsigned char *scratch)
{
	size_t n;
	if (seed_count && (rng() & 1)) {
		int si = (int)rng_below((uint64_t)seed_count);
		size_t base = seed_len[si];
		int muts, k;
		/* truncate or extend around the seed length */
		n = base;
		switch (rng() & 7) {
		case 0: n = rng_below(base + 1); break;         /* truncate */
		case 1: n = base + rng_below(64); break;        /* extend */
		default: break;
		}
		if (n > g_cap) n = g_cap;
		memset(scratch, 0, n);
		memcpy(scratch, seed_buf[si], n < base ? n : base);
		muts = 1 + (int)rng_below(24);
		for (k = 0; k < muts && n; k++) {
			size_t at = (size_t)rng_below(n);
			unsigned char b = (unsigned char)rng();
			switch (rng() & 3) {
			case 0: scratch[at] ^= (unsigned char)(1u << (rng() & 7)); break;
			default: scratch[at] = b; break;
			}
		}
	} else {
		size_t i;
		n = (size_t)rng_below(g_cap + 1);
		for (i = 0; i + 8 <= n; i += 8) {
			uint64_t r = rng();
			memcpy(scratch + i, &r, 8);
		}
		for (; i < n; i++) scratch[i] = (unsigned char)rng();
		/* often stamp a plausible header prefix so random cases get past the
		 * magic gate and reach the table walks */
		if (n >= 64 && (rng() & 1)) {
			scratch[0] = 0x7f; scratch[1] = 'E'; scratch[2] = 'L';
			scratch[3] = 'F';  scratch[4] = 2;   scratch[5] = 1;
			scratch[6] = 1;
			scratch[18] = 62; scratch[19] = 0;   /* EM_X86_64 */
			scratch[16] = 3;  scratch[17] = 0;   /* ET_DYN */
		}
	}
	return n;
}


/* Small in-file copies of the two checks the invariant pass needs, defined
 * below; declared here so check_invariants can use them. */
int region_in(uint64_t size, uint64_t off, uint64_t len);
int mul_check(uint64_t a, uint64_t b, uint64_t *r);

/* Re-prove, independently of the parser, that an accepted view lies wholly in
 * the image. A failure here is the parser having trusted a field. Returns a
 * static reason string on violation, NULL when every invariant holds. */
static const char *check_invariants(const elf_parsed *p, uint64_t size)
{
	unsigned i;
	if (p->phnum && !region_in(size, p->phoff, (uint64_t)p->phnum * 56))
		return "phdr table out of range";
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		if (!region_in(size, s->off, s->filesz)) return "PT_LOAD file range";
		if (s->filesz > s->memsz) return "PT_LOAD filesz > memsz";
		if (s->memsz == 0) return "PT_LOAD zero memsz";
	}
	if (p->has_dynamic) {
		uint64_t span;
		if (mul_check(p->dyn_count + 1, 16, &span) ||
		    !region_in(size, p->dyn_off, span))
			return "dynamic array out of range";
	}
	if (p->has_strtab) {
		if (!region_in(size, p->strtab_off, p->strsz)) return "strtab range";
		for (i = 0; i < p->needed_count; i++)
			if (p->needed[i] >= p->strsz) return "DT_NEEDED past strtab";
		if (p->has_soname && p->soname >= p->strsz) return "soname past strtab";
	}
	if (p->has_symtab && !region_in(size, p->symtab_off, p->syment))
		return "symtab start out of range";
	if (p->has_versym && !region_in(size, p->versym_off, 2))
		return "versym start out of range";
	if (p->has_verdef) {
		if (!region_in(size, p->verdef_off, 20)) return "verdef start range";
		if (p->verdefnum == 0) return "verdef with zero count";
	}
	if (p->has_verneed) {
		if (!region_in(size, p->verneed_off, 16)) return "verneed start range";
		if (p->verneednum == 0) return "verneed with zero count";
	}
	return NULL;
}

/* Definitions of the two checks declared above. Kept in the driver so it does
 * not reach into the parser's internals for them. */
int region_in(uint64_t size, uint64_t off, uint64_t len)
{
	if (off > size) return 0;
	return len <= size - off;
}
int mul_check(uint64_t a, uint64_t b, uint64_t *r)
{
	if (a != 0 && b > UINT64_MAX / a) return 1;
	*r = a * b; return 0;
}

int main(int argc, char **argv)
{
	uint64_t count = 1000000;
	uint64_t seed = 0x9e3779b97f4a7c15ULL;
	size_t cap = 16384;
	const char *corpus = "corpus";
	int quiet = 0;
	uint64_t i, rejects = 0, accepts = 0;
	unsigned char *scratch;
	int a;

	for (a = 1; a < argc; a++) {
		const char *s = argv[a];
		if (!strcmp(s, "-h") || !strcmp(s, "--help")) {
			printf("usage: fuzz [-n N] [-s HEX] [--corpus=DIR] [--cap=N] [-q]\n");
			return 0;
		} else if (!strcmp(s, "-q") || !strcmp(s, "--quiet")) {
			quiet = 1;
		} else if (!strcmp(s, "-n") && a + 1 < argc) {
			count = strtoull(argv[++a], NULL, 0);
		} else if (!strncmp(s, "--count=", 8)) {
			count = strtoull(s + 8, NULL, 0);
		} else if (!strcmp(s, "-s") && a + 1 < argc) {
			seed = strtoull(argv[++a], NULL, 0);
		} else if (!strncmp(s, "--seed=", 7)) {
			seed = strtoull(s + 7, NULL, 0);
		} else if (!strncmp(s, "--corpus=", 9)) {
			corpus = s + 9;
		} else if (!strncmp(s, "--cap=", 6)) {
			cap = (size_t)strtoull(s + 6, NULL, 0);
		} else {
			fprintf(stderr, "fuzz: unknown argument %s\n", s);
			return 2;
		}
	}

	rng_state = seed;
	if (arena_init(cap) != 0) {
		fprintf(stderr, "fuzz: could not map the guarded arena\n");
		return 2;
	}
	load_seeds(corpus);
	scratch = (unsigned char *)malloc(g_cap ? g_cap : 1);
	if (!scratch) { fprintf(stderr, "fuzz: out of memory\n"); return 2; }

	if (!quiet)
		fprintf(stderr, "fuzz: %llu cases, seed 0x%llx, cap %llu, %d seeds\n",
		        (unsigned long long)count, (unsigned long long)seed,
		        (unsigned long long)g_cap, seed_count);

	for (i = 0; i < count; i++) {
		size_t n = make_case(scratch);
		unsigned char *img = arena_place(scratch, n);
		elf_parsed out;
		elf_diag diag;
		elf_err e = elf_parse(img, n, &out, &diag);
		if (e == elf_ok) {
			const char *why = check_invariants(&out, n);
			if (why) {
				fprintf(stderr,
				        "\nfuzz: INVARIANT VIOLATED at case %llu: %s "
				        "(n=%llu)\n", (unsigned long long)i, why,
				        (unsigned long long)n);
				return 1;
			}
			accepts++;
		} else {
			if (!diag.field || diag.msg[0] == 0) {
				fprintf(stderr,
				        "\nfuzz: rejection without a diagnostic at case %llu "
				        "(code %s)\n", (unsigned long long)i, elf_err_name(e));
				return 1;
			}
			rejects++;
		}
		if (!quiet && (i & 0xfffff) == 0xfffff)
			fprintf(stderr, "\r  %llu / %llu",
			        (unsigned long long)(i + 1), (unsigned long long)count);
	}

	if (!quiet) fprintf(stderr, "\n");
	printf("fuzz: %llu cases clean  (%llu accepted, %llu rejected)\n",
	       (unsigned long long)count, (unsigned long long)accepts,
	       (unsigned long long)rejects);
	return 0;
}
