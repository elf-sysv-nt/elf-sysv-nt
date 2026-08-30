/* WP-41: the executable-format branch.
 *
 * Linux keeps this table in the kernel and calls it binfmt. There is no kernel
 * here, so it moves into the spawn path of the library Cygwin already funnels
 * every host call through. Moving the table changes where it lives and nothing
 * else: the same leading bytes decide the same three ways.
 *
 * The three outcomes are ELF, a `#!` script, and everything the host still
 * owns. They are decided from one read of the file's leading bytes, by one
 * classifier, in one order -- DR-0027 -- rather than by an ELF test bolted in
 * front of a `#!` test that was already there. The two would not disagree
 * today, since the first byte alone separates 0x7f, '#' and 'M'; they would
 * begin to disagree the first time either side grew a second condition, and
 * the shape that prevents that is worth more than the case it prevents.
 *
 * Everything in this header is pure. Nothing opens a file, allocates, or reads
 * ambient state: the caller supplies the leading bytes through a reader
 * callback, and the resolved chain is written into caller-owned storage. That
 * is what lets the classifier be certified against fixtures and fuzzed against
 * malformed heads with no process being spawned anywhere.
 */
#ifndef ELFSYSV_LOADER_EXEC_BINFMT_H
#define ELFSYSV_LOADER_EXEC_BINFMT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The bytes a classification may look at. Linux reads BINPRM_BUF_SIZE = 256
 * and decides from those alone, and a `#!` line that does not end inside them
 * is refused rather than truncated. Both numbers are the kernel's, and a
 * script that works on Linux has to work here, so neither is ours to widen. */
#define BINFMT_HEAD_MAX   256

/* How many interpreter hops a chain may take. Linux permits four, counted the
 * same way: the fourth `#!` in a row is refused. DR-0027. */
#define BINFMT_MAX_DEPTH  4

/* Bounds on the storage the resolver fills. A path longer than this is refused
 * rather than truncated, since a truncated path names a different file. */
#define BINFMT_PATH_MAX   4096
#define BINFMT_ARGV_MAX   1024

typedef enum {
	binfmt_elf = 0,   /* ELF64 little-endian x86-64, ET_EXEC or ET_DYN */
	binfmt_script,    /* `#!' with an interpreter on the line */
	binfmt_host,      /* an MZ image: the host's own spawn path keeps it */
	binfmt_unknown    /* nothing recognized it */
} binfmt_kind;

typedef enum {
	binfmt_ok = 0,
	binfmt_err_arg,           /* a precondition on the arguments was violated */
	binfmt_err_read,          /* the reader callback refused the file */
	binfmt_err_elf_class,     /* ELF, but not ELF64 little-endian x86-64 */
	binfmt_err_elf_type,      /* ELF64 x86-64, but neither ET_EXEC nor ET_DYN */
	binfmt_err_script_empty,  /* `#!' with no interpreter on the line */
	binfmt_err_script_long,   /* the `#!' line does not end inside the head */
	binfmt_err_depth,         /* the interpreter chain exceeded BINFMT_MAX_DEPTH */
	binfmt_err_space          /* a path or the rebuilt vector does not fit */
} binfmt_err;

typedef struct {
	binfmt_err  code;
	const char *field;   /* the header member or line element at fault */
	char        msg[256];
} binfmt_diag;

/* One classification. interp and arg are filled for binfmt_script only, and
 * arg is present only when has_arg is set -- an absent argument and an empty
 * one are different things, and the caller must not collapse them. e_type is
 * filled for binfmt_elf only. */
typedef struct {
	binfmt_kind kind;
	char        interp[BINFMT_HEAD_MAX];
	char        arg[BINFMT_HEAD_MAX];
	int         has_arg;
	uint16_t    e_type;
} binfmt_probe;

/* Classify from the leading bytes alone. head[0..n) is what the reader
 * returned, and n may be anything from zero to BINFMT_HEAD_MAX; a short read
 * is not an error, it is simply a file too short to be any of the three, which
 * classifies as binfmt_unknown. A malformed `#!' line is an error, because the
 * file did announce itself and then failed to name an interpreter.
 *
 * On binfmt_ok, out->kind holds the verdict and diag is untouched. On a
 * nonzero code, out->kind is binfmt_unknown and diag names the fault. */
binfmt_err binfmt_classify(const unsigned char *head, size_t n,
                           binfmt_probe *out, binfmt_diag *diag);

/* Reads up to n leading bytes of path into buf. Returns the count, which may
 * be zero, or -1 if the file cannot be read at all. The resolver makes no
 * other call against the file system, so a test supplies a table and the
 * spawn path supplies a real open. */
typedef long (*binfmt_head_fn)(void *ctx, const char *path,
                               unsigned char *buf, size_t n);

/* A resolved chain. file is what is finally executed and argv is the vector it
 * is entered with, both already accounting for every interpreter hop. The argv
 * entries point either into store or into the caller's original vector, so the
 * original must outlive this structure. */
typedef struct {
	binfmt_kind  kind;
	unsigned     depth;                 /* interpreter hops taken; 0 for a direct hit */
	const char  *file;                  /* points into store */
	unsigned     argc;
	const char  *argv[BINFMT_ARGV_MAX];
	uint16_t     e_type;                /* for binfmt_elf */
	char         store[BINFMT_PATH_MAX];
	size_t       store_used;
} binfmt_resolved;

/* Follow the chain from path, reading each file's head through read_head, and
 * report where it lands and with what vector.
 *
 * At each `#!' hop the vector is rebuilt the way the kernel rebuilds it: the
 * leading element is dropped and the interpreter, its optional single
 * argument, and the path of the file that named it are pushed in front of what
 * is left. The path then becomes the interpreter's, and the next read is of
 * that file. A chain that takes more than BINFMT_MAX_DEPTH hops is refused
 * with binfmt_err_depth, which is also what a `#!' cycle produces -- the limit
 * is the cycle detector, exactly as it is on Linux.
 *
 * argv must be NULL-terminated and must have at least one element. No pointer
 * argument may be null. */
binfmt_err binfmt_resolve(const char *path, char *const argv[],
                          binfmt_head_fn read_head, void *ctx,
                          binfmt_resolved *out, binfmt_diag *diag);

/* Stable names for test output and diagnostics. */
const char *binfmt_err_name(binfmt_err code);
const char *binfmt_kind_name(binfmt_kind kind);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_EXEC_BINFMT_H */
