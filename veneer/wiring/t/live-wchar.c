/*
 * live-wchar -- WP-56's eleventh live crossing, and the first whose finding
 * is negative: the bind loop resolves the wchar slice's real table against a
 * real elfsysv1.dll, and the slice's wide movers reach the real body -- but
 * they move a two-byte wchar_t, not the four-byte one the slice's forward
 * premise assumed, so the wchar rows do not cross as value-preserving
 * forwards the way string's and misc's did.
 *
 * The wchar slice is wire-wchar.gen.c: 87 rows, all forwards, no shim. Its
 * bind check is math's, stdlib's, sockets's, posix's, time's and misc's
 * shape -- every row must resolve, missing == 0 -- and it holds: all 87 wide
 * names are exported by the real DLL. What the earlier crossings then went on
 * to confirm, and this one instead refutes, is that a resolved wchar row
 * crosses as a plain tail jump. It does not.
 *
 * wmemcpy (w00082), wmemmove (w00083) and wmempcpy (w00084; see
 * wire-wchar.gen.s for the index -> name mapping) are the rows this specimen
 * calls. All three are the wide analogues of memcpy/memmove/mempcpy, marked
 * NOSIGFE in runtime/exports/cygwin-exports.tsv, standing on no conversion
 * state, stream, reent, locale or kernel -- exactly the freestanding shape
 * every earlier crossing chose. So the thunk reaches the body cleanly. The
 * body then does its job by its own idea of how wide a wchar_t is, and that
 * idea is the finding.
 *
 * The face this tree presents is el8's: `wchar_t` is four bytes, the System V
 * width. The body inside elfsysv1.dll is Cygwin's newlib, whose `wchar_t` is
 * two bytes, the width Windows uses. veneer/wiring/README.md recorded the
 * wide surface as "value-preserving end to end: wchar_t is 4 bytes on both
 * sides", but that was measured face-against-glibc, both el8; this is the
 * first time the wchar rows meet the real DLL, and the two sides disagree.
 * wmemcpy(dst, src, 5) moves ten bytes, not twenty: five two-byte elements.
 * A caller that built its source as four-byte elements, as the face's
 * `wchar_t` is, hands the body five words it reads as ten narrow ones.
 *
 * The consequence is recorded in the decision this branch adds: the wchar
 * slice cannot be a set of plain forwards. Every wchar_t-bearing row -- the
 * movers here, the string operators, the converters that produce or consume
 * wide characters -- needs a width-translating shim, narrowing four-byte
 * elements to two on the way down and widening on the way up, not a tail
 * jump. This specimen's job is to pin that finding so it reproduces and so a
 * later change that made the two widths agree would flip this test and
 * announce itself.
 *
 * The payload is five wide values with their high bits set, so a two-byte
 * move visibly drops the high half of each element rather than coinciding
 * with the four-byte result by luck. Every buffer is the specimen's own
 * array of a plain four-byte word, never a libc type, so the divergence the
 * test reads is the body's own width and not a header the two sides lay out
 * differently.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx, so
 * 31 is the only passing status (five checks), a pass meaning the negative
 * finding holds and reproduces:
 *
 *   0x01  the bind resolved every row of the all-forward table (missing 0);
 *         the 87 wide names are all exported by the real DLL
 *   0x02  wmemcpy reaches the real body: the destination's first word is no
 *         longer its sentinel
 *   0x04  the move matches a two-byte-wchar_t body exactly -- ten bytes for
 *         five elements, the rest untouched -- and does not match the
 *         four-byte model the slice assumed
 *   0x08  wmempcpy returns the destination advanced by ten bytes, 2 * n, the
 *         body striding in its own two-byte element, not the twenty the
 *         face's wchar_t would give
 *   0x10  a second length reproduces: wmemcpy of three elements moves six
 *         bytes, run last so a body that had drifted would show it here
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

/* One face wchar_t: four bytes, the System V width the el8 headers present.
 * The specimen owns every buffer of these outright, so what the checks read
 * is the body's own element width against this one, not a shared libc type. */
typedef uint32_t wc;

extern struct esn_wire_ent __esn_wire_wchar[];
extern const unsigned long __esn_wire_wchar_n;

/* The wired thunks this specimen calls directly, by their generated labels.
 * Declared with the face's four-byte element, which is exactly the mismatch
 * under test: the body will read these buffers as two-byte elements. */
extern wc *w00082(wc *dest, const wc *src, size_t n);   /* wmemcpy,  .weak */
extern wc *w00083(wc *dest, const wc *src, size_t n);   /* wmemmove, .weak */
extern wc *w00084(wc *dest, const wc *src, size_t n);   /* wmempcpy, .weak */

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

/* The payload and its sentinel live in rodata, so the specimen builds no
 * buffer at runtime and the freestanding link needs no libc fill. The high
 * bits are set in every value so a two-byte move drops the high half rather
 * than matching the four-byte result by luck. */
static const wc SENT = 0xEE11EE11u;
static const wc payload[5] = {
	0xA1B2C301u, 0xA1B2C302u, 0xA1B2C303u, 0xA1B2C304u, 0xA1B2C305u
};

/* Does dst, a run of face words, hold the first `bytes` bytes of the payload
 * and the sentinel after? That is the shape a body of element width
 * `bytes / n` leaves. */
static int moved_bytes_is(const wc *dst, int bytes)
{
	const uint8_t *db = (const uint8_t *) dst;
	const uint8_t *pb = (const uint8_t *) payload;
	const uint8_t *sb = (const uint8_t *) &SENT;
	int i;

	for (i = 0; i < 32; i++) {
		uint8_t want = (i < bytes) ? pb[i] : sb[i & 3];
		if (db[i] != want)
			return 0;
	}
	return 1;
}

void live_wchar_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_wchar,
					  __esn_wire_wchar_n,
					  resolve, (void *) rt);
		if (missing == 0 && __esn_wire_wchar_n > 0)
			status |= 0x01;

		/* wmemcpy of five elements. The body reaches (0x02), moves ten
		 * bytes for a two-byte wchar_t and not twenty for a four (0x04). */
		{
			wc dst[8];
			int i;

			for (i = 0; i < 8; i++)
				dst[i] = SENT;
			(void) w00082(dst, payload, 5);
			if (dst[0] != SENT)
				status |= 0x02;
			if (moved_bytes_is(dst, 10) && !moved_bytes_is(dst, 20))
				status |= 0x04;
		}

		/* wmempcpy of five elements returns the destination advanced by
		 * the body's own stride: 2 * 5 == 10 bytes, not 4 * 5 == 20. */
		{
			wc dst[8];
			uint8_t *ret;
			int i;

			for (i = 0; i < 8; i++)
				dst[i] = SENT;
			ret = (uint8_t *) w00084(dst, payload, 5);
			if (ret == (uint8_t *) dst + 10)
				status |= 0x08;
		}

		/* A second length reproduces the two-byte stride: three
		 * elements move six bytes. Run last so a body that had drifted
		 * would show it here. wmemmove is exercised as the mover so all
		 * three wide movers are crossed. */
		{
			wc dst[8];
			int i;

			for (i = 0; i < 8; i++)
				dst[i] = SENT;
			(void) w00083(dst, payload, 3);
			if (moved_bytes_is(dst, 6) && !moved_bytes_is(dst, 12))
				status |= 0x10;
		}
	}

	leave(status);
}
