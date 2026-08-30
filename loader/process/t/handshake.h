/* The block the entry specimen and the host driver report through. It is
 * shared so the two sides -- one cross-compiled to the System V ELF world, one
 * host code on the Microsoft ABI -- agree on the layout byte for byte.
 *
 * The specimen is entered on the real psABI stack WP-40 built: %rsp on argc,
 * 16-byte aligned, %rdx carrying the atexit handler. It has no argument
 * registers to find this block through, so it locates it through an
 * environment variable the driver set -- which keeps the auxv the builder
 * produced exactly the production set, with nothing injected for the test, and
 * makes finding the block itself a proof that envp was laid correctly.
 * host_rsp is the pointer the trampoline parked for the specimen to return on,
 * since there is no exit to call here. */
#ifndef ELFSYSV_WP40_HANDSHAKE_H
#define ELFSYSV_WP40_HANDSHAKE_H

#include <stdint.h>

/* The environment variable whose value is the decimal address of the
 * handshake block. The specimen scans envp for this prefix. */
#define PROC_HS_ENV "WP40_HS="

#define PROC_IN_MAGIC  0x0123456789ABCDEFULL
#define PROC_MAGIC_KEY 0x9E3779B97F4A7C15ULL

struct proc_hs {
	/* set by the driver before entry */
	uint64_t in_magic;
	uint64_t host_rsp;        /* the specimen restores this and rets */
	uint64_t expect_atexit;   /* what %rdx should have carried */

	/* filled by the specimen off the stack it was entered on */
	uint64_t out_magic;       /* in_magic ^ KEY: it ran and found the block */
	uint64_t out_sp;          /* the %rsp it was entered on */
	uint64_t out_sp_misalign; /* out_sp & 15: must be zero */
	uint64_t out_rdx;         /* the %rdx it was entered with */
	uint64_t out_rip;         /* an %rip inside its own text */

	uint64_t out_argc;
	uint64_t out_argv0_word;  /* first 8 bytes of argv[0] */
	uint64_t out_argv_last_null; /* argv[argc] as an integer: must be 0 */
	uint64_t out_env0_word;   /* first 8 bytes of envp[0], 0 if no env */
	uint64_t out_envc;        /* envp entries counted to its NULL */

	/* values read back out of the auxv */
	uint64_t out_at_pagesz;
	uint64_t out_at_phdr;
	uint64_t out_at_phent;
	uint64_t out_at_phnum;
	uint64_t out_at_entry;
	uint64_t out_at_random_word; /* first 8 of the AT_RANDOM bytes */
	uint64_t out_at_execfn_word; /* first 8 bytes of the AT_EXECFN string */
	uint64_t out_at_platform_word; /* first 8 bytes of the AT_PLATFORM string */
	uint64_t out_saw_sysinfo_ehdr; /* 1 if AT_SYSINFO_EHDR was present */
	uint64_t out_auxc;        /* auxv entries counted to AT_NULL */
};

#endif /* ELFSYSV_WP40_HANDSHAKE_H */
