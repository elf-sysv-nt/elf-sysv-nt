/*
 * exec.c -- the initial stack, laid out the way the psABI's process-startup
 * section draws it and the way _start reads it back.
 *
 * From the top down: the argument and environment strings, sixteen bytes for
 * AT_RANDOM, then the pointer vector -- argc, the argv pointers and their null,
 * the (empty) envp and its null, and the auxiliary vector.  The vector's base,
 * where argc sits and where %rsp points at entry, is brought to a sixteen-byte
 * boundary with a pad word when the count is odd, so the stack meets the psABI's
 * alignment at _start.  The whole image is written to the user stack through
 * user_copy_out; the thread then enters at e_entry with %rsp there.
 *
 * The gate travels in AT_SYSINFO.  That is the one auxv entry that is ours
 * rather than the kernel's convention, and it is what lets a program with no
 * `syscall` instruction reach the syscall table at all.
 */
#include <stdlib.h>
#include <string.h>

#include "exec.h"

#define STK_SIZE	0x4000u		/* the 16 KB user stack */

/* Auxiliary vector types, from the target elf.h. */
#define AT_NULL		0
#define AT_PHDR		3
#define AT_PHENT	4
#define AT_PHNUM	5
#define AT_PAGESZ	6
#define AT_ENTRY	9
#define AT_RANDOM	25
#define AT_SYSINFO	32

/* The page size the auxv advertises; the target's PAGE, not the host's. */
#define PAGE_SIZE_AUX	4096

int exec_enter(struct substrate *s, const struct load_info *li,
	       uint64_t gate_addr, int argc, char **argv, int tid)
{
	struct sub_backing stack = { SUB_BACKING_STACK, NULL, 0 };
	void *ubase = NULL;
	unsigned char *hb;
	uint64_t ub, argc_va, rnd_va, fault = 0;
	uint64_t *argv_va;
	size_t sp, o;
	int i, envc = 0, naux = 8, words;
	struct sub_regs ctx;
	static const unsigned char rnd[16] = {
		0x9e, 0x37, 0x79, 0xb9, 0x7f, 0x4a, 0x7c, 0x15,
		0xf3, 0x9c, 0xc0, 0x60, 0x5c, 0xed, 0xc8, 0x34,
	};

	if (s->as_map(s, &ubase, STK_SIZE, &stack, 0, SUB_PROT_READ |
		      SUB_PROT_WRITE) != 0)
		return -1;
	ub = (uint64_t)(uintptr_t)ubase;

	hb = calloc(1, STK_SIZE);
	argv_va = calloc((size_t)argc, sizeof *argv_va);
	if (!hb || !argv_va) {
		free(hb);
		free(argv_va);
		return -1;
	}
	sp = STK_SIZE;

	/* Strings first, at the very top, each recorded by its user address. */
	for (i = 0; i < argc; i++) {
		size_t n = strlen(argv[i]) + 1;

		sp -= n;
		memcpy(hb + sp, argv[i], n);
		argv_va[i] = ub + sp;
	}
	sp -= sizeof rnd;
	memcpy(hb + sp, rnd, sizeof rnd);
	rnd_va = ub + sp;

	sp &= ~(size_t)15;		/* strings done; align the vector base */

	/* argc + argv[] + NULL + envp[] + NULL + 2 words per auxv entry. */
	words = 1 + (argc + 1) + (envc + 1) + 2 * naux;
	if (words & 1)
		sp -= 8;		/* pad so argc lands 16-aligned */
	sp -= (size_t)words * 8;
	argc_va = ub + sp;

	o = sp;
#define PUT(v)  do { uint64_t _v = (v); memcpy(hb + o, &_v, 8); o += 8; } while (0)
	PUT((uint64_t)argc);
	for (i = 0; i < argc; i++)
		PUT(argv_va[i]);
	PUT(0);				/* argv terminator */
	PUT(0);				/* envp terminator (no entries) */
	PUT(AT_PHDR);   PUT(li->phdr_va);
	PUT(AT_PHENT);  PUT(li->phent);
	PUT(AT_PHNUM);  PUT(li->phnum);
	PUT(AT_PAGESZ); PUT(PAGE_SIZE_AUX);
	PUT(AT_ENTRY);  PUT(li->entry);
	PUT(AT_RANDOM); PUT(rnd_va);
	PUT(AT_SYSINFO); PUT(gate_addr);
	PUT(AT_NULL);   PUT(0);
#undef PUT

	/* Push the populated span -- from argc up to the top -- to the user
	 * stack.  What lies below argc stays the zero the stack backing gives,
	 * and is where thread_start plants its return slot. */
	if (s->user_copy_out(s, argc_va, hb + sp, STK_SIZE - sp, &fault) != 0) {
		free(hb);
		free(argv_va);
		return -1;
	}
	free(hb);
	free(argv_va);

	memset(&ctx, 0, sizeof ctx);
	ctx.rip = li->entry;
	ctx.rsp = argc_va;		/* _start steps past the return slot */
	return s->thread_start(s, tid, &ctx, 0);
}
