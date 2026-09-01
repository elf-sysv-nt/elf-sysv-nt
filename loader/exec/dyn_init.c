/*
 * WP-56: running a dynamic main image's initializers before entry.
 * See dyn_init.h and the DR on mirroring dl_run_init's order.
 *
 * The image is one object, already parsed by WP-31 and placed by WP-32, so the
 * runner reads its dynamic array out of the mapped image the way WP-34's engine
 * does -- a file offset turned into a link vaddr through the PT_LOAD that backs
 * it, then biased into place -- and calls the initializer tags in ABI order.
 */
#include "dyn_init.h"
#include "../elf/elf_types.h"

#include <stddef.h>

/* The (argc, argv, envp) form DT_INIT and the array entries are called with,
 * and -- this is load-bearing -- the System V AMD64 calling convention they use,
 * which the stub that calls them does not. The stub is a Windows PE built for
 * the Microsoft x64 ABI (see enter.S), where %rsi and %rdi are callee-saved and
 * arguments arrive in %rcx/%rdx/%r8; a System V initializer takes its arguments
 * in %rdi/%rsi/%rdx and treats %rsi and %rdi as scratch. Called as an ordinary
 * pointer the mismatch passes the wrong argument registers and lets the callee
 * clobber registers the caller trusts, which corrupts the stub after the init
 * returns. The sysv_abi attribute makes the compiler emit the cross-ABI call --
 * marshalling the arguments and preserving the Microsoft-callee-saved registers
 * around it -- the same bridge enter.S writes by hand for the one-way entry. */
typedef void __attribute__((sysv_abi)) (*init_fn)(int, char **, char **);

/* Turn a validated file offset into the link vaddr the loaded image carries it
 * at, by finding the PT_LOAD that backs it. Returns 1 on success. This mirrors
 * elf_reloc's off_to_vaddr; the dynamic section is the only offset read here. */
static int off_to_vaddr(const elf_parsed *p, uint64_t off, uint64_t *vaddr)
{
	unsigned i;
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		if (off >= s->off && off < s->off + s->filesz) {
			*vaddr = s->vaddr + (off - s->off);
			return 1;
		}
	}
	return 0;
}

/* Read one dynamic tag's value out of the mapped dynamic array. Returns 1 with
 * *out set, or 0 when the array carries no such tag. */
static int dyn_val(const Elf64_Dyn *dyn, int64_t tag, uint64_t *out)
{
	const Elf64_Dyn *d;
	for (d = dyn; d->d_tag != DT_NULL; d++) {
		if (d->d_tag == tag) {
			*out = d->d_un.d_val;
			return 1;
		}
	}
	return 0;
}

/* A dynamic entry holding an address, biased into the mapped image. Returns 0
 * when the tag is absent or the address is null. */
static uint64_t dyn_addr(const Elf64_Dyn *dyn, int64_t tag, uint64_t bias)
{
	uint64_t v;
	if (!dyn_val(dyn, tag, &v) || v == 0)
		return 0;
	return v + bias;
}

const char *dyn_init_err_name(dyn_init_err e)
{
	switch (e) {
	case dyn_init_ok:          return "ok";
	case dyn_init_err_arg:     return "arg";
	case dyn_init_err_dynamic: return "dynamic";
	}
	return "?";
}

/* Call every function in an init array in forward order, skipping the linker's
 * 0 and ~0 padding, and count each call. */
static void run_array(const Elf64_Dyn *dyn, uint64_t bias,
                      int64_t arr_tag, int64_t sz_tag,
                      int argc, char **argv, char **envp, unsigned *ran)
{
	uint64_t base = dyn_addr(dyn, arr_tag, bias);
	uint64_t bytes = 0, count, i;

	if (!base || !dyn_val(dyn, sz_tag, &bytes) || bytes < sizeof(uint64_t))
		return;
	count = bytes / sizeof(uint64_t);

	for (i = 0; i < count; i++) {
		uint64_t fn = ((const uint64_t *)(uintptr_t) base)[i];
		if (fn != 0 && fn != (uint64_t) -1) {
			((init_fn)(uintptr_t) fn)(argc, argv, envp);
			(*ran)++;
		}
	}
}

dyn_init_err dyn_init_run(const elf_parsed *p, const elf_mapping *m,
                          int argc, char **argv, char **envp,
                          unsigned *out_ran, dyn_init_diag *diag)
{
	const Elf64_Dyn *dyn;
	uint64_t dyn_vaddr, init;
	unsigned ran = 0;

	if (diag) { diag->stage = NULL; diag->ran = 0; }
	if (out_ran)
		*out_ran = 0;

	if (!p || !m) {
		if (diag) diag->stage = "guard";
		return dyn_init_err_arg;
	}

	/* No dynamic section means no initializer tags: a success that runs
	 * nothing, the shape the crossing's bare specimens have. */
	if (!p->has_dynamic)
		return dyn_init_ok;

	if (!off_to_vaddr(p, p->dyn_off, &dyn_vaddr)) {
		if (diag) diag->stage = "locate";
		return dyn_init_err_dynamic;
	}
	dyn = (const Elf64_Dyn *)(uintptr_t)(dyn_vaddr + m->load_bias);

	/* ABI order, the same WP-38's dl_run_init runs: DT_PREINIT_ARRAY (a
	 * program's alone) before DT_INIT (legacy _init) before DT_INIT_ARRAY. */
	run_array(dyn, m->load_bias, DT_PREINIT_ARRAY, DT_PREINIT_ARRAYSZ,
	          argc, argv, envp, &ran);

	init = dyn_addr(dyn, DT_INIT, m->load_bias);
	if (init) {
		((init_fn)(uintptr_t) init)(argc, argv, envp);
		ran++;
	}

	run_array(dyn, m->load_bias, DT_INIT_ARRAY, DT_INIT_ARRAYSZ,
	          argc, argv, envp, &ran);

	if (out_ran)
		*out_ran = ran;
	if (diag)
		diag->ran = ran;
	return dyn_init_ok;
}
