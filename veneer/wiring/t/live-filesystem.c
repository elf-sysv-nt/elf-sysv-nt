/*
 * live-filesystem -- WP-56's fourteenth live crossing. The bind loop resolves
 * the filesystem slice's real table against a real elfsysv1.dll, and one of the
 * slice's generated thunks -- fnmatch -- is called for real on NT.
 *
 * The filesystem slice is wire-filesystem.gen.c: 103 rows, 67 forwards and 36
 * shims, every row naming an elfsysv1.dll export. Its bind check is math's,
 * stdlib's, sockets's, posix's, time's, misc's and stdio's shape -- every row
 * must resolve, missing == 0.
 *
 * DR-0055 fixed the rule for a SIGFE slice with no callable pure row: it
 * crosses by its bind alone. That record lists the SIGFE-heavy slices that
 * inherit the rule -- memory, signal, process, identity, io-mux, threads,
 * regex, syslog, sysv-ipc, io, system -- and pointedly omits filesystem,
 * because filesystem carries pure NOSIGFE rows a freestanding harness can call.
 * fnmatch is one: runtime/exports/cygwin-exports.tsv marks it NOSIGFE, and its
 * body is a byte-pattern matcher standing on no reent, no cygheap, no clock and
 * no kernel -- for an ASCII pattern in the default C locale it is the pure
 * comparison string's ffs and misc's insque were. Its arguments are two
 * const char * the specimen owns and an int flag word, no libc struct whose two
 * sides might disagree, the same discipline every earlier crossing kept. The
 * rest of the slice -- open, stat, the readdir and xattr families -- reads the
 * fd table and the cygheap and is left for the differential and process
 * bring-up.
 *
 * fnmatch is w00028; see wire-filesystem.gen.s for the index -> name mapping.
 * It is declared here with fnmatch's own prototype and called directly by its
 * generated label. System V, the convention this whole unit is compiled to, is
 * the shape a real ELF caller reaches it through too.
 *
 * What fnmatch adds that the earlier crossed rows did not is a finding. The
 * forward is wired forward-same, but the flag word is not value-preserving:
 * el8's <fnmatch.h> numbers FNM_PATHNAME 0x01 and FNM_NOESCAPE 0x02, while
 * Cygwin's numbers them the other way round -- FNM_NOESCAPE 0x01, FNM_PATHNAME
 * 0x02. FNM_PERIOD (0x04), FNM_LEADING_DIR (0x08) and FNM_CASEFOLD (0x10) agree,
 * and FNM_NOMATCH is 1 on both. So the flagless call crosses value-preserving,
 * but an el8 caller passing FNM_PATHNAME (0x01) reaches a body that reads 0x01
 * as FNM_NOESCAPE: the forward misdelivers it, and fnmatch needs a shim that
 * swaps the two low flag bits. This crossing shows both halves against the real
 * DLL: the flagless contract holds, and the body reads 0x01 as NOESCAPE and
 * 0x02 as PATHNAME, which is Cygwin's numbering and not el8's.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind resolved every row of the 103-row table (missing 0)
 *   0x02  fnmatch("*.c", "foo.c", 0) == 0 -- the flagless forward matches
 *   0x04  fnmatch("*.c", "foo.h", 0) == FNM_NOMATCH -- and rejects, and
 *         FNM_NOMATCH is 1 on both libcs, so the return crosses too
 *   0x08  fnmatch("*", "a/b", 0x02) == FNM_NOMATCH -- 0x02 is PATHNAME in the
 *         body, so '*' is stopped at '/'
 *   0x10  fnmatch("*", "a/b", 0x01) == 0 -- 0x01 is NOESCAPE in the body, not
 *         PATHNAME, so '*' crosses '/': el8's FNM_PATHNAME does not reach the
 *         body as PATHNAME, the finding this crossing earns
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_filesystem[];
extern const unsigned long __esn_wire_filesystem_n;

/* The wired thunk this specimen calls directly, by its generated label.
 * fnmatch takes two const char * and an int flag word and returns an int,
 * exactly its real prototype, without reaching into the very libc under test. */
extern int w00028(const char *pattern, const char *string, int flags); /* fnmatch */

static uint16_t rd16(const uint8_t *p)
{
return (uint16_t)(p[0] | ((uint16_t) p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
return p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
       ((uint32_t) p[3] << 24);
}

static int name_is(const uint8_t *p, const char *want)
{
while (*want && *p == (uint8_t) *want) {
p++;
want++;
}
return *want == 0 && *p == 0;
}

/* Resolve one export by name from a loaded PE image -- the same walk
 * runtime/face/t/elfcall.c uses, adapted to wire.h's resolver shape so it
 * can stand in for the runtime's eventual GetProcAddress callback. */
static void *pe_export(const uint8_t *base, const char *name)
{
uint32_t lfanew, nnames, i;
const uint8_t *opt, *dir;

if (rd16(base) != 0x5A4D)
return 0;
lfanew = rd32(base + 0x3C);
if (rd32(base + lfanew) != 0x00004550)
return 0;
opt = base + lfanew + 4 + 20;
if (rd16(opt) != 0x20B)
return 0;
if (rd32(opt + 108) < 1 || rd32(opt + 112) == 0)
return 0;
dir = base + rd32(opt + 112);
nnames = rd32(dir + 24);
for (i = 0; i < nnames; i++) {
if (name_is(base + rd32(base + rd32(dir + 32) + 4u * i), name)) {
uint16_t ord = rd16(base + rd32(dir + 36) + 2u * i);
return (void *)(base + rd32(base + rd32(dir + 28)
    + 4u * ord));
}
}
return 0;
}

static void *resolve(const char *export_name, void *ctx)
{
return pe_export((const uint8_t *) ctx, export_name);
}

void live_filesystem_main(uint64_t *sp, terminator_fn leave)
{
uint64_t status = 0;
uint64_t *p;
const uint8_t *rt = 0;
size_t missing;

/* Past argv and its terminator, past envp and its terminator. */
p = sp + 1 + sp[0] + 1;
while (*p)
p++;
p++;
for (; p[0]; p += 2) {
if (p[0] == AT_BASE) {
rt = (const uint8_t *)(uintptr_t) p[1];
break;
}
}

if (rt) {
missing = __esn_wire_bind(__esn_wire_filesystem,
  __esn_wire_filesystem_n,
  resolve, (void *) rt);
if (missing == 0 && __esn_wire_filesystem_n > 0)
status |= 0x01;

/* The flagless forward: matches, and rejects with FNM_NOMATCH,
 * whose value (1) is the same on both libcs. */
if (w00028("*.c", "foo.c", 0) == 0)
status |= 0x02;
if (w00028("*.c", "foo.h", 0) == 1)
status |= 0x04;

/* The flag word. 0x02 is PATHNAME in the body -- '*' cannot
 * cross '/', so "a/b" is rejected. */
if (w00028("*", "a/b", 0x02) == 1)
status |= 0x08;
/* 0x01 is NOESCAPE in the body, not PATHNAME -- '*' crosses
 * '/', so "a/b" matches. el8's FNM_PATHNAME (0x01) does not
 * reach the body as PATHNAME: the forward misdelivers the flag
 * word, and fnmatch needs a bit-swapping shim. */
if (w00028("*", "a/b", 0x01) == 0)
status |= 0x10;
}

leave(status);
}
