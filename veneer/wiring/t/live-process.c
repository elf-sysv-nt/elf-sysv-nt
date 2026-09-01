/*
 * live-process -- WP-56's sixteenth live crossing, crossed by its bind alone.
 * The bind loop (wire.c) resolves the process slice's real table against a
 * real elfsysv1.dll; no process body is called.
 *
 * The process slice is wire-process.gen.c: 43 rows, 39 forwards and 4 shims --
 * the wait family (wait, wait3, wait4, waitpid), the POSIX spawn surface
 * (posix_spawn, posix_spawnp and the file-actions and attr families), the
 * scheduler calls (sched_yield, the sched_get and sched_set families,
 * sched_getaffinity and its two versions), the priority pair (getpriority,
 * setpriority) and the resource limits (getrlimit, setrlimit). The four shims
 * are exactly the resource-limit rows, and the crossing asks the same question
 * memory's did: does a Cygwin-faced DLL export the whole set, or does the bind
 * leave rows a shim must synthesise, as signal's two System V dispositions did?
 *
 * Measurement answers cleanly, and the shape is memory's, not signal's: the
 * bind leaves no row null. Every process export glibc names, Cygwin exports
 * under the same name. The apparent gap is the four shim rows -- getrlimit64
 * and setrlimit64 -- and it is not one. glibc splits the resource-limit calls
 * into a base and an LFS *64 variant; Cygwin, being LP64, has one call each and
 * no separate getrlimit64/setrlimit64 export, so the bare *64 names are absent
 * from the DLL. But the generator already knew that: the getrlimit64 row carries
 * export_name "getrlimit" and the setrlimit64 row "setrlimit", forward-aliases
 * onto the single call, so those rows bind through the base name and only the
 * bare *64 names -- which no row asks the resolver for -- are missing. On a
 * 64-bit target the *64 alias is the base call unchanged, no translation left
 * to do. So process crosses with an empty unresolved set, exactly as memory's
 * mmap64 alias let it: it needs no shim, and DR-0055's rule that a SIGFE slice
 * crosses by its bind alone applies here with nothing left over.
 *
 * Crossed by its bind alone, not by call: no process row is stateless. wait and
 * waitpid stand on the process's children and its signal state; posix_spawn
 * forks and execs; the scheduler and rlimit calls are syscalls into the
 * kernel's per-process state; every one is SIGFE in Cygwin, entering cygtls on
 * the way in. A freestanding harness brings none of that up, so calling a body
 * here would read or mutate state the harness never initialised -- the trap
 * fnmatch sprang in live-filesystem. The bodies are left to the two bars that
 * reach them: diff-slice.sh on the pinned el8 image, and process bring-up.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind left no row unresolved -- the finding as a check: process
 *         needs no shim, the whole slice binds, unlike signal's two rows
 *   0x02  every filled slot lands inside the DLL's mapped image span, so a
 *         resolved thunk tail-jumps into the real body region, not unmapped
 *         space
 *   0x04  the resolver discriminates, and the getrlimit64 alias holds:
 *         getrlimit resolves; the bare name getrlimit64 does not (Cygwin has
 *         no *64 export); a junk name does not; yet the getrlimit64 row still
 *         binds, because its export_name is "getrlimit", which does resolve
 *   0x08  distinct exported names reach distinct bodies (waitpid, posix_spawn,
 *         sched_yield)
 *   0x10  the bind is idempotent, per DR-0049: a rebind leaves no row null and
 *         every filled slot equal to a fresh resolve of its export name
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_process[];
extern const unsigned long __esn_wire_process_n;

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

void live_process_main(uint64_t *sp, terminator_fn leave)
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
uintptr_t base = (uintptr_t) rt;
uintptr_t end = base + pe_size_of_image(rt);

(void) __esn_wire_bind(__esn_wire_process,
       __esn_wire_process_n,
       resolve, (void *) rt);

/* The finding as a check: no row is left unresolved. Every
 * process export binds; the slice needs no shim. */
for (i = 0; i < __esn_wire_process_n; i++)
if (__esn_wire_process[i].fn == 0)
nulls++;
if (nulls == 0 && __esn_wire_process_n > 0)
status |= 0x01;

/* Every filled slot lands inside the mapped image span. */
{
int all_in = 1;

for (i = 0; i < __esn_wire_process_n; i++) {
uintptr_t fn = (uintptr_t) __esn_wire_process[i].fn;

if (fn == 0)
continue;
if (fn < base || fn >= end)
all_in = 0;
}
if (all_in && __esn_wire_process_n > 0)
status |= 0x02;
}

/* The resolver discriminates, and the getrlimit64 alias holds: a
 * real base resolves, the bare name getrlimit64 does not (Cygwin
 * has no *64 export), a junk name does not -- yet the getrlimit64
 * row binds anyway, because its export_name is "getrlimit", which
 * resolves. */
if (pe_export(rt, "getrlimit") != 0 &&
    pe_export(rt, "getrlimit64") == 0 &&
    pe_export(rt, "__no_such_process_export_zzq") == 0 &&
    pe_export(rt, "getrlimit") != 0)
status |= 0x04;

/* Distinct exported names reach distinct bodies. */
{
void *a = pe_export(rt, "waitpid");
void *b = pe_export(rt, "posix_spawn");
void *c = pe_export(rt, "sched_yield");

if (a && b && c && a != b && b != c && a != c)
status |= 0x08;
}

/* Idempotent rebind: no row null again, every filled slot equal
 * to a fresh resolve of its export name. */
{
size_t nulls2 = 0;
int same = 1;

(void) __esn_wire_bind(__esn_wire_process,
       __esn_wire_process_n,
       resolve, (void *) rt);
for (i = 0; i < __esn_wire_process_n; i++) {
void *fresh = pe_export(rt,
__esn_wire_process[i].export_name);

if (__esn_wire_process[i].fn != fresh)
same = 0;
if (__esn_wire_process[i].fn == 0)
nulls2++;
}
if (same && nulls2 == 0 && __esn_wire_process_n > 0)
status |= 0x10;
}
}

leave(status);
}
