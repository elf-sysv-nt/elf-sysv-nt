/* WP-41: the executable-format branch. See binfmt.h for what this decides and
 * DR-0027 for why it decides in this order.
 *
 * Nothing here opens a file or allocates. The leading bytes arrive from the
 * caller, the resolved chain is written into caller-owned storage, and the
 * only loop that can run more than a bounded number of times is the one the
 * depth limit bounds. That is deliberate: this code reads the first bytes of
 * whatever a user asked to run, which makes every field in it attacker-shaped,
 * and a parser with no allocation and no unbounded loop has a much smaller
 * surface to get wrong.
 */
#include "binfmt.h"

#include <string.h>

#define ET_EXEC    2
#define ET_DYN     3
#define EM_X86_64  62

/* The shortest head that can carry an answer: the e_machine field ends at
 * byte 20, and nothing before it distinguishes an ELF we can run from one we
 * cannot. */
#define ELF_MIN_HEAD 20

static binfmt_err fault(binfmt_diag *d, binfmt_err code, const char *field,
                        const char *msg)
{
	if (d) {
		d->code = code;
		d->field = field;
		strncpy(d->msg, msg, sizeof d->msg - 1);
		d->msg[sizeof d->msg - 1] = '\0';
	}
	return code;
}

static uint16_t le16(const unsigned char *p)
{
	return (uint16_t)(p[0] | ((uint16_t) p[1] << 8));
}

static int space(char c)
{
	return c == ' ' || c == '\t';
}

/* The `#!' line, parsed the way the kernel parses it.
 *
 * The terminator is the first newline or NUL at or after byte 2. A head that
 * fills the whole buffer without one is refused rather than truncated: a
 * truncated interpreter path names a different program, and silently running
 * a different program is worse than refusing to run any. A short read behaves
 * as though the file were NUL-padded, which is what makes a two-line script
 * with no trailing newline work.
 *
 * Trailing blanks are stripped, then leading blanks skipped, then the
 * interpreter runs to the first blank and the single optional argument is
 * everything after the blanks that follow it -- blanks and all, since the
 * kernel does not split it and a script that passes `-e -u' as one argument
 * relies on that. */
static binfmt_err parse_shebang(const unsigned char *head, size_t n,
                                binfmt_probe *out, binfmt_diag *diag)
{
	char buf[BINFMT_HEAD_MAX + 1];
	size_t end, i;
	const char *name, *arg;

	memcpy(buf, head, n);
	buf[n] = '\0';

	for (end = 2; end < n; end++)
		if (buf[end] == '\0' || buf[end] == '\n')
			break;
	if (end == n && n >= BINFMT_HEAD_MAX)
		return fault(diag, binfmt_err_script_long, "#!",
			     "the interpreter line does not end within the "
			     "first 256 bytes");
	buf[end] = '\0';

	while (end > 2 && space(buf[end - 1]))
		buf[--end] = '\0';

	i = 2;
	while (i < end && space(buf[i]))
		i++;
	if (i >= end)
		return fault(diag, binfmt_err_script_empty, "#!",
			     "the interpreter line names no interpreter");

	name = buf + i;
	while (i < end && !space(buf[i]))
		i++;
	arg = NULL;
	if (i < end) {
		buf[i++] = '\0';
		while (i < end && space(buf[i]))
			i++;
		if (i < end)
			arg = buf + i;
	}

	out->kind = binfmt_script;
	strncpy(out->interp, name, sizeof out->interp - 1);
	out->interp[sizeof out->interp - 1] = '\0';
	if (arg) {
		out->has_arg = 1;
		strncpy(out->arg, arg, sizeof out->arg - 1);
		out->arg[sizeof out->arg - 1] = '\0';
	}
	return binfmt_ok;
}

binfmt_err binfmt_classify(const unsigned char *head, size_t n,
                           binfmt_probe *out, binfmt_diag *diag)
{
	if (!out || (!head && n))
		return fault(diag, binfmt_err_arg, "head",
			     "no probe to fill, or a length with no bytes");
	if (n > BINFMT_HEAD_MAX)
		n = BINFMT_HEAD_MAX;

	memset(out, 0, sizeof *out);
	out->kind = binfmt_unknown;

	/* The order is DR-0027's, and it is one chain rather than three tests
	 * bolted together: the first match wins and the rest are not consulted.
	 * ELF first, `#!' second, the host's own format last, unrecognized
	 * after that. */
	if (n >= 4 && !memcmp(head, "\177ELF", 4)) {
		if (n < ELF_MIN_HEAD)
			return fault(diag, binfmt_err_elf_class, "e_ident",
				     "ELF magic in a file too short to carry "
				     "an ELF header");
		if (head[4] != 2 || head[5] != 1)
			return fault(diag, binfmt_err_elf_class, "e_ident",
				     "not little-endian ELF64");
		if (le16(head + 18) != EM_X86_64)
			return fault(diag, binfmt_err_elf_class, "e_machine",
				     "not an x86-64 object");
		out->e_type = le16(head + 16);
		if (out->e_type != ET_EXEC && out->e_type != ET_DYN) {
			out->e_type = 0;
			return fault(diag, binfmt_err_elf_type, "e_type",
				     "neither ET_EXEC nor ET_DYN, so there is "
				     "nothing to enter");
		}
		out->kind = binfmt_elf;
		return binfmt_ok;
	}

	if (n >= 2 && head[0] == '#' && head[1] == '!')
		return parse_shebang(head, n, out, diag);

	if (n >= 2 && head[0] == 'M' && head[1] == 'Z') {
		out->kind = binfmt_host;
		return binfmt_ok;
	}

	return binfmt_ok;
}

/* Storage for the strings the resolver has to own: the paths and interpreter
 * arguments it read out of files, which live in nobody else's memory. The
 * caller's own argv strings are pointed at rather than copied. */
static const char *keep(binfmt_resolved *r, const char *s)
{
	size_t len = strlen(s) + 1;
	char *at;

	if (len > sizeof r->store - r->store_used)
		return NULL;
	at = r->store + r->store_used;
	memcpy(at, s, len);
	r->store_used += len;
	return at;
}

/* Room reserved at the front of the working vector for the pushes. Each hop
 * drops one element and pushes at most three, so BINFMT_MAX_DEPTH * 3 is the
 * bound and the slack above it is only to keep the arithmetic uninteresting. */
#define FRONT_SLACK (BINFMT_MAX_DEPTH * 3 + 4)

binfmt_err binfmt_resolve(const char *path, char *const argv[],
                          binfmt_head_fn read_head, void *ctx,
                          binfmt_resolved *out, binfmt_diag *diag)
{
	const char *work[BINFMT_ARGV_MAX];
	unsigned front = FRONT_SLACK, tail, i;
	const char *cur;
	unsigned depth = 0;

	if (!path || !argv || !argv[0] || !read_head || !out)
		return fault(diag, binfmt_err_arg, "argument",
			     "a required argument was null");

	memset(out, 0, sizeof *out);
	out->kind = binfmt_unknown;

	tail = front;
	for (i = 0; argv[i]; i++) {
		if (tail >= BINFMT_ARGV_MAX)
			return fault(diag, binfmt_err_space, "argv",
				     "the argument vector is longer than the "
				     "resolver can carry");
		work[tail++] = argv[i];
	}

	if (!(cur = keep(out, path)))
		return fault(diag, binfmt_err_space, "path",
			     "the path is longer than the resolver can carry");

	for (;;) {
		unsigned char head[BINFMT_HEAD_MAX];
		binfmt_probe probe;
		binfmt_err rc;
		long got;
		const char *interp, *arg = NULL, *file;

		got = read_head(ctx, cur, head, sizeof head);
		if (got < 0)
			return fault(diag, binfmt_err_read, "path",
				     "the file could not be read");
		if ((size_t) got > sizeof head)
			got = (long) sizeof head;

		if ((rc = binfmt_classify(head, (size_t) got, &probe, diag)) != binfmt_ok)
			return rc;

		if (probe.kind != binfmt_script) {
			out->kind = probe.kind;
			out->e_type = probe.e_type;
			out->depth = depth;
			out->file = cur;
			out->argc = tail - front;
			for (i = 0; i < out->argc; i++)
				out->argv[i] = work[front + i];
			out->argv[out->argc] = NULL;
			return binfmt_ok;
		}

		/* A hop. The limit is also the cycle detector: a script whose
		 * interpreter is itself simply spends the four hops and is
		 * refused, which is the answer a cycle deserves and costs no
		 * bookkeeping to reach. */
		if (depth >= BINFMT_MAX_DEPTH)
			return fault(diag, binfmt_err_depth, "#!",
				     "the interpreter chain is longer than "
				     "four hops, or it is a cycle");
		depth++;

		if (!(interp = keep(out, probe.interp)) ||
		    (probe.has_arg && !(arg = keep(out, probe.arg))))
			return fault(diag, binfmt_err_space, "#!",
				     "the interpreter line is longer than the "
				     "resolver can carry");
		file = cur;

		/* The kernel's rebuild: drop the leading element, then push
		 * the file that named the interpreter, the interpreter's
		 * single argument if it had one, and the interpreter itself,
		 * so the vector reads interp, arg, file, rest. */
		front++;
		work[--front] = file;
		if (arg)
			work[--front] = arg;
		work[--front] = interp;
		if (tail - front >= BINFMT_ARGV_MAX)
			return fault(diag, binfmt_err_space, "argv",
				     "the rebuilt argument vector does not fit");

		cur = interp;
	}
}

const char *binfmt_err_name(binfmt_err code)
{
	switch (code) {
	case binfmt_ok:                return "binfmt_ok";
	case binfmt_err_arg:           return "binfmt_err_arg";
	case binfmt_err_read:          return "binfmt_err_read";
	case binfmt_err_elf_class:     return "binfmt_err_elf_class";
	case binfmt_err_elf_type:      return "binfmt_err_elf_type";
	case binfmt_err_script_empty:  return "binfmt_err_script_empty";
	case binfmt_err_script_long:   return "binfmt_err_script_long";
	case binfmt_err_depth:         return "binfmt_err_depth";
	case binfmt_err_space:         return "binfmt_err_space";
	}
	return "binfmt_err_?";
}

const char *binfmt_kind_name(binfmt_kind kind)
{
	switch (kind) {
	case binfmt_elf:     return "elf";
	case binfmt_script:  return "script";
	case binfmt_host:    return "host";
	case binfmt_unknown: return "unknown";
	}
	return "?";
}
