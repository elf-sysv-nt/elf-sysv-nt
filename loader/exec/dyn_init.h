/*
 * WP-56: running a dynamic main image's initializers before entry.
 *
 * dyn_exec_link (DR-0058) leaves the crossing's main image relocated against
 * its runtime, but a program's constructors have not run: an el8 executable
 * carries DT_INIT and a DT_INIT_ARRAY the C runtime would call before main, and
 * entered at e_entry with a real startup file it would call them itself -- but
 * the images this route runs have no startup file of their own, so the loader
 * runs them, exactly as ld-linux runs an interpreter-less program's would be.
 * This is the step dyn_exec.h and the package README name as the caller's next
 * one, staged behind the link.
 *
 * The order is the one the ABI fixes and WP-38's dl_run_init already runs for
 * the dl graph: DT_PREINIT_ARRAY (a program's alone), then DT_INIT (the legacy
 * _init), then DT_INIT_ARRAY, each array in forward order, a 0 or ~0 entry
 * skipped as the linker's padding. The exec crossing holds one object -- the
 * main image, as an elf_parsed and an elf_mapping -- rather than the dl_state
 * graph, so the runner reads that pair directly; DR for why it mirrors
 * dl_run_init's order rather than composing its graph model.
 *
 * Each initializer is called with (argc, argv, envp), the convention DT_INIT
 * and the array entries share on this platform, taken from the same vector the
 * initial stack was built from. The calls run on the host stack, before
 * elf_enter parks it -- a constructor that only touches the image is safe there,
 * and a real el8 program's are, having had their PLT resolved into the runtime
 * by the link that precedes this.
 */
#ifndef ELFSYSV_EXEC_DYN_INIT_H
#define ELFSYSV_EXEC_DYN_INIT_H

#include "../elf/elf_parse.h"
#include "../map/elf_map.h"

/* Outcome codes. dyn_init_ok is the only success. */
typedef enum {
	dyn_init_ok = 0,
	dyn_init_err_arg,      /* a required pointer was null */
	dyn_init_err_dynamic   /* the image's dynamic section is not in a PT_LOAD */
} dyn_init_err;

/* Filled on a nonzero return: stage names where it stopped ("guard" or
 * "locate"), and ran carries how many initializer functions were called before
 * the stop (zero on an early guard). */
typedef struct {
	const char *stage;
	unsigned    ran;
} dyn_init_diag;

/* Run the main image's initializers in ABI order. p must be the elf_ok parse of
 * the image m maps; addresses in the init tags are link addresses and are
 * biased by m->load_bias before the call. An image with no initializer tags is
 * a success that runs nothing. On a nonzero return diag, if not null, names the
 * stage. out_ran, if not null, receives the number of functions called. */
dyn_init_err dyn_init_run(const elf_parsed *p, const elf_mapping *m,
                          int argc, char **argv, char **envp,
                          unsigned *out_ran, dyn_init_diag *diag);

/* A stable, lower-case name for a verdict, for diagnostics and tests. Never
 * NULL, even for a value outside the enum. */
const char *dyn_init_err_name(dyn_init_err e);

#endif /* ELFSYSV_EXEC_DYN_INIT_H */
