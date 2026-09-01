/*
 * live-threads -- WP-56's nineteenth live crossing, crossed by its bind
 * alone. The bind loop (wire.c) resolves the threads slice's real table
 * against a real elfsysv1.dll; no threads body is called.
 *
 * The threads slice is wire-threads.gen.c: 42 rows -- 41 forward-same and
 * one shim. The forwards are the POSIX threads surface pthread.h and
 * semaphore.h name (the attribute, condition-variable, mutex and scheduling
 * families, pthread_self, pthread_equal, pthread_exit, the cancellation
 * setters) and the four C11 threads.h entries (thrd_current, thrd_equal,
 * thrd_sleep, thrd_yield). Every one is a forward-same row: the name glibc
 * versions is the name Cygwin exports.
 *
 * The slice has two wrinkles, both worth naming and neither a System V gap.
 *
 * First, the one shim. Its thunk is __sigsetjmp (setjmp.h's versioned entry,
 * GLIBC_2.2.5), but its export_name is the plain sigsetjmp -- the shim is a
 * setjmp-family rename, the ELF face's __sigsetjmp resolved onto Cygwin's own
 * sigsetjmp export. So it binds by a plain export like the forwards do, not
 * like signal's two System V rows that had no Cygwin export at all and left
 * the table null for a shim to synthesise. threads' shim is a name the DLL
 * already carries; signal's were names it did not.
 *
 * Second, duplicate export names across versions. Six condition-variable
 * entries -- pthread_cond_broadcast, pthread_cond_destroy, pthread_cond_init,
 * pthread_cond_signal, pthread_cond_timedwait, pthread_cond_wait -- each carry
 * two rows, one at GLIBC_2.2.5 and one at GLIBC_2.3.2, the compat pair glibc
 * shipped when the condvar ABI changed. io-mux's version spread put four tags
 * on eight distinct names; threads puts two tags on one name twice over. The
 * tag lives in the ELF face's .symver on the thunk, not in the export name the
 * bind resolves, so both rows of a pair resolve to the single Cygwin export of
 * that plain name, and the bind fills both. The finding is that a compat pair
 * needs no reconciliation here: two thunks, two .symver tags, one export.
 *
 * So the crossing asks memory's question and gets memory's answer across a
 * slice with a rename and a compat pair: the whole set resolves and the bind
 * leaves no row null. No System V disposition as signal had, no struct
 * translation as filesystem's stat family had.
 *
 * Crossed by its bind alone, not by call: no threads row is callable from a
 * freestanding harness. Every pthread and thrd body is SIGFE in Cygwin,
 * entering the runtime's cygtls and its thread registry on the way in; a
 * mutex or condvar body reaches for the calling thread's cygtls the harness
 * never brought up, and sigsetjmp saves a signal mask against machinery the
 * harness never initialised -- the trap fnmatch sprang in live-filesystem.
 * The bodies are left to the two bars that reach them: diff-slice.sh on the
 * pinned el8 image, and threads bring-up.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind left no row unresolved -- the finding as a check: threads
 *         needs no System V synthesis, the whole slice binds, the rename and
 *         both halves of every compat pair among it
 *   0x02  every filled slot lands inside the DLL's mapped image span, so a
 *         resolved thunk tail-jumps into the real body region, not unmapped
 *         space
 *   0x04  the resolver discriminates, and the no-alias finding holds:
 *         pthread_self resolves, a junk name does not, and the row whose
 *         export_name is "pthread_self" bound to exactly that export -- each
 *         threads row reaches its body by its own plain name, the .symver on
 *         the thunk playing no part in the resolution
 *   0x08  distinct exported names reach distinct bodies (pthread_self,
 *         pthread_mutex_lock, and the renamed sigsetjmp)
 *   0x10  the bind is idempotent, per DR-0049: a rebind leaves no row null and
 *         every filled slot equal to a fresh resolve of its export name --
 *         including both rows of each compat pair, which resolve alike
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_threads[];
extern const unsigned long __esn_wire_threads_n;

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

static int str_eq(const char *a, const char *b)
{
while (*a && *a == *b) {
a++;
b++;
}
return *a == 0 && *b == 0;
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

void live_threads_main(uint64_t *sp, terminator_fn leave)
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

(void) __esn_wire_bind(__esn_wire_threads,
       __esn_wire_threads_n,
       resolve, (void *) rt);

/* The finding as a check: no row is left unresolved. Every
 * threads export binds -- the rename and both halves of every
 * compat pair among them; the slice needs no System V shim. */
for (i = 0; i < __esn_wire_threads_n; i++)
if (__esn_wire_threads[i].fn == 0)
nulls++;
if (nulls == 0 && __esn_wire_threads_n > 0)
status |= 0x01;

/* Every filled slot lands inside the mapped image span. */
{
int all_in = 1;

for (i = 0; i < __esn_wire_threads_n; i++) {
uintptr_t fn = (uintptr_t) __esn_wire_threads[i].fn;

if (fn == 0)
continue;
if (fn < base || fn >= end)
all_in = 0;
}
if (all_in && __esn_wire_threads_n > 0)
status |= 0x02;
}

/* The resolver discriminates, and the no-alias finding holds:
 * pthread_self resolves, a junk name does not, and the row whose
 * export_name is "pthread_self" bound to exactly that export --
 * each threads row reaches its body by its own plain name, the
 * .symver on the thunk playing no part in the resolution. */
{
void *direct = pe_export(rt, "pthread_self");
void *bound = 0;

for (i = 0; i < __esn_wire_threads_n; i++)
if (str_eq(__esn_wire_threads[i].export_name,
   "pthread_self"))
bound = __esn_wire_threads[i].fn;
if (direct != 0 && bound == direct &&
    pe_export(rt, "__no_such_threads_export_zzq") == 0)
status |= 0x04;
}

/* Distinct exported names reach distinct bodies: pthread_self, a
 * mutex body, and the renamed sigsetjmp (the shim's plain export,
 * not its __sigsetjmp thunk name). */
{
void *a = pe_export(rt, "pthread_self");
void *b = pe_export(rt, "pthread_mutex_lock");
void *c = pe_export(rt, "sigsetjmp");

if (a && b && c && a != b && b != c && a != c)
status |= 0x08;
}

/* Idempotent rebind: no row null again, every filled slot equal
 * to a fresh resolve of its export name -- both rows of each
 * compat pair among them, which resolve alike to one export. */
{
size_t nulls2 = 0;
int same = 1;

(void) __esn_wire_bind(__esn_wire_threads,
       __esn_wire_threads_n,
       resolve, (void *) rt);
for (i = 0; i < __esn_wire_threads_n; i++) {
void *fresh = pe_export(rt,
__esn_wire_threads[i].export_name);

if (__esn_wire_threads[i].fn != fresh)
same = 0;
if (__esn_wire_threads[i].fn == 0)
nulls2++;
}
if (same && nulls2 == 0 && __esn_wire_threads_n > 0)
status |= 0x10;
}
}

leave(status);
}
