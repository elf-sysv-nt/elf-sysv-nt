/* WP-42 fuzz target: the manifest unpacker.
 *
 * The manifest is the only thing in this package that reads bytes it did not
 * write in the same call. In the real fork it is a block the parent placed in
 * the child, and the child parses it before it has repaired anything -- before
 * the loader lock is meaningful, before the thread pointer is right, and while
 * the reservations it describes are the only thing standing between the ELF
 * world and the first host allocation. A parser that walks off the end there
 * has nothing to catch it.
 *
 * So it is fed malformed, truncated and mutated manifests and held to four
 * properties on every one:
 *
 *   1. It does not crash and does not perform undefined behaviour. Each
 *      manifest is placed so its last byte abuts a guard page (WP-31's
 *      harness), so a read at buf[len] faults rather than returning adjacent
 *      bytes, and the binary is built with -fsanitize=undefined
 *      -fsanitize-undefined-trap-on-error. A clean run is the evidence.
 *
 *   2. Every refusal says something. A -1 with an empty reason is a defect,
 *      because the child's only output on that path is that string.
 *
 *   3. Every acceptance is re-derived from the bytes. The count must be within
 *      the bound, the length must be exactly what that count occupies, every
 *      region must have a nonzero size that does not wrap, a known kind, and a
 *      terminated name, and the regions must be sorted and disjoint. These are
 *      re-checked here against the returned array rather than trusted from the
 *      parser that produced it.
 *
 *   4. Nothing outside the array is written. The output array is bracketed by
 *      canaries and both are checked after every case, so a count that indexed
 *      past ELF_FORK_REGION_MAX would be caught even if the run did not fault.
 *
 * Usage:
 *   fuzz [options]
 *
 * Options:
 *   -n N, --count=N     cases to run [default: 200000]
 *   -s HEX, --seed=HEX  64-bit PRNG seed [default: 0x9e3779b97f4a7c15]
 *   -q, --quiet         errors only
 *   -h, --help          print this message and exit
 *
 * Exit: 0 every case held, 1 a property was violated, 2 usage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../fork.h"
#include "../../elf/t/harness.h"

static uint64_t rng_state;

static uint64_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}

#define MF_HDR 12u
#define MF_REC (8u + 8u + 4u + 4u + ELF_FORK_WHAT_MAX)
#define MAX_BYTES (MF_HDR + ELF_FORK_REGION_MAX * MF_REC + 64)

/* A well-formed manifest of n regions, which the mutator then damages. Starting
 * from valid bytes is what puts the cases in the neighbourhood of acceptance,
 * where a parser's mistakes actually live; a purely random buffer is refused at
 * the magic and proves only that the magic is checked. */
static size_t build_valid(unsigned char *buf, unsigned n)
{
	elf_fork_state fs;
	elf_fork_state_init(&fs, NULL, NULL, NULL, NULL);
	for (unsigned i = 0; i < n; i++)
		elf_fork_region_add(&fs, UINT64_C(0x100000) + (uint64_t)i * 0x20000,
		                    0x10000, (i & 1) ? elf_fork_region_commit
		                                     : elf_fork_region_reserve,
		                    (uint32_t)(i % 8), "r");
	size_t used = 0;
	if (elf_fork_manifest_pack(&fs, buf, MAX_BYTES, &used) != 0)
		return 0;
	return used;
}

static int quiet;
static unsigned long cases;

static int fail(const char *what, unsigned long i)
{
	printf("wp42 fuzz: case %lu violated: %s\n", i, what);
	return 1;
}

/* Re-derive an acceptance from the bytes rather than from the parser. */
static const char *recheck(const elf_fork_region *r, int n, size_t len)
{
	if (n < 0 || n > ELF_FORK_REGION_MAX)
		return "the count is outside the bound";
	if (len != MF_HDR + (size_t)n * MF_REC)
		return "the length does not match the accepted count";
	uint64_t prev_end = 0;
	for (int i = 0; i < n; i++) {
		if (r[i].size == 0)
			return "an accepted region has zero size";
		if (r[i].base > UINT64_MAX - r[i].size)
			return "an accepted region wraps";
		if (r[i].kind != elf_fork_region_reserve &&
		    r[i].kind != elf_fork_region_commit)
			return "an accepted region has an unknown kind";
		if (i > 0 && r[i].base < prev_end)
			return "accepted regions overlap or are unsorted";
		prev_end = r[i].base + r[i].size;
		size_t j = 0;
		while (j < ELF_FORK_WHAT_MAX && r[i].what[j] != 0)
			j++;
		if (j >= ELF_FORK_WHAT_MAX)
			return "an accepted region name is not terminated";
	}
	return NULL;
}

int main(int argc, char **argv)
{
	unsigned long count = 200000;
	rng_state = UINT64_C(0x9e3779b97f4a7c15);

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			fputs("Usage: fuzz [-n N] [-s HEX] [-q]\n", stdout);
			return 0;
		} else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) {
			quiet = 1;
		} else if ((!strcmp(a, "-n") || !strcmp(a, "--count")) && i + 1 < argc) {
			count = strtoul(argv[++i], NULL, 0);
		} else if ((!strcmp(a, "-s") || !strcmp(a, "--seed")) && i + 1 < argc) {
			rng_state = strtoull(argv[++i], NULL, 0);
		} else if (!strncmp(a, "--count=", 8)) {
			count = strtoul(a + 8, NULL, 0);
		} else if (!strncmp(a, "--seed=", 7)) {
			rng_state = strtoull(a + 7, NULL, 0);
		} else {
			fputs("Usage: fuzz [-n N] [-s HEX] [-q]\n", stderr);
			return 2;
		}
	}
	if (rng_state == 0)
		rng_state = 1;

	/* The output array between two canaries. The parser is bounded by
	 * ELF_FORK_REGION_MAX and the header says so, but a count that got past
	 * the bound check would write here, and this is what notices. */
	struct {
		uint64_t lo[8];
		elf_fork_region out[ELF_FORK_REGION_MAX];
		uint64_t hi[8];
	} arena;

	unsigned char scratch[MAX_BYTES];
	unsigned long accepted = 0, refused = 0;

	for (cases = 0; cases < count; cases++) {
		unsigned n = (unsigned)(rng() % (ELF_FORK_REGION_MAX + 2));
		size_t len = build_valid(scratch, n > ELF_FORK_REGION_MAX
		                                  ? ELF_FORK_REGION_MAX : n);
		if (len == 0)
			len = MF_HDR;

		/* Damage. Sometimes the length, sometimes the bytes, sometimes both,
		 * and one case in sixteen is pure noise at a random length. */
		unsigned mode = (unsigned)(rng() % 16);
		if (mode == 0) {
			len = (size_t)(rng() % MAX_BYTES);
			for (size_t j = 0; j < len; j++)
				scratch[j] = (unsigned char)rng();
		} else {
			unsigned bites = (unsigned)(rng() % 6);
			for (unsigned b = 0; b < bites && len > 0; b++)
				scratch[rng() % len] = (unsigned char)rng();
			if (mode % 4 == 1 && len > 0)
				len -= (size_t)(rng() % len);      /* truncate */
			else if (mode % 4 == 2 && len + 8 < MAX_BYTES)
				len += (size_t)(rng() % 8) + 1;    /* trailing bytes */
		}

		guard_buf g;
		if (guard_load(scratch, len, &g) != 0)
			return fail("the guarded buffer could not be made", cases);

		memset(&arena, 0, sizeof arena);
		for (int k = 0; k < 8; k++) {
			arena.lo[k] = UINT64_C(0xfeedfacecafebeef) ^ (uint64_t)k;
			arena.hi[k] = UINT64_C(0x0123456789abcdef) ^ (uint64_t)k;
		}

		char why[ELF_FORK_WHY_MAX];
		memset(why, 0, sizeof why);
		int rc = elf_fork_manifest_unpack(g.base, g.len, arena.out,
		                                  why, sizeof why);
		guard_free(&g);

		for (int k = 0; k < 8; k++) {
			if (arena.lo[k] != (UINT64_C(0xfeedfacecafebeef) ^ (uint64_t)k))
				return fail("the unpacker wrote before its array", cases);
			if (arena.hi[k] != (UINT64_C(0x0123456789abcdef) ^ (uint64_t)k))
				return fail("the unpacker wrote past its array", cases);
		}

		if (rc < 0) {
			refused++;
			if (why[0] == '\0')
				return fail("a refusal with no reason", cases);
			if (memchr(why, '\0', sizeof why) == NULL)
				return fail("a reason that is not terminated", cases);
		} else {
			accepted++;
			const char *bad = recheck(arena.out, rc, len);
			if (bad != NULL)
				return fail(bad, cases);
		}
	}

	if (!quiet)
		printf("wp42 fuzz: %lu cases, %lu accepted, %lu refused, "
		       "no crash, no write outside the array\n",
		       count, accepted, refused);

	/* A run that accepted nothing has not exercised the acceptance path, which
	 * is where the re-derivation lives; that is a broken corpus, not a pass. */
	if (accepted == 0) {
		printf("wp42 fuzz: no case was accepted; the corpus is not reaching "
		       "the parser\n");
		return 1;
	}
	return 0;
}
