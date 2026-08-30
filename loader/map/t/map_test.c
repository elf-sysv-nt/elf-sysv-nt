/* WP-32 mapping test.
 *
 * Maps a static ELF specimen through elf_map, then holds the result to the
 * done-when bar three ways that a query alone cannot: it reads the mapping
 * back out of /proc/self/maps to prove the runtime's own bookkeeping recorded
 * it (the property WP-41 depends on); it touches the pages to prove the
 * protections are real and not merely what was asked for (spike 2's rule); and
 * it enters the image to prove it runs and that .bss arrived zeroed. It also
 * runs the two controls that must be turned away: an object whose segments
 * share a granule, and a second placement over an occupied span.
 *
 * Usage: map_test [--expect-granule] [--expect-occupied] [--base HEX] ELF
 */
#define _GNU_SOURCE
#include "../elf_map.h"
#include "../../elf/elf_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>

#define KEY        0x9E3779B97F4A7C15ULL
#define RODATA     0xC0FFEE0FBADC0DE5ULL

struct hs { uint64_t in_magic, out_magic, out_rodata, out_bss, out_rip; };
extern void elf_enter(void *entry, void *handshake);

static int failures;
static void ck(const char *what, int ok)
{
	printf("    %-44s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

/* ---- fault probes ------------------------------------------------------ */
static sigjmp_buf jb;
static volatile int in_probe;
static void on_fault(int sig) { (void) sig; if (in_probe) siglongjmp(jb, 1); }

static int store_faults(volatile unsigned char *p)
{
	in_probe = 1;
	if (sigsetjmp(jb, 1) == 0) { *p = 0x55; in_probe = 0; return 0; }
	in_probe = 0; return 1;
}
static int load_faults(const volatile unsigned char *p)
{
	volatile unsigned char v;
	in_probe = 1;
	if (sigsetjmp(jb, 1) == 0) { v = *p; (void) v; in_probe = 0; return 0; }
	in_probe = 0; return 1;
}

/* ---- /proc/self/maps oracle ------------------------------------------- */

/* Fill perms[5] ("rwxp"-style) for the map line covering addr; return 1 if a
 * line covers it, 0 if the address is in no mapping at all. */
static int maps_perms_at(uint64_t addr, char perms[8])
{
	FILE *f = fopen("/proc/self/maps", "r");
	char line[512];
	int found = 0;
	if (!f) return 0;
	while (fgets(line, sizeof line, f)) {
		unsigned long a, b;
		char pp[8] = {0};
		if (sscanf(line, "%lx-%lx %7s", &a, &b, pp) == 3 &&
		    addr >= a && addr < b) {
			memcpy(perms, pp, 8);
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

/* Is any part of [lo,hi) present in /proc/self/maps? */
static int maps_any_in(uint64_t lo, uint64_t hi)
{
	FILE *f = fopen("/proc/self/maps", "r");
	char line[512];
	int any = 0;
	if (!f) return 0;
	while (fgets(line, sizeof line, f)) {
		unsigned long a, b;
		if (sscanf(line, "%lx-%lx", &a, &b) == 2 && b > lo && a < hi) {
			any = 1; break;
		}
	}
	fclose(f);
	return any;
}

static void expect_perms(const elf_map_seg *s)
{
	char perms[8] = {0};
	char want[4];
	char what[80];
	want[0] = (s->flags & 4) ? 'r' : '-';
	want[1] = (s->flags & 2) ? 'w' : '-';
	want[2] = (s->flags & 1) ? 'x' : '-';
	want[3] = 0;
	int got = maps_perms_at(s->vaddr, perms);
	snprintf(what, sizeof what, "maps show %s at 0x%llx as %s (got %.3s)",
	         want, (unsigned long long) s->vaddr, want, got ? perms : "none");
	ck(what, got && strncmp(perms, want, 3) == 0);
}

/* ---- file slurp -------------------------------------------------------- */
static unsigned char *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	long n;
	unsigned char *buf;
	if (!f) { fprintf(stderr, "map_test: cannot open %s\n", path); return NULL; }
	if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 0) { fclose(f); return NULL; }
	rewind(f);
	buf = malloc((size_t) n);
	if (!buf || fread(buf, 1, (size_t) n, f) != (size_t) n) {
		free(buf); fclose(f); return NULL;
	}
	fclose(f);
	*size = (size_t) n;
	return buf;
}

/* Find the segment that carries a given flag combination. */
static const elf_map_seg *seg_with(const elf_mapping *m, uint32_t want, uint32_t mask)
{
	unsigned i;
	for (i = 0; i < m->seg_count; i++)
		if ((m->seg[i].flags & mask) == want)
			return &m->seg[i];
	return NULL;
}

int main(int argc, char **argv)
{
	const char *path = NULL;
	uint64_t base = 0x10000000ULL;
	int expect_granule = 0, expect_occupied = 0, i;
	unsigned char *image;
	size_t size = 0;
	elf_parsed parsed;
	elf_diag pdiag;
	elf_mapping m;
	elf_map_diag d;
	elf_map_err rc;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--expect-granule")) expect_granule = 1;
		else if (!strcmp(argv[i], "--expect-occupied")) expect_occupied = 1;
		else if (!strcmp(argv[i], "--base") && i + 1 < argc)
			base = strtoull(argv[++i], NULL, 0);
		else path = argv[i];
	}
	if (!path) { fprintf(stderr, "map_test: need an ELF path\n"); return 2; }

	signal(SIGSEGV, on_fault);
	signal(SIGILL, on_fault);
	signal(SIGBUS, on_fault);

	if (!(image = slurp(path, &size))) return 2;
	if (elf_parse(image, size, &parsed, &pdiag) != elf_ok) {
		fprintf(stderr, "map_test: %s failed to parse: %s\n", path, pdiag.msg);
		return 2;
	}

	printf("== %s\n\n", path);

	/* Control 1: an object whose segments share a granule is refused. */
	if (expect_granule) {
		rc = elf_map(image, size, &parsed, base, &m, &d);
		printf("    refusal code %s\n", elf_map_err_name(rc));
		printf("    diag: %s\n", d.msg);
		ck("segments sharing a granule are refused", rc == elf_map_err_granule);
		goto done;
	}

	/* Good path: place the object. */
	rc = elf_map(image, size, &parsed, base, &m, &d);
	if (rc != elf_map_ok) {
		fprintf(stderr, "map_test: elf_map refused %s: %s\n",
		        elf_map_err_name(rc), d.msg);
		return 2;
	}

	printf("    reserved 0x%llx bytes at 0x%llx, bias 0x%llx, entry 0x%llx\n",
	       (unsigned long long) m.size, (unsigned long long) m.base,
	       (unsigned long long) m.load_bias, (unsigned long long) m.entry);
	printf("    page 0x%llx granule 0x%llx  gnu_stack=%d exec=%d\n\n",
	       (unsigned long long) m.page_size, (unsigned long long) m.granule,
	       m.has_gnu_stack, m.stack_exec);

	/* Control 2: a second placement over the now-occupied span is refused. */
	if (expect_occupied) {
		elf_mapping m2;
		elf_map_diag d2;
		elf_map_err rc2 = elf_map(image, size, &parsed, base, &m2, &d2);
		printf("    second placement code %s\n", elf_map_err_name(rc2));
		printf("    diag: %s\n", d2.msg);
		ck("second placement over occupied span is refused",
		   rc2 == elf_map_err_reserve);
		if (rc2 == elf_map_ok) elf_unmap(&m2);
		elf_unmap(&m);
		goto done;
	}

	/* The runtime's own bookkeeping recorded every segment. */
	for (i = 0; i < (int) m.seg_count; i++)
		expect_perms(&m.seg[i]);

	/* The protections are real, not just reported. */
	{
		const elf_map_seg *text = seg_with(&m, 1, 1);        /* PF_X set */
		const elf_map_seg *rw   = seg_with(&m, 2, 2);        /* PF_W set */
		if (text)
			ck("a store into an executable segment faults",
			   store_faults((volatile unsigned char *)(uintptr_t) text->vaddr));
		if (rw) {
			/* Save and restore: the writable segment overlaps .bss, which
			 * the run below reads before writing, so the probe must leave
			 * it as it found it. */
			volatile unsigned char *w = (volatile unsigned char *)(uintptr_t) rw->vaddr;
			unsigned char saved = *w;
			ck("a store into the writable segment lands", !store_faults(w));
			*w = saved;
			/* the byte past filesz is committed zero (.bss is free) */
			ck("the byte past filesz reads zero",
			   !load_faults((const volatile unsigned char *)(uintptr_t)(rw->vaddr + rw->filesz)) &&
			   *((volatile unsigned char *)(uintptr_t)(rw->vaddr + rw->filesz)) == 0);
		}
	}

	/* Enter the image and read back its report. */
	{
		struct hs h;
		const elf_map_seg *text = seg_with(&m, 1, 1);
		memset(&h, 0, sizeof h);
		h.in_magic = 0x0123456789ABCDEFULL;
		elf_enter((void *)(uintptr_t) m.entry, &h);
		ck("the image ran and reached its handshake",
		   h.out_magic == (0x0123456789ABCDEFULL ^ KEY));
		ck("it read the correct word from the read-only segment",
		   h.out_rodata == RODATA);
		ck("the .bss it read before writing was zero", h.out_bss == 0);
		ck("it ran inside its own text segment",
		   text && h.out_rip >= text->vaddr && h.out_rip < text->vaddr + text->memsz);
	}

	/* The relro hook is callable and idempotent. This object carries no
	 * relro, so it is a no-op that must still report success. */
	{
		elf_map_diag rd;
		ck("the relro hook succeeds", elf_map_protect_relro(&m, &rd) == elf_map_ok);
		ck("the relro hook is idempotent",
		   elf_map_protect_relro(&m, &rd) == elf_map_ok);
	}

	/* Release, and confirm the bookkeeping no longer shows the span. */
	{
		uint64_t b = m.base, s = m.size;
		elf_unmap(&m);
		ck("after unmap the span is gone from the bookkeeping",
		   !maps_any_in(b, b + s));
	}

done:
	printf("\ncase_failures=%d\ncase_result=%s\n", failures,
	       failures ? "fail" : "pass");
	free(image);
	return failures ? 1 : 0;
}
