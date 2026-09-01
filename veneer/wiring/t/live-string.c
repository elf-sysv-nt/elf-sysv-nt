/*
 * live-string -- WP-56's fourth live crossing, and the first over a slice
 * that mixes forwards with a shim: the bind loop resolves the string
 * slice's real table against a real elfsysv1.dll, and a few of the slice's
 * generated thunks are called for real on NT.
 *
 * math and runtime were the census's only wholly-NOSIGFE slices, and their
 * live crossings (t/live-math, t/live-runtime) rested on that: a thunk
 * whose body is NOSIGFE needs no full Cygwin process bring-up, so the
 * specimen can stay freestanding, walk the auxv to AT_BASE, and resolve
 * exports out of the image's own PE export directory -- WP-27's elfcall
 * shape. The string slice is not wholly NOSIGFE, but the rows this specimen
 * exercises are: memcpy, ffs and strverscmp are pure computation over
 * caller-owned memory (runtime/exports/cygwin-exports.tsv marks all three
 * NOSIGFE), so the same freestanding shape reaches them.
 *
 * What string adds over math is the forward/shim split made visible at the
 * bind. Of the slice table's 43 distinct export names, 42 are forwards and
 * resolve against the real DLL by name; one, __errno_location, is the
 * slice's only shim -- a translation the veneer supplies, not a same-name
 * forward -- and Cygwin exports no symbol of that name, so it does not
 * resolve. The bind therefore leaves exactly one slot null, and it is that
 * shim's: check 0x01 asserts precisely this rather than missing == 0, which
 * would be false and would hide the very distinction the slice is built on.
 *
 * The three thunks exercised (w00030 memcpy, w00026 ffs, w00044 strverscmp;
 * see wire-string.gen.s for the index -> name mapping) are .globl globals,
 * declared here and called directly by their generated label -- the
 * specimen does not need the .symver-bound glibc entry points a real ELF
 * binary's dynamic linker would resolve to reach them, only proof that the
 * thunk's tail jump lands on the slot the bind loop filled and returns the
 * real body's answer.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 15 is the only passing status (four checks):
 *
 *   0x01  the bind resolved every forward row and left exactly the one
 *         shim (__errno_location) null
 *   0x02  the memcpy thunk (w00030) copies and returns the destination
 *   0x04  the ffs thunk (w00026) reaches the real body
 *   0x08  the strverscmp thunk (w00044) reaches the real body
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_string[];
extern const unsigned long __esn_wire_string_n;

/* The wired thunks this specimen calls directly, by their generated label
 * (wire-string.gen.s -- w00030 is memcpy, w00026 is ffs, w00044 is
 * strverscmp). System V, the convention this whole unit is compiled to, is
 * the shape a real ELF caller reaches them through too. */
extern void *w00030(void *, const void *, unsigned long);   /* memcpy */
extern int   w00026(int);                                   /* ffs */
extern int   w00044(const char *, const char *);            /* strverscmp */

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

/* A local, libc-free string equality -- the specimen is freestanding and
 * must not reach for the very libc it is testing. */
static int streq(const char *a, const char *b)
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

/* Every forward row filled, exactly the one shim (__errno_location) left
 * null. Returns nonzero only for that exact shape. */
static int bind_left_only_the_shim(void)
{
	size_t i;
	int shim_seen = 0;

	for (i = 0; i < __esn_wire_string_n; i++) {
		if (__esn_wire_string[i].fn == 0) {
			if (!streq(__esn_wire_string[i].export_name,
			           "__errno_location"))
				return 0;   /* a forward row went unresolved */
			shim_seen = 1;
		}
	}
	return shim_seen;
}

void live_string_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_string,
		                          __esn_wire_string_n,
		                          resolve, (void *) rt);
		if (missing == 1 && bind_left_only_the_shim())
			status |= 0x01;

		{
			static const char src[8] =
			    { 'w', 'i', 'r', 'e', 'd', '!', '?', 0 };
			volatile char dst[8];
			void *ret;
			int ok = 1, i;

			for (i = 0; i < 8; i++)
				dst[i] = 0x5A;
			ret = w00030((void *) dst, src, 8);
			if (ret != (void *) dst)
				ok = 0;
			for (i = 0; i < 8; i++)
				if (dst[i] != src[i])
					ok = 0;
			if (ok)
				status |= 0x02;
		}

		if (w00026(0) == 0 && w00026(1) == 1 && w00026(8) == 4 &&
		    w00026(0x80) == 8)
			status |= 0x04;

		if (w00044("a", "b") < 0 && w00044("b", "a") > 0 &&
		    w00044("wired", "wired") == 0)
			status |= 0x08;
	}

	leave(status);
}
