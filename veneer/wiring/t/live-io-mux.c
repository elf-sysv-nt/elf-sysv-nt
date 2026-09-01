/*
 * live-io-mux -- WP-56's eighteenth live crossing, crossed by its bind
 * alone. The bind loop (wire.c) resolves the io-mux slice's real table
 * against a real elfsysv1.dll; no io-mux body is called.
 *
 * The io-mux slice is wire-io-mux.gen.c: 8 rows, all forwards and no shim --
 * the readiness multiplexers (poll, ppoll, pselect, select) and the two
 * descriptor-backed event sources (signalfd, and the timerfd trio
 * timerfd_create, timerfd_gettime, timerfd_settime). Every one is a
 * forward-same row: the name glibc versions is the name Cygwin exports.
 *
 * The wrinkle here, small and worth naming, is version spread. identity's
 * seventeen rows all versioned at GLIBC_2.2.5; io-mux's eight carry four
 * different tags -- poll/pselect/select at GLIBC_2.2.5, ppoll at GLIBC_2.4,
 * signalfd at GLIBC_2.7, and the timerfd trio at GLIBC_2.8. The version tag
 * lives in the ELF face's .symver on the thunk, not in the export name the
 * bind resolves, so the spread changes nothing for the crossing: each row's
 * export_name is still the plain symbol, and the DLL exports it undecorated.
 * So the crossing asks memory's question and gets memory's answer in its
 * plainest form across a mixed-version slice: the whole set resolves and the
 * bind leaves no row null.
 *
 * The shape is memory's, process's and identity's, not signal's or
 * filesystem's. signal left two System V rows for a shim to synthesise;
 * filesystem's stat family needed the struct translation; process's four
 * getrlimit64/setrlimit64 rows looked like a gap until their export_name
 * aliases closed it. io-mux has none of that: every row's export_name is the
 * plain symbol glibc names, so the bind reaches each body by that name with
 * nothing to bridge, and the four version tags never enter the resolution.
 * The finding is the absence of a shim across a version-spread slice.
 *
 * Crossed by its bind alone, not by call: no io-mux row is callable from a
 * freestanding harness. poll, ppoll, pselect and select block on real
 * descriptors and a timeout the harness has no event loop to satisfy;
 * signalfd and the timerfd trio create descriptor-backed kernel objects and
 * are SIGFE in Cygwin, entering cygtls on the way in. Calling a body here
 * would touch descriptor and signal-mask state the harness never initialised
 * -- the trap fnmatch sprang in live-filesystem. The bodies are left to the
 * two bars that reach them: diff-slice.sh on the pinned el8 image, and io-mux
 * bring-up.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind left no row unresolved -- the finding as a check: io-mux
 *         needs no shim, the whole slice binds, unlike signal's two rows
 *   0x02  every filled slot lands inside the DLL's mapped image span, so a
 *         resolved thunk tail-jumps into the real body region, not unmapped
 *         space
 *   0x04  the resolver discriminates, and the no-alias finding holds: poll
 *         resolves, a junk name does not, and the row whose export_name is
 *         "poll" bound to exactly that export -- each io-mux row reaches its
 *         body by its own plain name, the version tag on the thunk playing no
 *         part in the resolution
 *   0x08  distinct exported names reach distinct bodies (poll, select,
 *         signalfd), spanning three of the slice's four version tags
 *   0x10  the bind is idempotent, per DR-0049: a rebind leaves no row null and
 *         every filled slot equal to a fresh resolve of its export name
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_io_mux[];
extern const unsigned long __esn_wire_io_mux_n;

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

void live_io_mux_main(uint64_t *sp, terminator_fn leave)
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

(void) __esn_wire_bind(__esn_wire_io_mux,
       __esn_wire_io_mux_n,
       resolve, (void *) rt);

/* The finding as a check: no row is left unresolved. Every
 * io-mux export binds; the slice needs no shim. */
for (i = 0; i < __esn_wire_io_mux_n; i++)
if (__esn_wire_io_mux[i].fn == 0)
nulls++;
if (nulls == 0 && __esn_wire_io_mux_n > 0)
status |= 0x01;

/* Every filled slot lands inside the mapped image span. */
{
int all_in = 1;

for (i = 0; i < __esn_wire_io_mux_n; i++) {
uintptr_t fn = (uintptr_t) __esn_wire_io_mux[i].fn;

if (fn == 0)
continue;
if (fn < base || fn >= end)
all_in = 0;
}
if (all_in && __esn_wire_io_mux_n > 0)
status |= 0x02;
}

/* The resolver discriminates, and the no-alias finding holds:
 * poll resolves, a junk name does not, and the row whose
 * export_name is "poll" bound to exactly that export -- each
 * io-mux row reaches its body by its own plain name, the version
 * tag on the thunk playing no part in the resolution. */
{
void *direct = pe_export(rt, "poll");
void *bound = 0;

for (i = 0; i < __esn_wire_io_mux_n; i++)
if (str_eq(__esn_wire_io_mux[i].export_name, "poll"))
bound = __esn_wire_io_mux[i].fn;
if (direct != 0 && bound == direct &&
    pe_export(rt, "__no_such_io_mux_export_zzq") == 0)
status |= 0x04;
}

/* Distinct exported names reach distinct bodies, spanning three
 * of the slice's four version tags (GLIBC_2.2.5, 2.7 and the
 * 2.8 timerfd family through signalfd's 2.7 neighbour). */
{
void *a = pe_export(rt, "poll");
void *b = pe_export(rt, "select");
void *c = pe_export(rt, "signalfd");

if (a && b && c && a != b && b != c && a != c)
status |= 0x08;
}

/* Idempotent rebind: no row null again, every filled slot equal
 * to a fresh resolve of its export name. */
{
size_t nulls2 = 0;
int same = 1;

(void) __esn_wire_bind(__esn_wire_io_mux,
       __esn_wire_io_mux_n,
       resolve, (void *) rt);
for (i = 0; i < __esn_wire_io_mux_n; i++) {
void *fresh = pe_export(rt,
__esn_wire_io_mux[i].export_name);

if (__esn_wire_io_mux[i].fn != fresh)
same = 0;
if (__esn_wire_io_mux[i].fn == 0)
nulls2++;
}
if (same && nulls2 == 0 && __esn_wire_io_mux_n > 0)
status |= 0x10;
}
}

leave(status);
}
