/*
 * WP-56: the dynamic crossing driver.
 *
 * exec_kind_of() reads a parsed image and returns static, dynamic, or
 * unsupported. The stub keeps its present path for a static image -- entered
 * at e_entry with no loader, the path WP-41 certifies -- and refuses an
 * unsupported one. A dynamic image is bzip2's shape: an ET_EXEC that names
 * /lib64/ld-linux-x86-64.so.2 in a PT_INTERP and imports libc.so.6 through an
 * unrelocated GOT. Entered the way a static image is entered it faults on its
 * first library call. This driver is what runs between the map and the entry
 * for that image, and DR-0058 is its record.
 *
 * It composes the loader packages already delivered rather than adding a new
 * one. Per DR-0058 the runtime is the interpreter: the object the stub already
 * loads at AT_BASE is both the party PT_INTERP names and the object that
 * exports libc.so.6's symbols, so there is no second ELF to place. The driver
 * enters the main image into the relocation scope as the load-order root and
 * the runtime as its single satisfied DT_NEEDED, then applies WP-34's engine
 * over the pair -- which resolves the main image's GOT and PLT against the
 * runtime's exports in load order, WP-36 deciding each versioned symbol, and
 * freezes PT_GNU_RELRO. What is left after this call is an image whose imports
 * point at the runtime; entering it at e_entry, and running its DT_INIT chain
 * first, are the caller's, staged behind this link step.
 *
 * The scope is long-lived by construction: a lazy PLT slot carries the stable
 * address of a scope object in GOT[1], so the scope must outlive every call the
 * image makes. The stub never returns, so a single process-lifetime scope is
 * right; the driver owns one and hands the caller a const view of it.
 */
#ifndef ELFSYSV_EXEC_DYN_EXEC_H
#define ELFSYSV_EXEC_DYN_EXEC_H

#include "../elf/elf_parse.h"
#include "../map/elf_map.h"
#include "../reloc/elf_reloc.h"

/* The pair a dynamic program start presents. Each image is already parsed by
 * WP-31 and placed by WP-32; the driver borrows these, it does not own or free
 * them. rt_name is the soname the main image's DT_NEEDED carries for the
 * runtime (libc.so.6 for an el8 program) and is used only for diagnostics and
 * as the scope object's name. */
typedef struct {
	elf_mapping      *main_map;
	const elf_parsed *main_p;
	elf_mapping      *rt_map;
	const elf_parsed *rt_p;
	const char       *rt_name;   /* NULL -> "libc.so.6" */
} dyn_exec_req;

/* Outcome codes. dyn_exec_ok is the only success. */
typedef enum {
	dyn_exec_ok = 0,
	dyn_exec_err_arg,        /* a required pointer was null */
	dyn_exec_err_not_dynamic,/* the main image is not the dynamic shape */
	dyn_exec_err_no_runtime, /* the main image needs a runtime; none was given */
	dyn_exec_err_scope,      /* the scope could not hold the pair */
	dyn_exec_err_reloc       /* WP-34 refused the relocation; see diag.reloc */
} dyn_exec_err;

/* Filled on a nonzero return. stage names where it stopped -- "guard",
 * "add-main", "add-runtime", or "apply" -- and reloc carries WP-34's own
 * diagnostic when the stage is "apply". */
typedef struct {
	const char     *stage;
	char            msg[256];
	elf_reloc_diag  reloc;
} dyn_exec_diag;

/* Link the main image against the runtime. On success the main image's GOT and
 * PLT point at the runtime's exports and *out_scope, if not null, is set to the
 * process-lifetime scope the crossing now depends on -- borrow it, do not
 * mutate or free it. On failure a nonzero code is returned and diag is filled.
 * The main image is not entered and its DT_INIT chain is not run; both are the
 * caller's next step. */
dyn_exec_err dyn_exec_link(const dyn_exec_req *req,
                           const elf_reloc_scope **out_scope,
                           dyn_exec_diag *diag);

/* A stable, lower-case name for a verdict, for diagnostics and tests. Never
 * NULL, even for a value outside the enum. */
const char *dyn_exec_err_name(dyn_exec_err e);

#endif /* ELFSYSV_EXEC_DYN_EXEC_H */
