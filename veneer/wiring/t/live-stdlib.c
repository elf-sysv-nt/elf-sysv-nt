/*
 * live-stdlib -- WP-56's fifth live crossing, and the first to carry a
 * struct-by-value return across the boundary. The bind loop resolves the
 * stdlib slice's real table against a real elfsysv1.dll, and six of the
 * slice's generated thunks are called for real on NT: the scalar abs
 * family and the struct-returning div family.
 *
 * The stdlib slice is 97 rows, all forwards, no shim (wire-stdlib.gen.c),
 * so its bind check is math's shape -- every row must resolve, missing == 0
 * -- not string's exactly-one-null. What stdlib adds over the earlier
 * crossings is the return shape. math, runtime and string all returned a
 * single scalar; div, ldiv and lldiv return div_t, ldiv_t and lldiv_t by
 * value, which the psABI hands back in registers (div_t in %rax, the two
 * wider ones in %rax:%rdx). A thunk is a bare tail jump, so it forwards
 * that register pair untouched; this specimen is the first to prove the
 * pair arrives intact through the bind loop and the branch.
 *
 * The NOSIGFE boundary string found still holds. abs, labs, llabs, div,
 * ldiv and lldiv are NOSIGFE (runtime/exports/cygwin-exports.tsv) and, like
 * string's ffs family, stand alone: each is pure arithmetic over its
 * arguments with no memory, reent or locale behind it, so it crosses in a
 * freestanding harness that never set up Cygwin's thread pointer. The rest
 * of stdlib -- strtol and the environment and the seeded generators, whose
 * bodies read reent -- is left for the same process bring-up the SIGFE
 * slices wait on, exactly as string left memcpy and strverscmp.
 *
 * The six thunks exercised (w00003 abs, w00029 labs, w00032 llabs, w00012
 * div, w00031 ldiv, w00033 lldiv; see wire-stdlib.gen.s for the index ->
 * name mapping) are .globl globals, declared here and called directly by
 * their generated label. System V, the convention this whole unit is
 * compiled to, is the shape a real ELF caller reaches them through too.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind resolved every row of the all-forward table (missing 0)
 *   0x02  the abs thunk (w00003) reaches the real body
 *   0x04  the labs thunk (w00029) reaches the real body
 *   0x08  the llabs thunk (w00032) reaches the real body
 *   0x10  the div, ldiv and lldiv thunks return their struct pair intact,
 *         and a second pass of the whole set still agrees after the
 *         crossings above
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_stdlib[];
extern const unsigned long __esn_wire_stdlib_n;

/* The wired thunks this specimen calls directly, by their generated label.
 * The struct types are declared locally to the psABI's shape rather than
 * pulled from stdlib.h -- the specimen is freestanding and must not reach
 * for the very libc it is testing -- but they match div_t/ldiv_t/lldiv_t
 * field for field, so the return class is identical. */
typedef struct { int quot; int rem; } sdiv_t;
typedef struct { long quot; long rem; } sldiv_t;
typedef struct { long long quot; long long rem; } slldiv_t;

extern int      w00003(int);                    /* abs   */
extern long     w00029(long);                   /* labs  */
extern long long w00032(long long);             /* llabs */
extern sdiv_t   w00012(int, int);               /* div   */
extern sldiv_t  w00031(long, long);             /* ldiv  */
extern slldiv_t w00033(long long, long long);   /* lldiv */

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

/* The div family returns its two-field struct in the register pair the
 * psABI assigns; compare both fields against the C-truncation answer. */
static int div_ok(void)
{
	sdiv_t a = w00012(17, 5);
	sdiv_t b = w00012(-17, 5);
	sldiv_t c = w00031(100, 7);
	slldiv_t d = w00033(1000003LL, 7);

	return a.quot == 3 && a.rem == 2 &&
	       b.quot == -3 && b.rem == -2 &&
	       c.quot == 14 && c.rem == 2 &&
	       d.quot == 142857LL && d.rem == 4LL;
}

void live_stdlib_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_stdlib,
		                          __esn_wire_stdlib_n,
		                          resolve, (void *) rt);
		if (missing == 0 && __esn_wire_stdlib_n > 0)
			status |= 0x01;

		if (w00003(-5) == 5 && w00003(7) == 7 && w00003(0) == 0)
			status |= 0x02;

		if (w00029(-(1L << 40)) == (1L << 40) && w00029(9L) == 9L)
			status |= 0x04;

		if (w00032(-(1LL << 50)) == (1LL << 50) && w00032(3LL) == 3LL)
			status |= 0x08;

		if (div_ok() &&
		    /* a second pass of the whole set, after the crossings above,
		     * to show the bodies stay correct rather than degrading */
		    w00003(-11) == 11 && w00029(-2L) == 2L &&
		    w00032(-7LL) == 7LL && div_ok())
			status |= 0x10;
	}

	leave(status);
}
