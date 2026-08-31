/*
 * live-math -- WP-56's first live crossing: the bind loop resolves the math
 * slice's real table against a real elfsysv1.dll, and a couple of the
 * slice's generated thunks are called for real on NT.
 *
 * Every wired slice up to now is judged only on el8 (diff-slice.sh, both
 * sides compiled and run on the pinned Linux image); nothing has yet run
 * `__esn_wire_bind` (veneer/wiring/wire.c) against a real DLL's real export
 * table, or called a generated wire thunk (wire-<slice>.gen.s) as executed
 * candidate code. This specimen does both, for the smallest slice whose
 * whole table is NOSIGFE (math, 34 rows): a full Cygwin process bring-up
 * like runtime/face/t/fault.c's is not needed, so the shape here is
 * WP-27's elfcall specimen's (runtime/face/t/elfcall.c) -- freestanding,
 * cross-compiled, walk the auxv to AT_BASE, resolve names out of the
 * image's own PE export directory -- with a resolver of the right shape
 * (`esn_wire_resolver`, wire.h) standing in for the runtime's eventual
 * GetProcAddress callback, and the bind loop itself doing the resolving
 * rather than this specimen calling pe_export row by row.
 *
 * The three thunks exercised (w00010 copysign, w00022 isnan, w00025 ldexp;
 * see wire-math.gen.s for the index -> name mapping) are `.weak` globals,
 * declared here and called directly -- the specimen does not need the
 * `.symver`-bound glibc entry points a real ELF binary's dynamic linker
 * would resolve to reach them, only proof that the thunk's tail jump lands
 * on the slot the bind loop filled and returns the right value.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 15 is the only passing status (four checks: bind ran and filled every
 * slot, then the three thunks each hit the real DLL body).
 *
 *   0x01  the bind loop resolved all 34 math rows (missing == 0)
 *   0x02  the copysign thunk (w00010) reaches the real body
 *   0x04  the isnan thunk (w00022) reaches the real body
 *   0x08  the ldexp thunk (w00025) reaches the real body
 */

#include <stdint.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_math[];
extern const unsigned long __esn_wire_math_n;

/* The wired thunks this specimen calls directly, by their generated label
 * (wire-math.gen.s -- w00010 is copysign, w00022 is isnan, w00025 is
 * ldexp). System V, the convention this whole unit is compiled to, is the
 * shape a real ELF caller reaches them through too. */
extern double w00010(double, double);   /* copysign */
extern int    w00022(double);           /* isnan */
extern double w00025(double, int);      /* ldexp */

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

void live_math_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_math, __esn_wire_math_n,
		                          resolve, (void *) rt);
		if (missing == 0)
			status |= 0x01;

		if (w00010(-3.5, 1.0) == 3.5 && w00010(3.5, -1.0) == -3.5)
			status |= 0x02;

		if (w00022(0.0) == 0 && w00022(1.0 / 0.0 - 1.0 / 0.0) != 0)
			status |= 0x04;

		if (w00025(1.0, 8) == 256.0)
			status |= 0x08;
	}

	leave(status);
}
