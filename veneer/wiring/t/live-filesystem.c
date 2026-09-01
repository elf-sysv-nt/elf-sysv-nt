/*
 * live-filesystem -- WP-56's fourteenth live crossing, and the second crossed
 * by its bind alone. The bind loop (wire.c) resolves the filesystem slice's
 * real table against a real elfsysv1.dll; no filesystem body is called.
 *
 * The filesystem slice is wire-filesystem.gen.c: 103 rows, 67 forwards and 36
 * shims. DR-0055 crosses a SIGFE slice by its bind alone and lists the slices
 * that inherit the rule, omitting filesystem on the reading that filesystem
 * carries a callable pure row. Measurement refutes that reading. filesystem's
 * only NOSIGFE, argument-only rows -- fnmatch, alphasort, versionsort -- look
 * pure but are not: fnmatch consults the locale's ctype and collation, and
 * alphasort/versionsort run strcoll/strverscmp over a struct dirent, so each
 * stands on locale or reent state a freestanding harness never brings up.
 * Calling fnmatch here proved it directly: built byte-identical and run three
 * times it returned three different verdicts (a five-check status of 30, then
 * 5, then 31), the signature of a body reading uninitialised state. NOSIGFE
 * names the calling convention a thunk needs, not whether the body behind it
 * stands on its own -- DR-0055's own words -- and filesystem is a slice with
 * NOSIGFE rows and no stateless one. So it crosses by its bind alone, as stdio
 * did, and its bodies are left to the two bars that reach them: diff-slice.sh
 * on the pinned el8 image, and process bring-up.
 *
 * The bind carries a finding of its own. Eleven rows do not resolve, and they
 * are exactly the rows a real shim must synthesise. Ten are the stat family:
 * glibc's versioned wrappers __xstat, __fxstat, __lxstat, __xmknod, their *at
 * forms and their *64 forms -- the (int version, ...) entry points el8 binaries
 * import. Cygwin has no such ABI: it exports plain stat, fstat, lstat, fstatat,
 * mknod and mknodat (all present in the DLL), and being LP64 it has no separate
 * *64 symbol. The eleventh is getdirentries, which Cygwin exports neither as
 * itself nor as getdents. The generator left the glibc name in each row's
 * export_name as a placeholder; a real shim body drops glibc's version
 * argument, translates the struct stat layout and calls the Cygwin function
 * (the *64 rows onto the same call), and getdirentries is composed from
 * readdir/seekdir/telldir or left a documented stub. Every other row -- every
 * forward, and the 25 shims whose export exists -- binds.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind left exactly the eleven stat-family and getdirentries rows
 *         unresolved and every other row filled -- the finding as a check: the
 *         null rows are exactly the set a shim must synthesise, no more, no less
 *   0x02  every filled slot lands inside the DLL's mapped image span, so a
 *         resolved thunk tail-jumps into the real body region, not unmapped
 *         space (the eleven null slots are not filled and are not checked)
 *   0x04  the resolver discriminates: chmod, a real forward, resolves; __xstat,
 *         a stat-family name Cygwin does not export, does not; and a junk name
 *         does not -- so the all-but-eleven result is a fact about the names
 *   0x08  distinct exported names reach distinct bodies (chmod, closedir, fnmatch)
 *   0x10  the bind is idempotent, per DR-0049: a rebind leaves the same eleven
 *         null and every filled slot equal to a fresh resolve of its name
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_filesystem[];
extern const unsigned long __esn_wire_filesystem_n;

/* The eleven rows that cannot bind against a Cygwin-faced DLL: glibc's
 * versioned stat/mknod wrappers and getdirentries. A real shim body reaches
 * Cygwin's plain stat/fstat/lstat/fstatat/mknod/mknodat (or, for getdirentries,
 * composes readdir/seekdir/telldir); the generator left the glibc name as a
 * placeholder, so these are precisely the rows the bind refuses. */
static const char *const expected_null[] = {
"__fxstat", "__fxstat64", "__fxstatat", "__fxstatat64",
"__lxstat", "__lxstat64", "__xmknod", "__xmknodat",
"__xstat", "__xstat64", "getdirentries", 0
};

static int str_eq(const char *a, const char *b)
{
while (*a && *a == *b) {
a++;
b++;
}
return *a == 0 && *b == 0;
}

static int is_expected_null(const char *name)
{
int i;
for (i = 0; expected_null[i]; i++)
if (str_eq(name, expected_null[i]))
return 1;
return 0;
}

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

/* SizeOfImage from the PE32+ optional header: opt starts past the DOS stub,
 * the PE signature and the 20-byte file header, and SizeOfImage sits 56 bytes
 * into it -- the same header this file's pe_export already walks. */
static uint32_t pe_size_of_image(const uint8_t *base)
{
uint32_t lfanew = rd32(base + 0x3C);
const uint8_t *opt = base + lfanew + 4 + 20;

return rd32(opt + 56);
}

void live_filesystem_main(uint64_t *sp, terminator_fn leave)
{
uint64_t status = 0;
uint64_t *p;
const uint8_t *rt = 0;

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
size_t i, nulls = 0;
int stray = 0;
uintptr_t base = (uintptr_t) rt;
uintptr_t end = base + pe_size_of_image(rt);

(void) __esn_wire_bind(__esn_wire_filesystem,
       __esn_wire_filesystem_n,
       resolve, (void *) rt);

/* The finding as a check: every unresolved row is one of the
 * eleven the stat family and getdirentries need a shim for, and
 * every other row resolved. */
for (i = 0; i < __esn_wire_filesystem_n; i++) {
if (__esn_wire_filesystem[i].fn == 0) {
nulls++;
if (!is_expected_null(
__esn_wire_filesystem[i].export_name))
stray = 1;
}
}
if (nulls == 11 && !stray && __esn_wire_filesystem_n > 0)
status |= 0x01;

/* Every filled slot lands inside the mapped image span. The
 * eleven null slots are expected null and are skipped. */
{
int all_in = 1;

for (i = 0; i < __esn_wire_filesystem_n; i++) {
uintptr_t fn =
(uintptr_t) __esn_wire_filesystem[i].fn;

if (fn == 0)
continue;
if (fn < base || fn >= end)
all_in = 0;
}
if (all_in && __esn_wire_filesystem_n > 0)
status |= 0x02;
}

/* The resolver discriminates: a real forward resolves, a
 * stat-family name Cygwin does not export does not, and a junk
 * name does not. */
if (pe_export(rt, "chmod") != 0 &&
    pe_export(rt, "__xstat") == 0 &&
    pe_export(rt, "__no_such_filesystem_export_zzq") == 0)
status |= 0x04;

/* Distinct exported names reach distinct bodies. */
{
void *a = pe_export(rt, "chmod");
void *b = pe_export(rt, "closedir");
void *c = pe_export(rt, "fnmatch");

if (a && b && c && a != b && b != c && a != c)
status |= 0x08;
}

/* Idempotent rebind: the same eleven null again, every filled
 * slot equal to a fresh resolve of its name. */
{
size_t nulls2 = 0;
int same = 1;

(void) __esn_wire_bind(__esn_wire_filesystem,
       __esn_wire_filesystem_n,
       resolve, (void *) rt);
for (i = 0; i < __esn_wire_filesystem_n; i++) {
void *fresh = pe_export(rt,
__esn_wire_filesystem[i].export_name);

if (__esn_wire_filesystem[i].fn != fresh)
same = 0;
if (__esn_wire_filesystem[i].fn == 0)
nulls2++;
}
if (same && nulls2 == 11 && __esn_wire_filesystem_n > 0)
status |= 0x10;
}
}

leave(status);
}
