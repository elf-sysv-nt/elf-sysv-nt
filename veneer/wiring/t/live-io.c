/*
 * live-io -- WP-56's twenty-third live crossing, crossed by its bind alone.
 * The bind loop (wire.c) resolves the io slice's real table against a real
 * elfsysv1.dll; no I/O body is called.
 *
 * The io slice is wire-io.gen.c: 2 rows, both forwards and no shim -- libc's
 * scatter-gather I/O primitives `readv` and `writev`. Each is a forward-same
 * row versioned at GLIBC_2.2.5: the name glibc versions is the name Cygwin
 * exports, with no rename and no compat pair -- exactly one version tag on
 * each of two distinct names, the plainness sysv-ipc and syslog had.
 *
 * The one thing new here is how narrow the wired table is against the slice.
 * The demand ranking placed thirty-one names in this slice, down the three
 * I/O headers (`sys/uio.h`, `sys/sendfile.h`, `aio.h`); the table wires two.
 * Sixteen are the POSIX asynchronous-I/O family -- `aio_read`, `aio_write`,
 * `aio_error`, `aio_return`, `aio_suspend`, `aio_cancel`, `aio_fsync`,
 * `lio_listio` and their `*64` aliases -- which glibc ships in `librt.so.1`,
 * not `libc`, so they never enter libc's forward map at all; and thirteen
 * are stubs the classification marks with no body behind them -- `aio_init`,
 * the `preadv`/`pwritev` family and its `*64`/`*2` variants, the
 * `process_vm_readv`/`process_vm_writev` pair, and `sendfile`/`sendfile64` --
 * which gen-wire leaves to WP-53's ret. Neither absence is a shim shortfall:
 * the wired two are exactly libc's vectored read and write, and real-map.sh's
 * partition owns the rest. The finding is those two binding whole.
 *
 * Crossed by its bind alone, not by call: neither io row is safely callable
 * from a freestanding harness. `readv` and `writev` move bytes across file
 * descriptors through Cygwin's subsystem, which is SIGFE, entering cygtls on
 * the way in and reaching the fhandler layer; a freestanding harness never
 * brought that up -- the trap `fnmatch` sprang in live-filesystem. So the
 * specimen calls nothing and reads the table the bind filled. The bodies are
 * left to the two bars that reach them: diff-slice.sh's readv-writev.c over a
 * pipe on the pinned el8 image, and a hosted run.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind left no row unresolved -- the finding as a check: the two
 *         wired I/O primitives both bind, the slice needs no shim
 *   0x02  every filled slot lands inside the DLL's mapped image span, so a
 *         resolved thunk tail-jumps into the real body region, not unmapped
 *         space
 *   0x04  the resolver discriminates, and the plain-forward finding holds:
 *         `readv` resolves, a junk name does not, and the row whose
 *         export_name is `readv` bound to exactly that one export -- the
 *         single version tag on the thunk playing no part in the resolution
 *   0x08  distinct exported names reach distinct bodies (`readv`, `writev`),
 *         the vectored read and the vectored write
 *   0x10  the bind is idempotent, per DR-0049: a rebind leaves no row null and
 *         every filled slot equal to a fresh resolve of its export name
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_io[];
extern const unsigned long __esn_wire_io_n;

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

void live_io_main(uint64_t *sp, terminator_fn leave)
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

(void) __esn_wire_bind(__esn_wire_io,
       __esn_wire_io_n,
       resolve, (void *) rt);

/* The finding as a check: no row is left unresolved. Both
 * wired I/O primitives bind; the slice needs no shim. */
for (i = 0; i < __esn_wire_io_n; i++)
if (__esn_wire_io[i].fn == 0)
nulls++;
if (nulls == 0 && __esn_wire_io_n > 0)
status |= 0x01;

/* Every filled slot lands inside the mapped image span. */
{
int all_in = 1;

for (i = 0; i < __esn_wire_io_n; i++) {
uintptr_t fn = (uintptr_t) __esn_wire_io[i].fn;

if (fn == 0)
continue;
if (fn < base || fn >= end)
all_in = 0;
}
if (all_in && __esn_wire_io_n > 0)
status |= 0x02;
}

/* The resolver discriminates, and the plain-forward finding holds:
 * readv resolves, a junk name does not, and the single row whose
 * export_name is readv bound to exactly that one export -- each io
 * row reaches its body by its own plain name, the version tag on
 * the thunk playing no part in the resolution. */
{
void *direct = pe_export(rt, "readv");
size_t rows = 0, matched = 0;

for (i = 0; i < __esn_wire_io_n; i++)
if (str_eq(__esn_wire_io[i].export_name, "readv")) {
rows++;
if (__esn_wire_io[i].fn == direct)
matched++;
}
if (direct != 0 && rows == 1 && matched == 1 &&
    pe_export(rt, "__no_such_io_export_zzq") == 0)
status |= 0x04;
}

/* Distinct exported names reach distinct bodies (readv, writev),
 * the vectored read and the vectored write. */
{
void *a = pe_export(rt, "readv");
void *b = pe_export(rt, "writev");

if (a && b && a != b)
status |= 0x08;
}

/* Idempotent rebind: no row null again, every filled slot equal
 * to a fresh resolve of its export name. */
{
size_t nulls2 = 0;
int same = 1;

(void) __esn_wire_bind(__esn_wire_io,
       __esn_wire_io_n,
       resolve, (void *) rt);
for (i = 0; i < __esn_wire_io_n; i++) {
void *fresh = pe_export(rt,
__esn_wire_io[i].export_name);

if (__esn_wire_io[i].fn != fresh)
same = 0;
if (__esn_wire_io[i].fn == 0)
nulls2++;
}
if (same && nulls2 == 0 && __esn_wire_io_n > 0)
status |= 0x10;
}
}

leave(status);
}
