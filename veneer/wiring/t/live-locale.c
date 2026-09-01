/*
 * live-locale -- WP-56's seventh live crossing, and the first of a slice
 * whose family is almost entirely off-limits to a freestanding harness. The
 * bind loop resolves the locale slice's real table against a real
 * elfsysv1.dll, and one of the slice's generated thunks -- toascii -- is
 * called for real on NT.
 *
 * The locale slice is 83 rows, all forwards, no shim (wire-locale.gen.c and
 * an empty wire-locale.shims.tsv), so its bind check is math's, stdlib's and
 * sockets's shape -- every row must resolve, missing == 0 -- not string's
 * exactly-one-null.
 *
 * What locale is, that the earlier crossed slices were not, is a slice whose
 * bodies overwhelmingly read locale or ctype state through the thread
 * pointer this freestanding harness never establishes. The classifiers and
 * their _l twins (isalpha, iswctype, ...), setlocale, localeconv,
 * nl_langinfo, strfmon and the message catalogs all reach the current
 * locale object or the ctype tables it fronts; calling any of them here
 * would corrupt the way string's memcpy and strverscmp did, for the same
 * reason -- NOSIGFE names the calling convention a thunk needs, not whether
 * the body behind it stands on its own. toascii is the exception the slice
 * still offers: POSIX fixes it as `c & 0x7f`, a pure argument-only mask with
 * no table, reent or locale behind it (runtime/exports/cygwin-exports.tsv
 * marks it NOSIGFE), so it crosses cleanly exactly as string's ffs family,
 * stdlib's abs family and sockets's byte-order family did. The rest of
 * locale is left for the process bring-up the SIGFE slices wait on.
 *
 * The one thunk exercised (w00067 toascii; see wire-locale.gen.s for the
 * index -> name mapping) is declared here and called directly by its
 * generated label. System V, the convention this whole unit is compiled to,
 * is the shape a real ELF caller reaches it through too.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind resolved every row of the all-forward table (missing 0)
 *   0x02  toascii strips bit 7 from a value that has it set
 *   0x04  toascii passes a value already inside 0..127 through unchanged
 *   0x08  toascii masks EOF and negative inputs by their low seven bits
 *   0x10  toascii == (c & 0x7f) over every c in -128..255, re-checked after
 *         the crossings above to show the body stays correct, not degrading
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_locale[];
extern const unsigned long __esn_wire_locale_n;

/* The wired thunk this specimen calls directly, by its generated label.
 * toascii takes and returns int, exactly its real prototype, so the argument
 * and return classes match without reaching into the very libc under test. */
extern int w00067(int);   /* toascii, .globl forward-same */

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

void live_locale_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_locale,
		                          __esn_wire_locale_n,
		                          resolve, (void *) rt);
		if (missing == 0 && __esn_wire_locale_n > 0)
			status |= 0x01;

		/* toascii strips bit 7 from a value that carries it. */
		if (w00067(0xE9) == 0x69 &&
		    w00067(0xFF) == 0x7F &&
		    w00067(0x80) == 0x00)
			status |= 0x02;

		/* A value already inside 0..127 passes through untouched. */
		if (w00067('A') == 'A' &&
		    w00067(0) == 0 &&
		    w00067(0x7F) == 0x7F)
			status |= 0x04;

		/* EOF and negative inputs are masked by their low seven bits,
		 * not treated as errors: (-1)&0x7f is 0x7f, (-128)&0x7f is 0. */
		if (w00067(-1) == 0x7F &&
		    w00067(-128) == 0x00 &&
		    w00067(-129) == 0x7F)
			status |= 0x08;

		/* toascii == (c & 0x7f) over the whole signed-char-plus-byte
		 * range, run after the crossings above so a body that had
		 * degraded would show it here. */
		{
			int c, ok = 1;
			for (c = -128; c <= 255; c++)
				if (w00067(c) != (c & 0x7F))
					ok = 0;
			if (ok)
				status |= 0x10;
		}
	}

	leave(status);
}
