/*
 * WP-56: the dynamic crossing driver. See dyn_exec.h and DR-0058.
 *
 * The composition WP-34's own certification runs by hand -- init a scope, add
 * the objects root-first, apply -- lifted into one function with the load-order
 * policy DR-0058 fixes and the classifier as its gate, so the stub gains one
 * call rather than a second copy of the sequence. The relocation itself is
 * WP-34's; this owns only the order the objects enter the scope in and the
 * scope's process lifetime.
 */
#include "dyn_exec.h"
#include "exec_kind.h"

#include <stddef.h>
#include <string.h>

/* One process runs one main image. A lazy PLT slot carries the stable address
 * of a scope object in GOT[1] (WP-34), so the scope must outlive every call the
 * image makes; the stub never returns, so a single file-static scope is the
 * whole of its lifetime management. */
static elf_reloc_scope g_scope;

static void note(dyn_exec_diag *d, const char *stage, const char *msg)
{
	if (!d)
		return;
	d->stage = stage;
	if (msg) {
		strncpy(d->msg, msg, sizeof d->msg - 1);
		d->msg[sizeof d->msg - 1] = '\0';
	}
}

const char *dyn_exec_err_name(dyn_exec_err e)
{
	switch (e) {
	case dyn_exec_ok:            return "ok";
	case dyn_exec_err_arg:       return "arg";
	case dyn_exec_err_not_dynamic: return "not-dynamic";
	case dyn_exec_err_no_runtime:  return "no-runtime";
	case dyn_exec_err_scope:     return "scope";
	case dyn_exec_err_reloc:     return "reloc";
	}
	return "?";
}

/* Map WP-34's failure onto the driver's vocabulary. A scope that cannot hold
 * two objects is the driver's own limit to name; everything else is the
 * engine's refusal of the pair, and diag->reloc already carries its detail. */
static dyn_exec_err from_reloc(elf_reloc_err re)
{
	return (re == elf_reloc_err_scope_full) ? dyn_exec_err_scope
	                                        : dyn_exec_err_reloc;
}

dyn_exec_err dyn_exec_link(const dyn_exec_req *req,
                           const elf_reloc_scope **out_scope,
                           dyn_exec_diag *diag)
{
	const char *rt_name;
	elf_reloc_err re;

	if (diag) {
		diag->stage = NULL;
		diag->msg[0] = '\0';
		memset(&diag->reloc, 0, sizeof diag->reloc);
	}

	if (!req || !req->main_map || !req->main_p) {
		note(diag, "guard", "the main image is missing");
		return dyn_exec_err_arg;
	}

	/* The classifier's verdict is the gate, so the stub's one branch and this
	 * driver never disagree about what "dynamic" means. A static image enters
	 * at e_entry with no loader and an unsupported one is refused upstream;
	 * neither reaches a correct caller, and this rejects it if one does. */
	if (exec_kind_of(req->main_p) != EXEC_KIND_DYNAMIC) {
		note(diag, "guard", "the main image is not the dynamic shape");
		return dyn_exec_err_not_dynamic;
	}

	/* A dynamic image imports from a runtime; the crossing has nothing to
	 * resolve against without one. */
	if (!req->rt_map || !req->rt_p) {
		note(diag, "guard", "the dynamic image needs a runtime; none was given");
		return dyn_exec_err_no_runtime;
	}
	rt_name = req->rt_name ? req->rt_name : "libc.so.6";

	elf_reloc_scope_init(&g_scope);

	/* Load order is resolution order. The main image is obj[0], the root a
	 * first-definition search starts from, and the runtime is its single
	 * satisfied DT_NEEDED behind it. DR-0058. */
	re = elf_reloc_add(&g_scope, req->main_map, req->main_p, "<main>",
	                   diag ? &diag->reloc : NULL);
	if (re != elf_reloc_ok) {
		note(diag, "add-main", "the main image's dynamic view did not read");
		return from_reloc(re);
	}
	re = elf_reloc_add(&g_scope, req->rt_map, req->rt_p, rt_name,
	                   diag ? &diag->reloc : NULL);
	if (re != elf_reloc_ok) {
		note(diag, "add-runtime", "the runtime's dynamic view did not read");
		return from_reloc(re);
	}

	/* One apply over the pair: the main image's GOT and PLT resolve against
	 * the runtime's exports in load order, TLS module ids and offsets are
	 * assigned, and each object's PT_GNU_RELRO is frozen. WP-34. */
	re = elf_reloc_apply(&g_scope, diag ? &diag->reloc : NULL);
	if (re != elf_reloc_ok) {
		note(diag, "apply", "the relocation engine refused the pair");
		return from_reloc(re);
	}

	if (out_scope)
		*out_scope = &g_scope;
	return dyn_exec_ok;
}
