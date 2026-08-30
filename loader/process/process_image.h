/* WP-40: the initial process image.
 *
 * proc_build_stack() lays out the stack a System V AMD64 program is entered
 * on: the block the kernel builds in fs/binfmt_elf.c and hands to _start with
 * %rsp pointing at argc. There is no kernel here, so the runtime builds it,
 * and the shape has to be the kernel's exactly or a glibc that reads it back
 * with pointer arithmetic tuned to the kernel's layout will read the wrong
 * words. What we may honestly differ in is the platform the auxv describes,
 * not the mold it is poured into.
 *
 * The layout, from high address down to the entry %rsp:
 *
 *   [top]   arg strings, env strings, the AT_PLATFORM string,
 *           the AT_EXECFN string, and the 16 AT_RANDOM bytes
 *           -- the pointer targets, placed first so the vector below
 *           can point up into them
 *   ...     alignment padding so argc lands on a 16-byte boundary
 *           AT_NULL
 *           the auxv, one (a_type, a_val) pair per entry
 *           NULL                          (envp terminator)
 *           envp[0..envc-1]
 *           NULL                          (argv terminator)
 *           argv[0..argc-1]
 *   %rsp -> argc                          (16-byte aligned)
 *
 * The auxv this builds is the subject of the differential test: run
 * field-by-field against an auxv a real Linux kernel builds, it must differ
 * only in the entries that describe the platform. AT_SYSINFO_EHDR is
 * deliberately absent -- there is no vDSO here -- and a consumer that treats
 * its absence as fatal is the bug this package exists to surface early.
 *
 * The builder is pure layout over a caller-owned buffer: it makes no host
 * call and reads no ambient state, so the identity the auxv reports
 * (AT_UID..AT_EGID, AT_SECURE, AT_HWCAP, AT_RANDOM, ...) arrives through
 * proc_image_params, which the runtime fills from the host in WP-41 and a
 * test fills with fixtures. What the builder computes for itself is only what
 * it can read from the placed image: AT_PHDR, AT_PHENT, AT_PHNUM, AT_ENTRY.
 *
 * The register contract at entry -- %rsp on argc, 16-byte aligned, and %rdx
 * carrying the shared-object atexit handler or zero -- is the caller's to
 * honor when it jumps; proc_build_stack reports the %rsp to use and echoes the
 * %rdx to set, and the entry trampoline asserts both.
 */
#ifndef ELFSYSV_LOADER_PROCESS_IMAGE_H
#define ELFSYSV_LOADER_PROCESS_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "../elf/elf_parse.h"
#include "../map/elf_map.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The auxiliary-vector types this package builds or names. The numeric values
 * are the kernel's (uapi/linux/auxvec.h and the x86 asm/auxvec.h); a consumer
 * indexes on them, so they are not ours to renumber. AT_SYSINFO_EHDR is
 * declared so the code that omits it can name what it omits. */
#define AT_NULL          0
#define AT_IGNORE        1
#define AT_EXECFD        2
#define AT_PHDR          3
#define AT_PHENT         4
#define AT_PHNUM         5
#define AT_PAGESZ        6
#define AT_BASE          7
#define AT_FLAGS         8
#define AT_ENTRY         9
#define AT_NOTELF        10
#define AT_UID           11
#define AT_EUID          12
#define AT_GID           13
#define AT_EGID          14
#define AT_PLATFORM      15
#define AT_HWCAP         16
#define AT_CLKTCK        17
#define AT_SECURE        23
#define AT_BASE_PLATFORM 24
#define AT_RANDOM        25
#define AT_HWCAP2        26
#define AT_EXECFN        31
#define AT_SYSINFO_EHDR  33

/* Failure codes. proc_ok is the only success. */
typedef enum {
	proc_ok = 0,
	proc_err_arg,     /* a precondition on the arguments was violated */
	proc_err_space,   /* the stack buffer cannot hold the image */
	proc_err_image    /* the mapping and parse disagree about the phdrs */
} proc_err;

typedef struct {
	proc_err    code;
	const char *field;
	char        msg[256];
} proc_diag;

/* The platform the auxv reports. Every field here is something the runtime
 * reads from the host at exec time and a test supplies as a fixture; none of
 * it is computed from the image. random16 points at 16 bytes the caller has
 * seeded; the builder copies them onto the stack and points AT_RANDOM at the
 * copy. platform is the AT_PLATFORM string ("x86_64"); execfn is the path
 * AT_EXECFN reports, normally argv[0] resolved. */
typedef struct {
	uint64_t    page_size;    /* AT_PAGESZ */
	uint64_t    base;         /* AT_BASE: interpreter load base, 0 if none */
	uint64_t    flags;        /* AT_FLAGS */
	uint64_t    hwcap;        /* AT_HWCAP */
	uint64_t    hwcap2;       /* AT_HWCAP2 */
	uint64_t    clktck;       /* AT_CLKTCK */
	uint32_t    uid, euid;    /* AT_UID, AT_EUID */
	uint32_t    gid, egid;    /* AT_GID, AT_EGID */
	uint64_t    secure;       /* AT_SECURE: 0 or 1 */
	const void *random16;     /* 16 bytes for AT_RANDOM; must not be null */
	const char *platform;     /* AT_PLATFORM string; must not be null */
	const char *execfn;       /* AT_EXECFN string; must not be null */
} proc_image_params;

/* Where each pointer target landed, and the vector's own extent. All are
 * runtime addresses in the image's address space, which for the in-process
 * builder is the caller's own. Filled on success for the tests to check and
 * for a diagnostic dump; a caller that only wants to enter needs sp and rdx. */
typedef struct {
	uint64_t sp;          /* the entry %rsp: points at argc, 16-byte aligned */
	uint64_t rdx;          /* the value to place in %rdx at entry (atexit/0) */

	uint64_t argc;
	uint64_t argv_ptr;     /* &argv[0] on the stack */
	uint64_t envp_ptr;     /* &envp[0] on the stack */
	uint64_t auxv_ptr;     /* &auxv[0] (first a_type) on the stack */
	unsigned auxv_count;   /* pairs, including the AT_NULL terminator */

	uint64_t at_phdr, at_entry;
	uint16_t at_phent, at_phnum;
	uint64_t at_random;    /* address of the 16 copied bytes */
	uint64_t at_platform;  /* address of the platform string */
	uint64_t at_execfn;    /* address of the execfn string */

	uint64_t used_lo;      /* lowest byte written (== sp) */
	uint64_t used_hi;      /* one past the highest byte written */
} proc_layout;

/* Build the stack for a placed image.
 *
 * m and p describe the image WP-32 mapped and WP-31 parsed; they must be the
 * results for the same object. argv and envp are NULL-terminated arrays of C
 * strings (envp may be an array holding only the terminator, but not null).
 * pr carries the platform. atexit_fn is the value for %rdx: the shared-object
 * termination handler the runtime wants registered, or 0.
 *
 * stack_lo..stack_hi is the writable region the stack is built in; the builder
 * writes only inside it and grows down from stack_hi. On success out->sp is
 * the %rsp to enter on and out is fully filled; on failure a nonzero code is
 * returned and diag names the fault. No pointer argument may be null. */
proc_err proc_build_stack(const elf_mapping *m, const elf_parsed *p,
                          char *const argv[], char *const envp[],
                          const proc_image_params *pr, uint64_t atexit_fn,
                          void *stack_lo, void *stack_hi,
                          proc_layout *out, proc_diag *diag);

/* A stable, human-readable name for an auxv type, for the differential dump
 * and test output. Returns "AT_<n>" in a static buffer for an unknown type. */
const char *proc_auxv_name(uint64_t at_type);

/* A stable name for a code, for test output. */
const char *proc_err_name(proc_err code);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_PROCESS_IMAGE_H */
