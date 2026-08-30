/* WP-41 unit tests: the branch and the chain, as pure decisions.
 *
 * Nothing here spawns anything. The classifier is fed heads byte by byte and
 * the resolver is fed a fixture table instead of a file system, so the three
 * outcomes, the `#!' line's every corner, the vector the kernel rebuilds, and
 * the depth limit are all checked without a process existing anywhere. The
 * cases that matter most are the refusals: a truncated interpreter line, a
 * chain that closes on itself, an ELF this host cannot enter.
 */
#include "../binfmt.h"
#include "../dispatch.h"

#include <stdio.h>
#include <string.h>

static int failures;
static void ck(const char *what, int ok)
{
	if (!ok) { printf("    %-56s FAILED\n", what); failures++; }
}

/* A head that classifies as a runnable ELF: magic, ELF64 LSB, ET_EXEC,
 * EM_X86_64. Only the bytes the classifier reads are set. */
static void elf_head(unsigned char *h, int type, int machine, int cls)
{
	memset(h, 0, 32);
	memcpy(h, "\177ELF", 4);
	h[4] = (unsigned char) cls;
	h[5] = 1;
	h[6] = 1;
	h[16] = (unsigned char)(type & 0xff);
	h[17] = (unsigned char)(type >> 8);
	h[18] = (unsigned char)(machine & 0xff);
	h[19] = (unsigned char)(machine >> 8);
}

static binfmt_err classify_str(const char *s, binfmt_probe *p)
{
	binfmt_diag d;
	return binfmt_classify((const unsigned char *) s, strlen(s), p, &d);
}

static void test_classify(void)
{
	unsigned char h[64];
	binfmt_probe p;
	binfmt_diag d;

	elf_head(h, 2, 62, 2);
	ck("an ET_EXEC x86-64 head is elf",
	   binfmt_classify(h, 32, &p, &d) == binfmt_ok && p.kind == binfmt_elf);
	elf_head(h, 3, 62, 2);
	ck("an ET_DYN x86-64 head is elf",
	   binfmt_classify(h, 32, &p, &d) == binfmt_ok && p.kind == binfmt_elf &&
	   p.e_type == 3);
	elf_head(h, 1, 62, 2);
	ck("ET_REL is refused, not run",
	   binfmt_classify(h, 32, &p, &d) == binfmt_err_elf_type);
	elf_head(h, 2, 3, 2);
	ck("an i386 object is refused",
	   binfmt_classify(h, 32, &p, &d) == binfmt_err_elf_class);
	elf_head(h, 2, 62, 1);
	ck("a 32-bit class is refused",
	   binfmt_classify(h, 32, &p, &d) == binfmt_err_elf_class);
	elf_head(h, 2, 62, 2);
	ck("ELF magic with no header behind it is refused",
	   binfmt_classify(h, 8, &p, &d) == binfmt_err_elf_class);

	ck("MZ is the host's", classify_str("MZ\x90\x00", &p) == binfmt_ok &&
	   p.kind == binfmt_host);
	ck("an empty file is unknown",
	   binfmt_classify(h, 0, &p, &d) == binfmt_ok && p.kind == binfmt_unknown);
	ck("a text file is unknown",
	   classify_str("hello, world\n", &p) == binfmt_ok &&
	   p.kind == binfmt_unknown);
	ck("one byte of ELF magic is unknown, not elf",
	   binfmt_classify((const unsigned char *) "\177", 1, &p, &d) == binfmt_ok &&
	   p.kind == binfmt_unknown);
}

static void test_shebang(void)
{
	binfmt_probe p;
	char line[BINFMT_HEAD_MAX + 8];

	ck("a plain interpreter",
	   classify_str("#!/bin/sh\n", &p) == binfmt_ok &&
	   p.kind == binfmt_script && !strcmp(p.interp, "/bin/sh") && !p.has_arg);
	ck("blanks after #! are skipped",
	   classify_str("#!   \t/bin/sh\n", &p) == binfmt_ok &&
	   !strcmp(p.interp, "/bin/sh"));
	ck("a single argument is taken whole",
	   classify_str("#!/bin/sed -n -e p\n", &p) == binfmt_ok &&
	   !strcmp(p.interp, "/bin/sed") && p.has_arg &&
	   !strcmp(p.arg, "-n -e p"));
	ck("trailing blanks are stripped from the argument",
	   classify_str("#!/bin/awk -f  \t \n", &p) == binfmt_ok &&
	   p.has_arg && !strcmp(p.arg, "-f"));
	ck("trailing blanks with no argument leave none",
	   classify_str("#!/bin/sh   \n", &p) == binfmt_ok &&
	   !strcmp(p.interp, "/bin/sh") && !p.has_arg);
	ck("a last line with no newline still parses",
	   classify_str("#!/bin/sh", &p) == binfmt_ok &&
	   !strcmp(p.interp, "/bin/sh"));
	ck("an embedded NUL ends the line",
	   binfmt_classify((const unsigned char *) "#!/bin/sh\0-x\n", 13, &p, NULL)
	   == binfmt_ok && !strcmp(p.interp, "/bin/sh") && !p.has_arg);
	ck("a bare #! names no interpreter",
	   classify_str("#!\n", &p) == binfmt_err_script_empty);
	ck("#! and blanks alone name no interpreter",
	   classify_str("#!    \n", &p) == binfmt_err_script_empty);
	ck("a two-byte file is a truncated #!, not a script",
	   classify_str("#!", &p) == binfmt_err_script_empty);

	/* The line has to end inside the head the kernel reads, or the file is
	 * refused. Truncating would name a different program. */
	memset(line, 'a', sizeof line);
	memcpy(line, "#!/bin/", 7);
	ck("a line longer than the head is refused",
	   binfmt_classify((const unsigned char *) line, BINFMT_HEAD_MAX, &p, NULL)
	   == binfmt_err_script_long);
	line[BINFMT_HEAD_MAX - 1] = '\n';
	ck("a line ending on the last byte of the head is accepted",
	   binfmt_classify((const unsigned char *) line, BINFMT_HEAD_MAX, &p, NULL)
	   == binfmt_ok && p.kind == binfmt_script);
}

/* The fixture file system: a name, and the head a read of it returns. A head
 * given as NULL is a file that cannot be read at all. */
struct file { const char *path; const char *head; size_t len; };

static struct file fs[16];
static unsigned fs_n;

static void put(const char *path, const char *head)
{
	fs[fs_n].path = path;
	fs[fs_n].head = head;
	fs[fs_n].len = head ? strlen(head) : 0;
	fs_n++;
}

static void put_bytes(const char *path, const unsigned char *head, size_t n)
{
	fs[fs_n].path = path;
	fs[fs_n].head = (const char *) head;
	fs[fs_n].len = n;
	fs_n++;
}

static long fixture_head(void *ctx, const char *path, unsigned char *buf, size_t n)
{
	unsigned i;
	(void) ctx;
	for (i = 0; i < fs_n; i++) {
		if (strcmp(fs[i].path, path))
			continue;
		if (!fs[i].head)
			return -1;
		if (fs[i].len < n)
			n = fs[i].len;
		memcpy(buf, fs[i].head, n);
		return (long) n;
	}
	return -1;
}

static int vector_is(const binfmt_resolved *r, const char *const want[])
{
	unsigned i;
	for (i = 0; want[i]; i++)
		if (i >= r->argc || strcmp(r->argv[i], want[i]))
			return 0;
	return i == r->argc;
}

static void test_resolve(void)
{
	static unsigned char elf[32];
	binfmt_resolved r;
	binfmt_diag d;
	char *argv2[] = { "script", "one", NULL };
	char *argv1[] = { "prog", NULL };

	elf_head(elf, 2, 62, 2);
	fs_n = 0;
	put_bytes("/bin/true", elf, sizeof elf);
	put("/s1", "#!/bin/true\n");
	put("/s2", "#!/bin/true -x\n");
	put("/s3", "#!/s1\n");
	put("/loop", "#!/loop\n");
	put("/gone", NULL);
	put("/text", "not a program\n");

	{
		static const char *want[] = { "prog", NULL };
		ck("a direct ELF is entered unchanged",
		   binfmt_resolve("/bin/true", argv1, fixture_head, NULL, &r, &d)
		   == binfmt_ok && r.kind == binfmt_elf && r.depth == 0 &&
		   !strcmp(r.file, "/bin/true") && vector_is(&r, want));
	}
	{
		static const char *want[] = { "/bin/true", "/s1", "one", NULL };
		ck("one hop rebuilds the vector the kernel's way",
		   binfmt_resolve("/s1", argv2, fixture_head, NULL, &r, &d)
		   == binfmt_ok && r.kind == binfmt_elf && r.depth == 1 &&
		   !strcmp(r.file, "/bin/true") && vector_is(&r, want));
	}
	{
		static const char *want[] = { "/bin/true", "-x", "/s2", "one", NULL };
		ck("the interpreter's argument sits between it and the file",
		   binfmt_resolve("/s2", argv2, fixture_head, NULL, &r, &d)
		   == binfmt_ok && vector_is(&r, want));
	}
	{
		static const char *want[] = { "/bin/true", "/s1", "/s3", "one", NULL };
		ck("two hops stack in the order they were taken",
		   binfmt_resolve("/s3", argv2, fixture_head, NULL, &r, &d)
		   == binfmt_ok && r.depth == 2 && vector_is(&r, want));
	}
	ck("a #! cycle is refused by the depth limit",
	   binfmt_resolve("/loop", argv2, fixture_head, NULL, &r, &d)
	   == binfmt_err_depth);
	ck("an unreadable file is a read failure, not a format",
	   binfmt_resolve("/gone", argv2, fixture_head, NULL, &r, &d)
	   == binfmt_err_read);
	ck("a text file resolves as unknown rather than failing",
	   binfmt_resolve("/text", argv2, fixture_head, NULL, &r, &d)
	   == binfmt_ok && r.kind == binfmt_unknown);
}

/* The limit is four hops, and the boundary is where a limit is worth testing:
 * four must run and five must not. */
static void test_depth_boundary(void)
{
	static unsigned char elf[32];
	static char paths[8][8];
	static char heads[8][32];
	binfmt_resolved r;
	binfmt_diag d;
	char *argv1[] = { "prog", NULL };
	unsigned n;

	elf_head(elf, 2, 62, 2);
	for (n = 1; n <= BINFMT_MAX_DEPTH + 1; n++) {
		unsigned i;
		fs_n = 0;
		put_bytes("/bin/true", elf, sizeof elf);
		/* /h1 -> /h2 -> ... -> /h<n> -> /bin/true, so n hops. */
		for (i = 1; i <= n; i++) {
			snprintf(paths[i], sizeof paths[i], "/h%u", i);
			if (i == n)
				snprintf(heads[i], sizeof heads[i], "#!/bin/true\n");
			else
				snprintf(heads[i], sizeof heads[i], "#!/h%u\n", i + 1);
			put(paths[i], heads[i]);
		}
		if (n <= BINFMT_MAX_DEPTH)
			ck("a chain within the limit resolves",
			   binfmt_resolve("/h1", argv1, fixture_head, NULL, &r, &d)
			   == binfmt_ok && r.depth == n);
		else
			ck("the first chain past the limit is refused",
			   binfmt_resolve("/h1", argv1, fixture_head, NULL, &r, &d)
			   == binfmt_err_depth);
	}
}

/* The host's command-line quoting. Two things are checked on every case: the
 * bytes produced, and that the length reported when sizing with no buffer
 * equals the length written with one. The second is not pedantry -- the first
 * version of this function sized by walking with its store disabled, and with
 * the store went the increment that advanced through the argument, so sizing
 * never returned at all. A caller that sizes then fills has to be given the
 * same answer twice. */
static void quoted_is(const char *arg, const char *want)
{
	char got[256];
	size_t sized = exec_quote_arg(arg, NULL, 0);
	size_t wrote = exec_quote_arg(arg, got, sizeof got);
	char what[160];

	snprintf(what, sizeof what, "quoting %s", arg);
	ck(what, wrote == sized && !strcmp(got, want));
}

static void test_quoting(void)
{
	quoted_is("plain", "\"plain\"");
	quoted_is("has space", "\"has space\"");
	quoted_is("", "\"\"");
	quoted_is("C:\\tmp\\stub.exe", "\"C:\\tmp\\stub.exe\"");
	/* A backslash only escapes when a quote follows it, so a run before a
	 * quote doubles and a run anywhere else does not. */
	quoted_is("say \"hi\"", "\"say \\\"hi\\\"\"");
	quoted_is("back\\\\slash", "\"back\\\\slash\"");
	quoted_is("trailing\\", "\"trailing\\\\\"");
	quoted_is("odd\\\"", "\"odd\\\\\\\"\"");
}

int main(void)
{
	printf("  WP-41 unit: the branch and the chain\n");
	test_classify();
	test_shebang();
	test_resolve();
	test_depth_boundary();
	test_quoting();
	if (failures)
		printf("  %d check(s) failed\n", failures);
	else
		printf("  all checks passed\n");
	return failures ? 1 : 0;
}
