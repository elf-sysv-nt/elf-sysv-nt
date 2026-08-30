/* corewrite_test.c -- drive the writer without a crashing process.
 *
 * Fabricates a small process image with planted, distinctive values and
 * writes core.elf to the current directory. accept.sh then reads the file
 * back with the WP-60 gdb and the cross readelf and checks every planted
 * value: the registers out of NT_PRSTATUS, the word at the interrupted
 * stack pointer out of the PT_LOAD, the mapping list out of NT_FILE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../elfcore.h"

#define TEXT_VA 0x400000ull
#define STACK_VA 0x7ffff0000000ull
#define RIP_VA (TEXT_VA + 0x123)
#define RSP_VA (STACK_VA + 0x1000)
#define STACK_WORD 0xdeadbeefcafef00dull

static long file_sink(void *cookie, const void *buf, size_t n)
{
	return (long)fwrite(buf, 1, n, (FILE *)cookie);
}

int main(void)
{
	static unsigned char text[4096], stack[8192];
	elfsysv_sigctx_t ctx;
	elfcore_proc_t proc;
	elfcore_seg_t segs[2];
	uint64_t auxv[4] = { 6, 4096, 0, 0 };	/* AT_PAGESZ, AT_NULL */
	uint64_t word = STACK_WORD;
	FILE *f;
	int rc;

	memset(text, 0xcc, sizeof text);
	memset(stack, 0, sizeof stack);
	memcpy(stack + (RSP_VA - STACK_VA), &word, 8);

	memset(&ctx, 0, sizeof ctx);
	ctx.rax = 0x1111111111111111ull;
	ctx.rbx = 0x2222222222222222ull;
	ctx.r15 = 0x1515151515151515ull;
	ctx.rbp = RSP_VA + 0x40;
	ctx.rsp = RSP_VA;
	ctx.rip = RIP_VA;
	ctx.rflags = 0x246;
	ctx.cs = 0x33;
	ctx.ss = 0x2b;

	memset(&proc, 0, sizeof proc);
	proc.signo = 11;
	proc.pid = 4242;
	proc.fname = "crashme";
	proc.psargs = "crashme --hard";
	proc.ctx = &ctx;
	proc.auxv = auxv;
	proc.auxv_len = sizeof auxv;

	segs[0].vaddr = TEXT_VA;
	segs[0].len = sizeof text;
	segs[0].bytes = text;
	segs[0].prot = ELFCORE_R | ELFCORE_X;
	segs[0].path = "/fake/bin/crashme";
	segs[0].file_off = 0;

	segs[1].vaddr = STACK_VA;
	segs[1].len = sizeof stack;
	segs[1].bytes = stack;
	segs[1].prot = ELFCORE_R | ELFCORE_W;
	segs[1].path = NULL;
	segs[1].file_off = 0;

	/* Refusals first: no segments, a segment with no bytes. */
	if (elfcore_write(file_sink, NULL, &proc, segs, 0) != -2)
		return puts("FAIL: accepted zero segments"), 1;
	segs[1].bytes = NULL;
	if (elfcore_write(file_sink, NULL, &proc, segs, 2) != -2)
		return puts("FAIL: accepted a byteless segment"), 1;
	segs[1].bytes = stack;

	f = fopen("core.elf", "wb");
	if (!f)
		return puts("FAIL: cannot open core.elf"), 1;
	rc = elfcore_write(file_sink, f, &proc, segs, 2);
	if (fclose(f) || rc)
		return printf("FAIL: elfcore_write rc=%d\n", rc), 1;
	puts("OK: wrote core.elf");
	return 0;
}
