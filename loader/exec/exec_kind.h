/*
 * WP-56: which crossing an image needs.
 *
 * The stub (WP-41) enters e_entry directly. That is right for a static
 * executable, whose _start needs no loader, and wrong for a dynamic one,
 * whose entry runs before its GOT is relocated and before libc.so.6 is
 * bound. Linux resolves the difference in the kernel by reading PT_INTERP:
 * an image that names an interpreter is entered through the interpreter,
 * not at its own e_entry. There is no kernel here, so the decision moves
 * into the stub's spawn path, and this pure function is that decision.
 *
 * It is a classifier over an already-parsed image (WP-31's elf_parsed),
 * not a second parse: e_type, PT_INTERP and PT_DYNAMIC are the fields it
 * reads, and every one of them elf_parse() has already bounds-checked.
 * The verdict names which crossing the caller owes the image, and nothing
 * more; loading the interpreter stand-in and relocating the image are the
 * dynamic driver's, not this function's. DR for the dynamic crossing.
 */
#ifndef ELFSYSV_EXEC_KIND_H
#define ELFSYSV_EXEC_KIND_H

#include "../elf/elf_parse.h"

typedef enum {
/* ET_EXEC, no PT_INTERP: _start runs with no loader. Today's path,
 * the one WP-41 certifies. */
EXEC_KIND_STATIC,

/* Names a PT_INTERP: entered through the loader standing in for
 * ld-linux, which relocates the image and binds libc.so.6 before
 * the image's own entry runs. ET_EXEC at a fixed base (bzip2's
 * shape) or ET_DYN at a load bias (a PIE) both land here; the bias
 * is the driver's to compute, not this verdict's to distinguish. */
EXEC_KIND_DYNAMIC,

/* Structurally an executable image but one this route does not run:
 * a relocatable object, a core, ET_NONE, or an ET_DYN with a dynamic
 * section but no interpreter -- a shared object, loadable through the
 * dl surface but not runnable as a program here. The caller refuses
 * it rather than guessing a crossing for it. */
EXEC_KIND_UNSUPPORTED
} exec_kind;

/* Classify a parsed image. Pure: reads only the fields named above and
 * allocates nothing. `p` must be a successful elf_parse() result. */
exec_kind exec_kind_of(const elf_parsed *p);

/* A stable, lower-case name for a verdict, for diagnostics and tests.
 * Never NULL, even for a value outside the enum. */
const char *exec_kind_name(exec_kind k);

#endif /* ELFSYSV_EXEC_KIND_H */
