/*
 * live-sockets -- WP-56's sixth live crossing, and the first to call a
 * weak-alias thunk. The bind loop resolves the sockets slice's real table
 * against a real elfsysv1.dll, and four of the slice's generated thunks --
 * the byte-order family -- are called for real on NT.
 *
 * The sockets slice is 66 rows, all forwards, no shim (wire-sockets.gen.c),
 * so its bind check is math's and stdlib's shape -- every row must resolve,
 * missing == 0 -- not string's exactly-one-null. It is the largest
 * all-forward table bound live so far.
 *
 * What sockets adds over the earlier crossings is a weak-alias thunk. glibc
 * exports ntohl and ntohs as weak aliases of htonl and htons -- on a
 * little-endian target the two are the same permutation over the same body
 * -- and gen-wire.py carries that weakness through: htonl (w00028) and htons
 * (w00029) are .globl thunks, while ntohl (w00044) and ntohs (w00045) are
 * .weak ones (see wire-sockets.gen.s). This specimen is the first to call a
 * weak thunk directly and show it reaches the same real body its strong twin
 * does. The family also returns narrower than a register -- htons and ntohs
 * hand back uint16_t, htonl and ntohl uint32_t -- so the caller reads only
 * the defined low bits of whatever the thunk forwards, a return shape none
 * of the earlier scalar-returning crossings exercised.
 *
 * The NOSIGFE boundary string found still holds. htonl, htons, ntohl and
 * ntohs are NOSIGFE (runtime/exports/cygwin-exports.tsv) and, like string's
 * ffs family and stdlib's abs family, stand alone: each is a pure byte
 * permutation over its argument with no memory, reent or locale behind it,
 * so it crosses in a freestanding harness that never set up Cygwin's thread
 * pointer. The rest of sockets -- the resolver, the socket calls, everything
 * whose body reads reent or touches a descriptor -- is left for the same
 * process bring-up the SIGFE slices wait on, exactly as string left memcpy.
 *
 * The four thunks exercised (w00028 htonl, w00029 htons, w00044 ntohl,
 * w00045 ntohs; see wire-sockets.gen.s for the index -> name mapping) are
 * declared here and called directly by their generated label. System V, the
 * convention this whole unit is compiled to, is the shape a real ELF caller
 * reaches them through too.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind resolved every row of the all-forward table (missing 0)
 *   0x02  the htonl thunk (w00028) reaches the real body and swaps 32 bits
 *   0x04  the htons thunk (w00029) reaches the real body and swaps 16 bits
 *   0x08  the weak ntohl/ntohs aliases (w00044/w00045) reach the body and
 *         agree with their strong twins
 *   0x10  the family is self-inverse -- ntohl(htonl(v)) == v and
 *         htons(ntohs(w)) == w over several values -- and a second pass of
 *         the whole set still agrees after the crossings above
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_sockets[];
extern const unsigned long __esn_wire_sockets_n;

/* The wired thunks this specimen calls directly, by their generated label.
 * The widths match the byte-order family's declared returns -- uint16_t for
 * the short pair, uint32_t for the long -- so the return class is identical
 * to the real prototypes without reaching into the very libc under test. */
extern uint32_t w00028(uint32_t);   /* htonl, .globl */
extern uint16_t w00029(uint16_t);   /* htons, .globl */
extern uint32_t w00044(uint32_t);   /* ntohl, .weak alias of htonl */
extern uint16_t w00045(uint16_t);   /* ntohs, .weak alias of htons */

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

void live_sockets_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_sockets,
		                          __esn_wire_sockets_n,
		                          resolve, (void *) rt);
		if (missing == 0 && __esn_wire_sockets_n > 0)
			status |= 0x01;

		/* htonl swaps a 32-bit value end for end on this LE target. */
		if (w00028(0x01020304u) == 0x04030201u &&
		    w00028(0u) == 0u &&
		    w00028(0xFFFFFFFFu) == 0xFFFFFFFFu)
			status |= 0x02;

		/* htons swaps a 16-bit value; only the low half is defined. */
		if (w00029(0x0102u) == 0x0201u &&
		    w00029(0u) == 0u &&
		    w00029(0x00FFu) == 0xFF00u)
			status |= 0x04;

		/* The weak ntohl/ntohs aliases reach the body and, being the
		 * same permutation as their strong twins on a LE target, agree
		 * with them value for value. */
		if (w00044(0x04030201u) == 0x01020304u &&
		    w00044(0xDEADBEEFu) == w00028(0xDEADBEEFu) &&
		    w00045(0xBEEFu) == w00029(0xBEEFu))
			status |= 0x08;

		/* Self-inverse: the pair composes back to identity, no
		 * host-endianness assumption in the assertion. A second pass of
		 * the whole set after the crossings above shows the bodies stay
		 * correct rather than degrading. */
		if (w00044(w00028(0x12345678u)) == 0x12345678u &&
		    w00044(w00028(0x00000001u)) == 0x00000001u &&
		    w00045(w00029(0xABCDu)) == 0xABCDu &&
		    w00029(w00045(0x1234u)) == 0x1234u &&
		    w00028(0x0A0B0C0Du) == 0x0D0C0B0Au &&
		    w00029(0x0304u) == 0x0403u)
			status |= 0x10;
	}

	leave(status);
}
