/* WP-40 entry specimen: a static ELF entered on the real psABI stack.
 *
 * Built by the cross toolchain into an ET_EXEC with no libc, it is what a
 * program's _start sees before glibc's runs: %rsp on argc, 16-byte aligned,
 * %rdx carrying the shared-object atexit handler or zero, and everything else
 * -- argv, envp, the auxv -- reachable only by walking up from %rsp. It reads
 * all of that back into a handshake block and returns, so the driver can hold
 * what WP-40 built against what the stack actually carried.
 *
 * It finds the handshake block through a private auxv type the driver appended
 * (there are no argument registers on the psABI entry to hand it through), and
 * it returns by restoring the pointer the trampoline parked -- there is no
 * exit to call, because there is no kernel here.
 *
 * There are no system calls and no relocations. Do not add either; this must
 * exercise WP-40's stack and nothing below it.
 */
#include <stdint.h>

#include "handshake.h"

#define AT_NULL         0
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_ENTRY        9
#define AT_RANDOM       25
#define AT_EXECFN       31
#define AT_PLATFORM     15
#define AT_SYSINFO_EHDR 33

/* Match s against a prefix; return the char after the prefix, or 0. */
static const char *after_prefix(const char *s, const char *pfx)
{
	while (*pfx) {
		if (*s != *pfx)
			return 0;
		s++; pfx++;
	}
	return s;
}

/* Parse an unsigned decimal, no sign, no whitespace. */
static uint64_t parse_u64(const char *s)
{
	uint64_t v = 0;
	while (*s >= '0' && *s <= '9') {
		v = v * 10u + (uint64_t)(*s - '0');
		s++;
	}
	return v;
}

/* Read the first 8 bytes at p as a little-endian word, tolerating a string
 * shorter than 8 bytes by stopping at the NUL and zero-filling the rest. */
static uint64_t word_at(const char *p)
{
	uint64_t v = 0;
	int i;
	if (!p)
		return 0;
	for (i = 0; i < 8; i++) {
		unsigned char c = (unsigned char) p[i];
		v |= (uint64_t) c << (8 * i);
		if (c == 0)
			break;
	}
	return v;
}

/* Entered from _start with sp pointing at argc and rdx the entry %rdx. Returns
 * the parked host stack pointer for _start to restore. */
uint64_t specimen_main(uint64_t *sp, uint64_t rdx)
{
	uint64_t argc = sp[0];
	char **argv = (char **) &sp[1];
	char **envp = argv + argc + 1;
	uint64_t *auxv;
	struct proc_hs *h = 0;
	uint64_t envc = 0, auxc = 0;
	uint64_t rip;
	int saw_sysinfo = 0;
	char **e;

	__asm__ volatile ("leaq 0(%%rip), %0" : "=r"(rip));

	for (e = envp; *e; e++) {
		const char *val = after_prefix(*e, PROC_HS_ENV);
		if (val)
			h = (struct proc_hs *)(uintptr_t) parse_u64(val);
		envc++;
	}
	auxv = (uint64_t *)(e + 1);

	if (!h)
		return 0; /* the driver treats a zero as a lost handshake */

	h->out_magic = h->in_magic ^ PROC_MAGIC_KEY;
	h->out_sp = (uint64_t) sp;
	h->out_sp_misalign = (uint64_t) sp & 15u;
	h->out_rdx = rdx;
	h->out_rip = rip;

	h->out_argc = argc;
	h->out_argv0_word = argc ? word_at(argv[0]) : 0;
	h->out_argv_last_null = (uint64_t) argv[argc];
	h->out_envc = envc;
	h->out_env0_word = envc ? word_at(envp[0]) : 0;

	/* Second pass: read the auxv values back. */
	{
		uint64_t *a;
		for (a = auxv; a[0] != AT_NULL; a += 2) {
			auxc++;
			switch (a[0]) {
			case AT_PAGESZ: h->out_at_pagesz = a[1]; break;
			case AT_PHDR:   h->out_at_phdr = a[1]; break;
			case AT_PHENT:  h->out_at_phent = a[1]; break;
			case AT_PHNUM:  h->out_at_phnum = a[1]; break;
			case AT_ENTRY:  h->out_at_entry = a[1]; break;
			case AT_RANDOM:
				h->out_at_random_word = *(uint64_t *) a[1];
				break;
			case AT_EXECFN:
				h->out_at_execfn_word = word_at((char *) a[1]);
				break;
			case AT_PLATFORM:
				h->out_at_platform_word = word_at((char *) a[1]);
				break;
			case AT_SYSINFO_EHDR: saw_sysinfo = 1; break;
			default: break;
			}
		}
	}
	h->out_auxc = auxc + 1; /* count the AT_NULL terminator too */
	h->out_saw_sysinfo_ehdr = (uint64_t) saw_sysinfo;

	return h->host_rsp;
}

/* The real entry. On the psABI there are no argument registers: argc is at
 * (%rsp) and %rsp is 16-byte aligned, so a direct call into specimen_main is
 * correctly aligned (the call's own push leaves %rsp+8 a multiple of 16 at the
 * callee, which is exactly the ABI's requirement). %rdx is the atexit handler.
 * The stack below argc is free, so the C frame has room to grow. */
__asm__(
	".text\n"
	".globl _start\n"
	".type _start,@function\n"
	"_start:\n"
	"	movq	%rsp, %rdi\n"   /* arg0: pointer to argc */
	"	movq	%rdx, %rsi\n"   /* arg1: the entry %rdx (atexit) */
	"	call	specimen_main\n"
	/* Poison every register the Microsoft ABI calls callee-saved before
	 * handing control back, so the driver's proc_abi_probe measures the
	 * trampoline's save and restore rather than trusting it. %rax holds the
	 * parked host stack and is left alone. */
	"	movq	$0xDEAD01, %rbx\n"
	"	movq	$0xDEAD02, %rbp\n"
	"	movq	$0xDEAD03, %rdi\n"
	"	movq	$0xDEAD04, %rsi\n"
	"	movq	$0xDEAD05, %r12\n"
	"	movq	$0xDEAD06, %r13\n"
	"	movq	$0xDEAD07, %r14\n"
	"	movq	$0xDEAD08, %r15\n"
	"	movq	%rax, %rsp\n"   /* restore the parked host stack */
	"	ret\n"
);
