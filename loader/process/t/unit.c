/* WP-40 unit tests: the builder as pure layout.
 *
 * These do not map or enter anything. They fabricate the small parts of an
 * elf_parsed and elf_mapping the builder reads -- the phdr location, the entry,
 * the load segment -- and check the layout invariants directly across a range
 * of argc and envc: that the entry %rsp is always 16-byte aligned, that argc
 * and the two array terminators land where a program expects them, that the
 * auxv is terminated, and that the refusals fire. The alignment is the subtle
 * one, since it depends on the parity of the word count, so it is swept rather
 * than sampled.
 */
#include "../process_image.h"
#include "../../elf/elf_parse.h"
#include "../../map/elf_map.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int failures;
static void ck(const char *what, int ok)
{
	if (!ok) { printf("    %-52s FAILED\n", what); failures++; }
}

static unsigned char g_rand[16] = {0};
static char g_buf[128 * 1024] __attribute__((aligned(4096)));

/* A minimal image the builder can read: one PT_LOAD at 0x400000 covering the
 * phdrs at file offset 0x40, entry at 0x401000, no bias. */
static void fabricate(elf_parsed *p, elf_mapping *m)
{
	memset(p, 0, sizeof *p);
	memset(m, 0, sizeof *m);
	p->e_type = 2;            /* ET_EXEC */
	p->e_machine = 62;
	p->e_entry = 0x401000;
	p->phoff = 0x40;
	p->phnum = 6;
	p->phentsize = 56;
	p->load_count = 1;
	p->load[0].off = 0;
	p->load[0].vaddr = 0x400000;
	p->load[0].filesz = 0x1000;
	p->load[0].memsz = 0x1000;
	p->load[0].flags = 5;     /* R+X */
	m->base = 0x400000;
	m->size = 0x3000;
	m->load_bias = 0;
	m->entry = 0x401000;
	m->page_size = 0x1000;
	m->granule = 0x10000;
}

static void params(proc_image_params *pr)
{
	memset(pr, 0, sizeof *pr);
	pr->page_size = 4096;
	pr->clktck = 100;
	pr->uid = pr->euid = pr->gid = pr->egid = 1000;
	pr->hwcap = 0x178bfbff;
	pr->hwcap2 = 2;
	pr->random16 = g_rand;
	pr->platform = "x86_64";
	pr->execfn = "/bin/prog";
}

/* Build with a given argc and envc of short strings, and check the invariants
 * that do not depend on entry. */
static void sweep_one(unsigned na, unsigned ne)
{
	elf_parsed p;
	elf_mapping m;
	proc_image_params pr;
	proc_layout lay;
	proc_diag d;
	char *av[16], *ev[16];
	unsigned i;
	char what[80];

	fabricate(&p, &m);
	params(&pr);
	for (i = 0; i < na; i++) av[i] = (char *) "aa";
	av[na] = NULL;
	for (i = 0; i < ne; i++) ev[i] = (char *) "e=1";
	ev[ne] = NULL;

	if (proc_build_stack(&m, &p, av, ev, &pr, 0,
	                     g_buf, g_buf + sizeof g_buf, &lay, &d) != proc_ok) {
		snprintf(what, sizeof what, "build argc=%u envc=%u", na, ne);
		ck(what, 0);
		return;
	}
	snprintf(what, sizeof what, "argc=%u envc=%u: %%rsp is 16-byte aligned", na, ne);
	ck(what, (lay.sp & 15) == 0);
	snprintf(what, sizeof what, "argc=%u envc=%u: (%%rsp) holds argc", na, ne);
	ck(what, *(uint64_t *)(uintptr_t) lay.sp == na);
	snprintf(what, sizeof what, "argc=%u envc=%u: argv terminator is NULL", na, ne);
	ck(what, ((uint64_t *)(uintptr_t) lay.argv_ptr)[na] == 0);
	snprintf(what, sizeof what, "argc=%u envc=%u: envp terminator is NULL", na, ne);
	ck(what, ((uint64_t *)(uintptr_t) lay.envp_ptr)[ne] == 0);
	snprintf(what, sizeof what, "argc=%u envc=%u: AT_PHDR is 0x400040", na, ne);
	ck(what, lay.at_phdr == 0x400040);
	snprintf(what, sizeof what, "argc=%u envc=%u: auxv ends at AT_NULL", na, ne);
	ck(what, ((uint64_t *)(uintptr_t) lay.auxv_ptr)[2 * (lay.auxv_count - 1)] == AT_NULL);
}

int main(void)
{
	elf_parsed p;
	elf_mapping m;
	proc_image_params pr;
	proc_layout lay;
	proc_diag d;
	char *av[2], *ev[1];
	unsigned na, ne;

	printf("== WP-40 builder unit tests\n\n");

	/* Alignment must hold for every parity of the vector word count. */
	for (na = 0; na <= 6; na++)
		for (ne = 0; ne <= 6; ne++)
			sweep_one(na, ne);

	/* A buffer too small is refused, not overrun. */
	fabricate(&p, &m);
	params(&pr);
	av[0] = (char *) "x"; av[1] = NULL; ev[0] = NULL;
	ck("a stack buffer too small is refused with proc_err_space",
	   proc_build_stack(&m, &p, av, ev, &pr, 0, g_buf, g_buf + 64, &lay, &d)
	   == proc_err_space);

	/* A null argument is refused. */
	ck("a null argv is refused with proc_err_arg",
	   proc_build_stack(&m, &p, NULL, ev, &pr, 0, g_buf, g_buf + sizeof g_buf,
	                    &lay, &d) == proc_err_arg);

	/* Phdrs outside every loaded segment cannot be reported. */
	fabricate(&p, &m);
	p.phoff = 0x9000;   /* past the one segment's file range */
	ck("phdrs in no loaded segment are refused with proc_err_image",
	   proc_build_stack(&m, &p, av, ev, &pr, 0, g_buf, g_buf + sizeof g_buf,
	                    &lay, &d) == proc_err_image);

	printf("\ncase_failures=%d\ncase_result=%s\n", failures,
	       failures ? "fail" : "pass");
	return failures ? 1 : 0;
}
