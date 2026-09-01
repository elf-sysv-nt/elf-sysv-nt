/* WP-41: the spawn-path branch. See dispatch.h for what it decides and
 * DR-0027 and DR-0028 for why it decides it that way.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static exec_err fault(exec_diag *d, exec_err code, const char *field,
                      const char *fmt, ...)
{
	va_list ap;
	if (d) {
		d->code = code;
		d->field = field;
		va_start(ap, fmt);
		vsnprintf(d->msg, sizeof d->msg, fmt, ap);
		va_end(ap);
	}
	return code;
}

/* The head reader the resolver walks the chain with: the one place this
 * package touches the file system, and it only ever reads the leading bytes of
 * a file it was asked about. */
static long read_head(void *ctx, const char *path, unsigned char *buf, size_t n)
{
	FILE *f;
	size_t got;

	(void) ctx;
	if (!(f = fopen(path, "rb")))
		return -1;
	got = fread(buf, 1, n, f);
	fclose(f);
	return (long) got;
}

/* The host's command-line quoting, which is a parsing rule rather than a
 * quoting one: CommandLineToArgvW treats a backslash as an escape only when a
 * quote eventually follows it, so a run of backslashes is doubled exactly when
 * it precedes a quote or the closing quote of the argument, and left alone
 * otherwise. Getting this wrong turns an argument containing a backslash and a
 * quote into two arguments, which is how a path becomes a command. */
/* One character, written if there is room and counted either way, so the same
 * walk both sizes and fills. A function rather than a macro: the macro this
 * started as swallowed its argument's side effect on the sizing pass, where
 * the store it guarded never ran, and the loop that advanced through the
 * argument inside that argument never advanced. It cost an afternoon. */
static void put(char *out, size_t n, size_t *used, char c)
{
	if (*used < n)
		out[*used] = c;
	(*used)++;
}

size_t exec_quote_arg(const char *arg, char *out, size_t n)
{
	size_t used = 0, slashes, i;

	if (!arg)
		return 0;

	put(out, n, &used, '"');
	for (i = 0; arg[i]; ) {
		slashes = 0;
		while (arg[i] == '\\') { slashes++; i++; }
		if (!arg[i]) {
			/* At the end: these backslashes precede the closing
			 * quote, so each has to be doubled or the quote would
			 * be escaped by the last one. */
			for (; slashes; slashes--) {
				put(out, n, &used, '\\');
				put(out, n, &used, '\\');
			}
			break;
		}
		if (arg[i] == '"') {
			for (; slashes; slashes--) {
				put(out, n, &used, '\\');
				put(out, n, &used, '\\');
			}
			put(out, n, &used, '\\');
			put(out, n, &used, '"');
			i++;
			continue;
		}
		for (; slashes; slashes--)
			put(out, n, &used, '\\');
		put(out, n, &used, arg[i++]);
	}
	put(out, n, &used, '"');
	if (used < n)
		out[used] = '\0';
	else if (n)
		out[n - 1] = '\0';
	return used;
}

/* The environment block CreateProcess wants: the strings run together, each
 * terminated, and the whole thing terminated again. Returns a block the caller
 * frees, or null if envp was null (which means inherit) or the allocation
 * failed. */
static char *env_block(char *const envp[], int *failed)
{
	size_t total = 1, i;
	char *block, *at;

	*failed = 0;
	if (!envp)
		return NULL;
	for (i = 0; envp[i]; i++)
		total += strlen(envp[i]) + 1;
	if (total < 2)
		total = 2;
	if (!(block = malloc(total))) {
		*failed = 1;
		return NULL;
	}
	at = block;
	for (i = 0; envp[i]; i++) {
		size_t len = strlen(envp[i]) + 1;
		memcpy(at, envp[i], len);
		at += len;
	}
	*at = '\0';
	return block;
}

const char *exec_image_operand(const exec_config *cfg, const char *posix,
                               char *buf, size_t n)
{
	if (cfg && cfg->image_path && buf && n &&
	    cfg->image_path(posix, buf, n) == 0)
		return buf;
	return posix;
}

static char *build_command(const char *stub, const char *stub_options,
                           const char *image, const binfmt_resolved *r)
{
	size_t need = 1, used = 0;
	unsigned i;
	char *cmd;

	need += exec_quote_arg(stub, NULL, 0) + 1;
	if (stub_options)
		need += strlen(stub_options) + 1;
	/* The image is named once as the stub's operand -- image, which may be
	 * the host form of the path the real-process stub opens -- and then
	 * again in the program's own vector, because the two are not the same
	 * thing: the kernel's rebuild can leave a script's path in argv[0]
	 * while the file being run is the interpreter's, and the operand is the
	 * front end's to name for the stub while the vector is the program's to
	 * see. */
	need += exec_quote_arg(image, NULL, 0) + 1;
	for (i = 0; i < r->argc; i++)
		need += exec_quote_arg(r->argv[i], NULL, 0) + 1;

	if (!(cmd = malloc(need)))
		return NULL;

	used += exec_quote_arg(stub, cmd + used, need - used);
	if (stub_options) {
		used += (size_t) snprintf(cmd + used, need - used, " %s",
					  stub_options);
	}
	cmd[used++] = ' ';
	used += exec_quote_arg(image, cmd + used, need - used);
	for (i = 0; i < r->argc; i++) {
		cmd[used++] = ' ';
		used += exec_quote_arg(r->argv[i], cmd + used, need - used);
	}
	cmd[used] = '\0';
	return cmd;
}

exec_err elf_exec(const char *path, char *const argv[], char *const envp[],
                  const exec_config *cfg, exec_spawned *out, exec_diag *diag)
{
	binfmt_diag bdiag;
	binfmt_err brc;
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	uint64_t base, size;
	char *cmd, *block;
	const char *image;
	char image_win[4096];
	int env_failed;
	win_err werr;

	if (!path || !argv || !cfg || !cfg->stub || !out)
		return fault(diag, exec_err_arg, "argument",
			     "a required argument was null");

	memset(out, 0, sizeof *out);
	memset(&bdiag, 0, sizeof bdiag);

	brc = binfmt_resolve(path, argv, read_head, NULL, &out->resolved, &bdiag);
	if (brc != binfmt_ok)
		return fault(diag, exec_err_format, bdiag.field,
			     "%s: %s: %s", path, binfmt_err_name(brc), bdiag.msg);

	/* Everything that is not ELF at the end of the chain is the host's,
	 * and it gets the rebuilt vector rather than the original one. */
	if (out->resolved.kind != binfmt_elf)
		return exec_not_ours;

	base = cfg->window_base ? cfg->window_base : ELF_WINDOW_BASE;
	size = cfg->window_size ? cfg->window_size : ELF_WINDOW_SIZE;

	/* The stub opens the image itself, so the operand is named in the form
	 * that stub resolves: the real-process stub is handed the host path a
	 * converter produces, the plain-PE stub the Cygwin path unchanged. The
	 * program's own vector (out->resolved.argv) is untouched either way. */
	image = exec_image_operand(cfg, out->resolved.file,
				   image_win, sizeof image_win);
	if (!(cmd = build_command(cfg->stub, cfg->stub_options, image,
				  &out->resolved)))
		return fault(diag, exec_err_spawn, "command line",
			     "no room for the command line");
	block = env_block(envp, &env_failed);
	if (env_failed) {
		free(cmd);
		return fault(diag, exec_err_spawn, "environment",
			     "no room for the environment block");
	}

	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	if (!CreateProcessA(NULL, cmd, NULL, NULL, cfg->inherit ? TRUE : FALSE,
			    CREATE_SUSPENDED, block, NULL, &si, &pi)) {
		exec_err rc = fault(diag, exec_err_spawn, "CreateProcess",
				    "cannot start the stub %s: error %lu",
				    cfg->stub, (unsigned long) GetLastError());
		free(cmd);
		free(block);
		return rc;
	}
	free(cmd);
	free(block);

	/* The whole reason the child was created suspended. Nothing of its own
	 * has run, so the low window is still free in it; once it resumes, the
	 * kernel's stack placement and the Cygwin DLL's initialization will
	 * have taken the region and it is too late. DR-0028. */
	werr = elf_window_reserve_in(pi.hProcess, &out->window, base, size);
	if (werr != win_ok) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return fault(diag, exec_err_window, "window",
			     "the low window at 0x%llx for 0x%llx could not be "
			     "reserved in the child (%s); the stub must be "
			     "linked with a stack reserve of 0x%x or less",
			     (unsigned long long) base,
			     (unsigned long long) size, win_err_name(werr),
			     (unsigned) ELF_STUB_STACK_RESERVE);
	}

	if (ResumeThread(pi.hThread) == (DWORD) -1) {
		TerminateProcess(pi.hProcess, 1);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return fault(diag, exec_err_spawn, "ResumeThread",
			     "the child would not resume: error %lu",
			     (unsigned long) GetLastError());
	}

	out->pid = (unsigned long) pi.dwProcessId;
	out->proc = pi.hProcess;
	out->thread = pi.hThread;
	return exec_ok;
}

int elf_exec_wait(exec_spawned *s, int *status)
{
	DWORD code = 0;

	if (!s || !s->proc)
		return -1;
	if (WaitForSingleObject((HANDLE) s->proc, INFINITE) != WAIT_OBJECT_0)
		return -1;
	if (!GetExitCodeProcess((HANDLE) s->proc, &code))
		return -1;
	if (status)
		*status = (int) code;
	return 0;
}

void elf_exec_close(exec_spawned *s)
{
	if (!s)
		return;
	if (s->thread)
		CloseHandle((HANDLE) s->thread);
	if (s->proc)
		CloseHandle((HANDLE) s->proc);
	s->thread = NULL;
	s->proc = NULL;
}

const char *exec_err_name(exec_err code)
{
	switch (code) {
	case exec_ok:         return "exec_ok";
	case exec_not_ours:   return "exec_not_ours";
	case exec_err_arg:    return "exec_err_arg";
	case exec_err_format: return "exec_err_format";
	case exec_err_window: return "exec_err_window";
	case exec_err_spawn:  return "exec_err_spawn";
	}
	return "exec_err_?";
}
