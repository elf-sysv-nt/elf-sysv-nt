/* WP-41: the spawn-path branch.
 *
 * This is the half of the package that lives on the Cygwin side. Given a path
 * and an argument vector, it classifies the file through binfmt, follows any
 * `#!' chain, and either takes the ELF case itself -- starting the stub with
 * the low window already reserved in it -- or hands the case back for the host
 * spawn path to run as it always did.
 *
 * Handing back is as much a result as taking. A `#!' script whose interpreter
 * turns out to be an ordinary PE program is the host's to run, but only with
 * the vector the kernel's rebuild produced, so the answer carries that vector
 * rather than leaving the caller to redo the walk. One resolver, one walk, one
 * argument vector, whichever world the chain ends in: DR-0027.
 *
 * Nothing here replaces Cygwin's own obligations at exec -- descriptor
 * inheritance and close-on-exec, the working directory, signal disposition,
 * the environment -- which the spawn path already discharges for the PE case
 * and discharges identically for this one. What this adds is the branch and
 * the window.
 */
#ifndef ELFSYSV_LOADER_EXEC_DISPATCH_H
#define ELFSYSV_LOADER_EXEC_DISPATCH_H

#include <stdint.h>

#include "binfmt.h"
#include "reserve.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	exec_ok = 0,       /* the stub is running the image */
	exec_not_ours,     /* the host spawn path keeps this one */
	exec_err_arg,      /* a precondition on the arguments was violated */
	exec_err_format,   /* the file announced a format and then failed it */
	exec_err_window,   /* the low window could not be reserved in the child */
	exec_err_spawn     /* the host would not start the stub */
} exec_err;

typedef struct {
	exec_err    code;
	const char *field;
	char        msg[512];
} exec_diag;

typedef struct {
	/* The stub image, as a path the host's own spawn understands. There is
	 * no default: a tree that has not been installed has no fixed place to
	 * find it, and guessing would be worse than being told. */
	const char *stub;
	uint64_t    window_base;   /* 0 for ELF_WINDOW_BASE */
	uint64_t    window_size;   /* 0 for ELF_WINDOW_SIZE */
	int         inherit;       /* pass the host's inheritable handles down */
	const char *stub_options;  /* appended before the image path, or null */
} exec_config;

typedef struct {
	binfmt_resolved resolved;  /* where the chain ended and with what vector */
	elf_window      window;    /* the reservation made in the child */
	unsigned long   pid;
	void           *proc;      /* the child process handle, or null */
	void           *thread;    /* its initial thread handle, or null */
} exec_spawned;

/* Classify, follow the chain, and start the child if the chain ends in ELF.
 *
 * On exec_ok the child is running and out holds its handles, which the caller
 * closes with elf_exec_close once it has waited. On exec_not_ours the chain
 * ended somewhere the host owns and out->resolved carries the file and vector
 * the host should use; no process was started. Every other code is a refusal
 * with diag filled.
 *
 * envp may be null, in which case the child inherits this process's
 * environment. cfg->stub must name an image; no pointer argument other than
 * envp may be null. */
exec_err elf_exec(const char *path, char *const argv[], char *const envp[],
                  const exec_config *cfg, exec_spawned *out, exec_diag *diag);

/* Wait for a child started by elf_exec and report its exit status. Returns 0
 * on success and -1 if the wait failed. */
int elf_exec_wait(exec_spawned *s, int *status);

/* Close the handles in s. Safe on a zeroed or already closed structure. */
void elf_exec_close(exec_spawned *s);

/* Quote one argument for the host's command line, appending to out. This is
 * the rule CommandLineToArgvW inverts: a run of backslashes is doubled only
 * when it precedes a quote or ends the argument. It is separated out and
 * exported because the arguments it quotes came from a user, which makes it
 * worth testing on its own rather than only through a spawn.
 *
 * Returns the number of bytes it would have written, whether or not they fit,
 * so a caller can size a buffer by calling it with n of zero. */
size_t exec_quote_arg(const char *arg, char *out, size_t n);

/* A stable name for a code, for test output. */
const char *exec_err_name(exec_err code);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_EXEC_DISPATCH_H */
