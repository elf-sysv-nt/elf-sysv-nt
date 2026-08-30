/* WP-41 fuzz target: the branch reads the first bytes of whatever a user asked
 * to run, so every byte it looks at is attacker-shaped from the first line.
 * This feeds it malformed, truncated and randomly mutated heads and holds it to
 * four properties on every one:
 *
 *   1. It does not crash and does not perform undefined behaviour. Each head is
 *      placed so its last byte abuts a guard page (WP-31's harness), so a read
 *      at head[n] faults rather than returning adjacent bytes, and the binary
 *      is built with -fsanitize=undefined -fsanitize-undefined-trap-on-error.
 *      A clean run to completion is the evidence that neither happened.
 *
 *   2. Every refusal names a field and says something. A nonzero return with a
 *      null field or an empty message is a defect and stops the run.
 *
 *   3. Every acceptance is re-derived from the head. An elf verdict is
 *      re-checked against the magic, class, machine and type bytes; a script
 *      verdict must carry a non-empty interpreter that is a NUL-terminated
 *      string with no newline in it and no NUL of its own, since that string
 *      becomes a path the spawn path opens.
 *
 *   4. The chain terminates. The resolver is run over a fixture file system
 *      whose heads are the mutants, and it must return within the depth limit
 *      with a plausible vector rather than looping or running off its storage.
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
#include "../binfmt.h"
#include "../../elf/t/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state;

static uint64_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}

/* The seeds: one of each thing the classifier has a branch for, mutated into
 * the neighbourhood of a valid head, which is where the interesting rejects
 * live. A purely random buffer sweeps the shallow ones. */
static unsigned char seed_elf[64];
static const char seed_sh1[] = "#!/bin/sh\n";
static const char seed_sh2[] = "#!/usr/bin/env python3 -u\n";
static const char seed_sh3[] = "#!   /bin/awk -f \t\n";
static const char seed_mz[] = "MZ\x90\x00\x03\x00\x00\x00";

struct seed { const unsigned char *bytes; size_t len; };
static struct seed seeds[5];

static void build_seeds(void)
{
	memset(seed_elf, 0, sizeof seed_elf);
	memcpy(seed_elf, "\177ELF", 4);
	seed_elf[4] = 2; seed_elf[5] = 1; seed_elf[6] = 1;
	seed_elf[16] = 2;              /* ET_EXEC */
	seed_elf[18] = 62;             /* EM_X86_64 */
	seeds[0].bytes = seed_elf;               seeds[0].len = sizeof seed_elf;
	seeds[1].bytes = (const unsigned char *) seed_sh1; seeds[1].len = sizeof seed_sh1 - 1;
	seeds[2].bytes = (const unsigned char *) seed_sh2; seeds[2].len = sizeof seed_sh2 - 1;
	seeds[3].bytes = (const unsigned char *) seed_sh3; seeds[3].len = sizeof seed_sh3 - 1;
	seeds[4].bytes = (const unsigned char *) seed_mz;  seeds[4].len = sizeof seed_mz - 1;
}

/* One mutant, written into buf, length returned. Half the cases mutate a seed
 * and half are noise; a truncation is applied to either. */
static size_t mutate(unsigned char *buf, size_t cap)
{
	size_t n, i, edits;

	if (rng() & 1) {
		const struct seed *s = &seeds[rng() % 5];
		n = s->len;
		if (n > cap)
			n = cap;
		memcpy(buf, s->bytes, n);
		edits = rng() % 5;
		for (i = 0; i < edits && n; i++)
			buf[rng() % n] = (unsigned char)(rng() & 0xff);
	} else {
		n = rng() % (cap + 1);
		for (i = 0; i < n; i++)
			buf[i] = (unsigned char)(rng() & 0xff);
	}
	if ((rng() & 3) == 0 && n)
		n = rng() % n;
	return n;
}

static int complain(const char *what, const unsigned char *head, size_t n)
{
	size_t i;
	printf("  VIOLATION: %s\n  head (%zu bytes):", what, n);
	for (i = 0; i < n && i < 48; i++)
		printf(" %02x", head[i]);
	printf("%s\n", n > 48 ? " ..." : "");
	return 1;
}

/* Property 3 for a script verdict: the interpreter is going to be opened, so
 * it has to be a real path-shaped string rather than whatever fell out. */
static int interp_is_sane(const binfmt_probe *p)
{
	size_t len = strnlen(p->interp, sizeof p->interp);
	if (len == 0 || len == sizeof p->interp)
		return 0;
	if (memchr(p->interp, '\n', len))
		return 0;
	if (p->has_arg && strnlen(p->arg, sizeof p->arg) == sizeof p->arg)
		return 0;
	return 1;
}

static int elf_verdict_is_earned(const unsigned char *h, size_t n,
                                 const binfmt_probe *p)
{
	if (n < 20 || memcmp(h, "\177ELF", 4))
		return 0;
	if (h[4] != 2 || h[5] != 1)
		return 0;
	if ((h[18] | (h[19] << 8)) != 62)
		return 0;
	return p->e_type == 2 || p->e_type == 3;
}

/* The fixture file system for property 4: one file per name, each holding a
 * mutant, so a resolve walks a chain of nonsense and has to stop anyway. */
#define FS_N 6
static unsigned char fs_head[FS_N][BINFMT_HEAD_MAX];
static size_t fs_len[FS_N];
static const char *fs_name[FS_N] = { "/f0", "/f1", "/f2", "/f3", "/f4", "/f5" };

static long fixture_head(void *ctx, const char *path, unsigned char *buf, size_t n)
{
	unsigned i;
	(void) ctx;
	for (i = 0; i < FS_N; i++) {
		if (strcmp(fs_name[i], path))
			continue;
		if (fs_len[i] < n)
			n = fs_len[i];
		memcpy(buf, fs_head[i], n);
		return (long) n;
	}
	return -1;
}

static void usage(FILE *to)
{
	fprintf(to,
"Usage:\n"
"  fuzz [options]\n"
"\n"
"Options:\n"
"  -n N, --count=N     cases to run [default: 200000]\n"
"  -s HEX, --seed=HEX  64-bit PRNG seed [default: 0x9e3779b97f4a7c15]\n"
"  -q, --quiet         errors only\n"
"  -h, --help          print this message and exit\n");
}

int main(int argc, char **argv)
{
	unsigned long long count = 200000, seed = 0x9e3779b97f4a7c15ULL;
	int quiet = 0, i;
	unsigned long long c;
	unsigned long long accepted_elf = 0, accepted_sh = 0, refused = 0,
	                   host = 0, unknown = 0, chains = 0;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(stdout); return 0; }
		else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) quiet = 1;
		else if (!strcmp(a, "-n") && i + 1 < argc) count = strtoull(argv[++i], NULL, 0);
		else if (!strncmp(a, "--count=", 8)) count = strtoull(a + 8, NULL, 0);
		else if (!strcmp(a, "-s") && i + 1 < argc) seed = strtoull(argv[++i], NULL, 0);
		else if (!strncmp(a, "--seed=", 7)) seed = strtoull(a + 7, NULL, 0);
		else { fprintf(stderr, "fuzz: unknown option %s\n", a); usage(stderr); return 2; }
	}
	if (!seed)
		seed = 1;
	rng_state = seed;
	build_seeds();

	for (c = 0; c < count; c++) {
		unsigned char raw[BINFMT_HEAD_MAX];
		guard_buf g;
		binfmt_probe p;
		binfmt_diag d;
		binfmt_err rc;
		size_t n = mutate(raw, sizeof raw);

		if (guard_load(raw, n, &g) != 0) {
			printf("fuzz: cannot map a guarded buffer\n");
			return 1;
		}
		memset(&d, 0, sizeof d);
		rc = binfmt_classify(g.base, g.len, &p, &d);

		if (rc != binfmt_ok) {
			refused++;
			if (!d.field || !d.msg[0]) {
				guard_free(&g);
				return complain("a refusal named no field", raw, n);
			}
			if (p.kind != binfmt_unknown) {
				guard_free(&g);
				return complain("a refusal left a verdict behind", raw, n);
			}
		} else if (p.kind == binfmt_elf) {
			accepted_elf++;
			if (!elf_verdict_is_earned(g.base, g.len, &p)) {
				guard_free(&g);
				return complain("an elf verdict the head does not earn", raw, n);
			}
		} else if (p.kind == binfmt_script) {
			accepted_sh++;
			if (!interp_is_sane(&p)) {
				guard_free(&g);
				return complain("a script verdict with an unusable "
						"interpreter", raw, n);
			}
		} else if (p.kind == binfmt_host) {
			host++;
			if (g.len < 2 || g.base[0] != 'M' || g.base[1] != 'Z') {
				guard_free(&g);
				return complain("a host verdict without MZ", raw, n);
			}
		} else {
			unknown++;
		}
		guard_free(&g);

		/* Property 4, every sixteenth case: refill the fixture file
		 * system with fresh mutants and walk a chain through it. */
		if ((c & 15) == 0) {
			binfmt_resolved r;
			char *av[] = { "prog", "one", NULL };
			unsigned k;

			for (k = 0; k < FS_N; k++)
				fs_len[k] = mutate(fs_head[k], sizeof fs_head[k]);
			memset(&d, 0, sizeof d);
			rc = binfmt_resolve("/f0", av, fixture_head, NULL, &r, &d);
			chains++;
			if (rc == binfmt_ok) {
				if (r.depth > BINFMT_MAX_DEPTH || r.argc == 0 ||
				    r.argc >= BINFMT_ARGV_MAX || !r.file ||
				    r.store_used > sizeof r.store)
					return complain("a resolved chain broke its "
							"own bounds", fs_head[0], fs_len[0]);
				for (k = 0; k < r.argc; k++)
					if (!r.argv[k])
						return complain("a resolved vector has a "
								"null element",
								fs_head[0], fs_len[0]);
			} else if (!d.field || !d.msg[0]) {
				return complain("a chain refusal named no field",
						fs_head[0], fs_len[0]);
			}
		}
	}

	if (!quiet)
		printf("  %llu cases held: %llu elf, %llu script, %llu host, "
		       "%llu unknown, %llu refused; %llu chains walked\n",
		       count, accepted_elf, accepted_sh, host, unknown, refused, chains);
	return 0;
}
