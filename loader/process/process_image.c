/* WP-40: building the initial process image's stack. See process_image.h for
 * the layout and the contract. This file is pure layout over a caller-owned
 * buffer; it makes no host call. */

#include "process_image.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* The largest argv or envp this builder places. A process image at exec time
 * has far fewer; WP-41 raises it or switches to a two-pass scheme if a caller
 * ever needs more. Kept as automatic arrays so the builder stays reentrant. */
#define PROC_MAX_VEC 1024

/* One auxv entry as it is laid on the stack. */
struct auxent { uint64_t a_type, a_val; };

static void set_diag(proc_diag *d, proc_err code, const char *field,
                     const char *fmt, ...)
{
	va_list ap;
	d->code = code;
	d->field = field;
	va_start(ap, fmt);
	vsnprintf(d->msg, sizeof d->msg, fmt, ap);
	va_end(ap);
}

/* AT_PHDR is the runtime address of the program-header table. The kernel
 * reports it as the address the phdrs were loaded at, which is the load bias
 * plus the link address of whichever PT_LOAD's file range covers e_phoff. An
 * image whose phdrs are not inside any loaded segment cannot report them, and
 * that is an image this package refuses rather than guesses about. */
static int phdr_runtime_addr(const elf_mapping *m, const elf_parsed *p,
                             uint64_t *out)
{
	unsigned i;
	for (i = 0; i < p->load_count; i++) {
		uint64_t off = p->load[i].off;
		if (p->phoff >= off && p->phoff < off + p->load[i].filesz) {
			*out = m->load_bias + p->load[i].vaddr + (p->phoff - off);
			return 1;
		}
	}
	return 0;
}

/* Count the terminated pointer array; envp may carry only its terminator. */
static unsigned count_vec(char *const v[])
{
	unsigned n = 0;
	while (v[n])
		n++;
	return n;
}

const char *proc_err_name(proc_err code)
{
	switch (code) {
	case proc_ok:        return "proc_ok";
	case proc_err_arg:   return "proc_err_arg";
	case proc_err_space: return "proc_err_space";
	case proc_err_image: return "proc_err_image";
	}
	return "proc_err_?";
}

const char *proc_auxv_name(uint64_t t)
{
	static char buf[24];
	switch (t) {
	case AT_NULL:          return "AT_NULL";
	case AT_IGNORE:        return "AT_IGNORE";
	case AT_EXECFD:        return "AT_EXECFD";
	case AT_PHDR:          return "AT_PHDR";
	case AT_PHENT:         return "AT_PHENT";
	case AT_PHNUM:         return "AT_PHNUM";
	case AT_PAGESZ:        return "AT_PAGESZ";
	case AT_BASE:          return "AT_BASE";
	case AT_FLAGS:         return "AT_FLAGS";
	case AT_ENTRY:         return "AT_ENTRY";
	case AT_NOTELF:        return "AT_NOTELF";
	case AT_UID:           return "AT_UID";
	case AT_EUID:          return "AT_EUID";
	case AT_GID:           return "AT_GID";
	case AT_EGID:          return "AT_EGID";
	case AT_PLATFORM:      return "AT_PLATFORM";
	case AT_HWCAP:         return "AT_HWCAP";
	case AT_CLKTCK:        return "AT_CLKTCK";
	case AT_SECURE:        return "AT_SECURE";
	case AT_BASE_PLATFORM: return "AT_BASE_PLATFORM";
	case AT_RANDOM:        return "AT_RANDOM";
	case AT_HWCAP2:        return "AT_HWCAP2";
	case AT_EXECFN:        return "AT_EXECFN";
	case AT_SYSINFO_EHDR:  return "AT_SYSINFO_EHDR";
	}
	snprintf(buf, sizeof buf, "AT_%llu", (unsigned long long) t);
	return buf;
}

proc_err proc_build_stack(const elf_mapping *m, const elf_parsed *p,
                          char *const argv[], char *const envp[],
                          const proc_image_params *pr, uint64_t atexit_fn,
                          void *stack_lo, void *stack_hi,
                          proc_layout *out, proc_diag *diag)
{
	uint64_t phdr_addr;
	unsigned argc, envc, i, ai;
	uint64_t lo = (uint64_t)(uintptr_t) stack_lo;
	uint64_t hi = (uint64_t)(uintptr_t) stack_hi;
	uint64_t sp;
	uint64_t rand_addr, plat_addr, execfn_addr;
	uint64_t *arg_addr = NULL, *env_addr = NULL;
	uint64_t arg_tbl[PROC_MAX_VEC], env_tbl[PROC_MAX_VEC];
	struct auxent aux[32];
	unsigned naux = 0;
	unsigned n_words, reserve_words;
	uint64_t argc_addr, w;
	uint64_t plat_len, execfn_len;

	if (!m || !p || !argv || !envp || !pr || !out || !diag ||
	    !stack_lo || !stack_hi) {
		if (diag) set_diag(diag, proc_err_arg, "arguments",
		                   "a required pointer argument was null");
		return proc_err_arg;
	}
	if (!pr->random16 || !pr->platform || !pr->execfn) {
		set_diag(diag, proc_err_arg, "proc_image_params",
		         "random16, platform, and execfn must all be set");
		return proc_err_arg;
	}
	if (hi <= lo) {
		set_diag(diag, proc_err_arg, "stack",
		         "stack_hi 0x%llx is not above stack_lo 0x%llx",
		         (unsigned long long) hi, (unsigned long long) lo);
		return proc_err_arg;
	}

	if (!phdr_runtime_addr(m, p, &phdr_addr)) {
		set_diag(diag, proc_err_image, "e_phoff",
		         "the program headers at file offset 0x%llx lie in no "
		         "loaded segment, so AT_PHDR cannot be reported",
		         (unsigned long long) p->phoff);
		return proc_err_image;
	}

	argc = count_vec(argv);
	envc = count_vec(envp);
	if (argc > PROC_MAX_VEC || envc > PROC_MAX_VEC) {
		set_diag(diag, proc_err_space, "argv/envp",
		         "argc %u or envc %u exceeds the builder's cap of %d",
		         argc, envc, PROC_MAX_VEC);
		return proc_err_space;
	}

	memset(out, 0, sizeof *out);

	/* --- the pointer targets, at the top, growing down --------------- */
	sp = hi;

	/* AT_RANDOM: 16 bytes. */
	sp -= 16;
	memcpy((void *)(uintptr_t) sp, pr->random16, 16);
	rand_addr = sp;

	/* AT_PLATFORM and AT_EXECFN strings. */
	plat_len = (uint64_t) strlen(pr->platform) + 1;
	sp -= plat_len;
	memcpy((void *)(uintptr_t) sp, pr->platform, (size_t) plat_len);
	plat_addr = sp;

	execfn_len = (uint64_t) strlen(pr->execfn) + 1;
	sp -= execfn_len;
	memcpy((void *)(uintptr_t) sp, pr->execfn, (size_t) execfn_len);
	execfn_addr = sp;

	/* env strings, then arg strings. Copied through small in-frame address
	 * tables so the vector below can point at each copy. The tables live in
	 * the builder's own frame, not on the image stack. */
	{
		arg_addr = arg_tbl;
		env_addr = env_tbl;
		for (i = 0; i < envc; i++) {
			uint64_t len = (uint64_t) strlen(envp[i]) + 1;
			sp -= len;
			memcpy((void *)(uintptr_t) sp, envp[i], (size_t) len);
			env_addr[i] = sp;
		}
		for (i = 0; i < argc; i++) {
			uint64_t len = (uint64_t) strlen(argv[i]) + 1;
			sp -= len;
			memcpy((void *)(uintptr_t) sp, argv[i], (size_t) len);
			arg_addr[i] = sp;
		}
	}

	/* --- the auxiliary vector ---------------------------------------- *
	 * Emitted in the kernel's relative order for the entries we carry.
	 * AT_SYSINFO_EHDR is deliberately not here: there is no vDSO, and a
	 * consumer that faults on its absence is the bug this surfaces. */
	aux[naux].a_type = AT_HWCAP;   aux[naux++].a_val = pr->hwcap;
	aux[naux].a_type = AT_PAGESZ;  aux[naux++].a_val = pr->page_size;
	aux[naux].a_type = AT_CLKTCK;  aux[naux++].a_val = pr->clktck;
	aux[naux].a_type = AT_PHDR;    aux[naux++].a_val = phdr_addr;
	aux[naux].a_type = AT_PHENT;   aux[naux++].a_val = p->phentsize;
	aux[naux].a_type = AT_PHNUM;   aux[naux++].a_val = p->phnum;
	aux[naux].a_type = AT_BASE;    aux[naux++].a_val = pr->base;
	aux[naux].a_type = AT_FLAGS;   aux[naux++].a_val = pr->flags;
	aux[naux].a_type = AT_ENTRY;   aux[naux++].a_val = m->entry;
	aux[naux].a_type = AT_UID;     aux[naux++].a_val = pr->uid;
	aux[naux].a_type = AT_EUID;    aux[naux++].a_val = pr->euid;
	aux[naux].a_type = AT_GID;     aux[naux++].a_val = pr->gid;
	aux[naux].a_type = AT_EGID;    aux[naux++].a_val = pr->egid;
	aux[naux].a_type = AT_SECURE;  aux[naux++].a_val = pr->secure;
	aux[naux].a_type = AT_RANDOM;  aux[naux++].a_val = rand_addr;
	aux[naux].a_type = AT_HWCAP2;  aux[naux++].a_val = pr->hwcap2;
	aux[naux].a_type = AT_EXECFN;  aux[naux++].a_val = execfn_addr;
	aux[naux].a_type = AT_PLATFORM;aux[naux++].a_val = plat_addr;
	aux[naux].a_type = AT_NULL;    aux[naux++].a_val = 0;

	/* --- place the vector so argc lands 16-byte aligned -------------- *
	 * words = argc + argv + NULL + envp + NULL + auxv(2 each).  T is the
	 * string base rounded down to 16; the vector sits below it. If the word
	 * count is odd the argc slot would land 8 mod 16, so one pad word is
	 * left between the vector and the strings. */
	n_words = 1u + (argc + 1u) + (envc + 1u) + 2u * naux;
	reserve_words = n_words + (n_words & 1u);
	{
		uint64_t T = sp & ~(uint64_t) 15;
		argc_addr = T - (uint64_t) reserve_words * 8u;
	}
	if (argc_addr < lo) {
		set_diag(diag, proc_err_space, "stack",
		         "the image needs 0x%llx bytes of stack but only 0x%llx "
		         "are available", (unsigned long long)(hi - argc_addr),
		         (unsigned long long)(hi - lo));
		return proc_err_space;
	}

	/* --- write the vector upward from argc --------------------------- */
	w = argc_addr;
#define PUT(v) do { *(uint64_t *)(uintptr_t) w = (uint64_t)(v); w += 8; } while (0)
	PUT(argc);
	out->argv_ptr = w;
	for (i = 0; i < argc; i++)
		PUT(arg_addr[i]);
	PUT(0);                                  /* argv terminator */
	out->envp_ptr = w;
	for (i = 0; i < envc; i++)
		PUT(env_addr[i]);
	PUT(0);                                  /* envp terminator */
	out->auxv_ptr = w;
	for (ai = 0; ai < naux; ai++) {
		PUT(aux[ai].a_type);
		PUT(aux[ai].a_val);
	}
#undef PUT

	/* --- the result -------------------------------------------------- */
	out->sp = argc_addr;
	out->rdx = atexit_fn;
	out->argc = argc;
	out->auxv_count = naux;
	out->at_phdr = phdr_addr;
	out->at_entry = m->entry;
	out->at_phent = p->phentsize;
	out->at_phnum = p->phnum;
	out->at_random = rand_addr;
	out->at_platform = plat_addr;
	out->at_execfn = execfn_addr;
	out->used_lo = argc_addr;
	out->used_hi = hi;

	return proc_ok;
}
