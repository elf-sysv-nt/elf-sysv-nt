/* WP-40 image test.
 *
 * Drives the initial process image end to end: it parses a static ELF (WP-31),
 * maps it (WP-32), builds the psABI stack over it (WP-40), asserts the entry
 * contract the builder promises, enters the image on that stack through the
 * trampoline, and reads back -- off the specimen's own walk of the stack it was
 * handed -- that argc, argv[0], envp, and the auxv all arrived where a real
 * program's _start would look for them.
 *
 * It then prints the auxv the builder produced, in a normalized form, so the
 * differential harness can hold it field for field against an auxv a real
 * Linux kernel builds. The two must differ only in the entries that describe
 * the platform, and AT_SYSINFO_EHDR must be absent without the specimen having
 * faulted on its absence.
 *
 * Usage: image_test [--dump-auxv] ELF
 */
#define _GNU_SOURCE
#include "../process_image.h"
#include "../../elf/elf_parse.h"
#include "../../map/elf_map.h"
#include "handshake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern void proc_enter(void *entry, void *sp, uint64_t rdx, void *handshake);
extern unsigned proc_abi_probe(void *entry, void *sp, uint64_t rdx,
                               void *handshake);

/* A stack for the image, aligned to a page. Static so its address is stable
 * and its lifetime spans the entry. */
static char g_stack[512 * 1024] __attribute__((aligned(4096)));

/* Deterministic AT_RANDOM bytes, so the readback is checkable. */
static const unsigned char g_random[16] = {
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
};

/* The %rdx sentinel: a nonzero atexit-handler stand-in the entry must carry.
 * The kernel passes 0 for a static binary and the dynamic linker fills it; a
 * nonzero value here proves the register is plumbed, and the specimen never
 * dereferences it. */
#define ATEXIT_SENTINEL 0x0000123456ABCDEFULL

static int failures;
static void ck(const char *what, int ok)
{
	printf("    %-46s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

static unsigned char *slurp(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	long n;
	unsigned char *buf;
	if (!f) { fprintf(stderr, "image_test: cannot open %s\n", path); return NULL; }
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

static uint64_t leword(const unsigned char *p, int n)
{
	uint64_t v = 0;
	int i;
	for (i = 0; i < n && p[i]; i++)
		v |= (uint64_t) p[i] << (8 * i);
	return v;
}

static const elf_map_seg *text_seg(const elf_mapping *m)
{
	unsigned i;
	for (i = 0; i < m->seg_count; i++)
		if (m->seg[i].flags & 1)   /* PF_X */
			return &m->seg[i];
	return NULL;
}

int main(int argc, char **argv)
{
	const char *path = NULL;
	int dump = 0, i;
	unsigned char *image;
	size_t size = 0;
	elf_parsed parsed;
	elf_diag pdiag;
	elf_mapping m;
	elf_map_diag mdiag;
	proc_image_params pr;
	proc_layout lay;
	proc_diag pd;
	static struct proc_hs hs;
	char hs_env[64];
	const elf_map_seg *text;
	unsigned probe;

	/* argv/envp fixtures for the image. envp[0] carries the handshake
	 * address so the specimen can find it by walking envp. */
	char *img_argv[4];
	char *img_envp[4];
	static const char *arg0 = "/opt/app/bin/hello";

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--dump-auxv")) dump = 1;
		else path = argv[i];
	}
	if (!path) { fprintf(stderr, "image_test: need an ELF path\n"); return 2; }

	if (!(image = slurp(path, &size))) return 2;
	if (elf_parse(image, size, &parsed, &pdiag) != elf_ok) {
		fprintf(stderr, "image_test: %s failed to parse: %s\n", path, pdiag.msg);
		return 2;
	}
	if (elf_map(image, size, &parsed, 0x10000000ULL, &m, &mdiag) != elf_map_ok) {
		fprintf(stderr, "image_test: elf_map refused: %s\n", mdiag.msg);
		return 2;
	}

	snprintf(hs_env, sizeof hs_env, PROC_HS_ENV "%llu",
	         (unsigned long long)(uintptr_t) &hs);
	img_argv[0] = (char *) arg0;
	img_argv[1] = (char *) "arg-one";
	img_argv[2] = (char *) "arg-two";
	img_argv[3] = NULL;
	img_envp[0] = hs_env;
	img_envp[1] = (char *) "PATH=/usr/bin";
	img_envp[2] = (char *) "LANG=C";
	img_envp[3] = NULL;

	memset(&pr, 0, sizeof pr);
	pr.page_size = 4096;
	pr.base      = 0;                 /* no interpreter for this specimen */
	pr.flags     = 0;
	pr.hwcap     = 0x178bfbffULL;     /* a representative x86-64 el8 value */
	pr.hwcap2    = 0x2ULL;
	pr.clktck    = 100;
	pr.uid = pr.euid = 1000;
	pr.gid = pr.egid = 1000;
	pr.secure    = 0;
	pr.random16  = g_random;
	pr.platform  = "x86_64";
	pr.execfn    = arg0;

	memset(&hs, 0, sizeof hs);
	hs.in_magic = PROC_IN_MAGIC;
	hs.expect_atexit = ATEXIT_SENTINEL;

	if (proc_build_stack(&m, &parsed, img_argv, img_envp, &pr,
	                     ATEXIT_SENTINEL, g_stack, g_stack + sizeof g_stack,
	                     &lay, &pd) != proc_ok) {
		fprintf(stderr, "image_test: proc_build_stack failed: %s\n", pd.msg);
		elf_unmap(&m);
		return 2;
	}

	printf("== %s\n\n", path);
	printf("    entry 0x%llx  sp 0x%llx  auxv %u entries  rdx 0x%llx\n\n",
	       (unsigned long long) lay.at_entry, (unsigned long long) lay.sp,
	       lay.auxv_count, (unsigned long long) lay.rdx);

	/* --- the entry contract the builder promises --------------------- */
	ck("the entry %rsp is 16-byte aligned", (lay.sp & 15) == 0);
	ck("%rsp points at argc", *(uint64_t *)(uintptr_t) lay.sp == lay.argc);
	ck("the built stack stays inside its buffer",
	   lay.used_lo >= (uint64_t)(uintptr_t) g_stack &&
	   lay.used_hi <= (uint64_t)(uintptr_t)(g_stack + sizeof g_stack));
	ck("AT_PHDR is nonzero and inside the image",
	   lay.at_phdr >= m.base && lay.at_phdr < m.base + m.size);

	/* --- enter and read back ----------------------------------------- */
	hs.host_rsp = 0;
	probe = proc_abi_probe((void *)(uintptr_t) lay.at_entry,
	                       (void *)(uintptr_t) lay.sp, lay.rdx, &hs);

	ck("the image ran and found its handshake",
	   hs.out_magic == (PROC_IN_MAGIC ^ PROC_MAGIC_KEY));
	ck("the %rsp it was entered on is 16-byte aligned", hs.out_sp_misalign == 0);
	ck("the %rsp it saw is the one we built", hs.out_sp == lay.sp);
	ck("%rdx carried the atexit handler", hs.out_rdx == ATEXIT_SENTINEL);
	ck("it read argc back", hs.out_argc == lay.argc && hs.out_argc == 3);
	ck("it read argv[0] back", hs.out_argv0_word == leword((const unsigned char *) arg0, 8));
	ck("argv[argc] is the NULL terminator", hs.out_argv_last_null == 0);
	ck("it read envp back to its terminator", hs.out_envc == 3);
	ck("it read envp[0]", hs.out_env0_word == leword((const unsigned char *) img_envp[0], 8));
	ck("auxv AT_PAGESZ read back as 4096", hs.out_at_pagesz == 4096);
	ck("auxv AT_PHDR matches what we built", hs.out_at_phdr == lay.at_phdr);
	ck("auxv AT_PHENT read back as 56", hs.out_at_phent == 56);
	ck("auxv AT_PHNUM matches what we built", hs.out_at_phnum == lay.at_phnum);
	ck("auxv AT_ENTRY matches the mapping", hs.out_at_entry == m.entry);
	ck("auxv AT_RANDOM points at the 16 bytes",
	   hs.out_at_random_word == leword(g_random, 8));
	ck("auxv AT_EXECFN points at the path",
	   hs.out_at_execfn_word == leword((const unsigned char *) arg0, 8));
	ck("auxv AT_PLATFORM points at \"x86_64\"",
	   hs.out_at_platform_word == leword((const unsigned char *) "x86_64", 8));
	ck("AT_SYSINFO_EHDR is absent and not fatal to the consumer",
	   hs.out_saw_sysinfo_ehdr == 0);
	text = text_seg(&m);
	ck("it ran inside its own text segment",
	   text && hs.out_rip >= text->vaddr && hs.out_rip < text->vaddr + text->memsz);
	ck("the crossing preserved the callee-saved registers", probe == 0);

	/* --- the auxv we built, for the differential --------------------- */
	if (dump) {
		uint64_t *a = (uint64_t *)(uintptr_t) lay.auxv_ptr;
		printf("\n== auxv built by WP-40\n\n");
		for (;; a += 2) {
			printf("    %-16s 0x%llx\n", proc_auxv_name(a[0]),
			       (unsigned long long) a[1]);
			printf("builtkey=%s\n", proc_auxv_name(a[0]));
			if (a[0] == AT_NULL) break;
		}
	}

	elf_unmap(&m);
	printf("\ncase_failures=%d\ncase_result=%s\n", failures,
	       failures ? "fail" : "pass");
	free(image);
	return failures ? 1 : 0;
}
