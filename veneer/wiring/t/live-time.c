/*
 * live-time -- WP-56's ninth live crossing. The bind loop resolves the time
 * slice's real table against a real elfsysv1.dll, and one of the slice's
 * generated thunks -- difftime -- is called for real on NT.
 *
 * The time slice is wire-time.gen.c: 40 rows, all forwards, no shim (an
 * empty wire-time.shims.tsv), so its bind check is math's, stdlib's,
 * sockets's, locale's and posix's shape -- every row must resolve,
 * missing == 0 -- not string's exactly-one-null.
 *
 * What the time slice is, that the earlier crossed slices were not, is a
 * slice whose bodies overwhelmingly read a clock, a timezone, or Cygwin's
 * reent and thread state: clock_gettime, the gmtime/localtime family and
 * their _r twins, mktime, strftime, tzset and the timer calls all reach the
 * kernel face, the tz rules, or the current thread through the thread
 * pointer this freestanding harness never establishes. Calling any of them
 * here would corrupt the way string's memcpy and strverscmp did, and for the
 * same reason -- NOSIGFE names the calling convention a thunk needs, not
 * whether the body behind it stands on its own. timegm, the slice's other
 * NOSIGFE row, reads the tz-independent conversion tables and is not
 * argument-only, so it is out of scope here too.
 *
 * difftime (w00015; see wire-time.gen.s for the index -> name mapping) is
 * the one row that stands entirely alone. POSIX and Cygwin's body both fix
 * it as the arithmetic difference of two calendar times returned as a
 * double -- on this target, time_t is a signed integer and the body is the
 * subtraction (double)(time1 - time0), with no table, clock, reent or
 * locale behind it. runtime/exports/cygwin-exports.tsv marks it NOSIGFE. So
 * it crosses a freestanding harness cleanly, exactly as string's ffs
 * family, stdlib's abs family, sockets's byte-order family, locale's
 * toascii and posix's swab did. The rest of time is left for the process
 * bring-up the SIGFE-fenced slices wait on.
 *
 * What difftime adds that the earlier crossed rows did not is its result
 * class. ffs, abs and htonl each returned an integer in %rax; toascii the
 * same; swab wrote its result through a caller pointer. difftime returns a
 * double, handed back in %xmm0 by the psABI, and it takes two integer
 * (time_t) arguments -- so it is the first crossed forward whose inputs are
 * integers in the general registers and whose answer comes back in a
 * floating-point register. (math's own crossing returned doubles, but math
 * was the all-NOSIGFE slice that proved the bind mechanism; difftime is the
 * first of the demand-ranked, reent-heavy slices to exercise the xmm return
 * path.)
 *
 * The crossing keeps to values every one of which is exactly representable
 * as a double -- small integers and 10^9, all well under 2^53 -- so the
 * checks compare for exact equality without a tolerance and without
 * assuming anything the arithmetic does not guarantee.
 *
 * The one thunk exercised is declared here with difftime's own prototype
 * and called directly by its generated label. System V, the convention this
 * whole unit is compiled to, is the shape a real ELF caller reaches it
 * through too.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind resolved every row of the all-forward table (missing 0)
 *   0x02  difftime of two equal instants is 0.0
 *   0x04  difftime(later, earlier) is the positive span, in %xmm0
 *   0x08  difftime(earlier, later) is that span negated -- the sign is the
 *         body's, not the harness's
 *   0x10  difftime matches a local (double)(a - b) reference over a set of
 *         pairs including a 10^9-second span, re-checked after the crossings
 *         above to show the body stays correct, not degrading
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_time[];
extern const unsigned long __esn_wire_time_n;

/* The wired thunk this specimen calls directly, by its generated label.
 * difftime takes two time_t and returns a double, exactly its real
 * prototype; on this target time_t is a signed 64-bit integer. Declaring it
 * with that shape matches the argument and return classes without reaching
 * into the very libc under test. */
extern double w00015(long time1, long time0);   /* difftime, .globl */

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

void live_time_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_time,
					  __esn_wire_time_n,
					  resolve, (void *) rt);
		if (missing == 0 && __esn_wire_time_n > 0)
			status |= 0x01;

		/* difftime of two equal instants is 0.0. */
		if (w00015(1000L, 1000L) == 0.0)
			status |= 0x02;

		/* difftime(later, earlier) is the positive span. */
		if (w00015(100L, 40L) == 60.0)
			status |= 0x04;

		/* difftime(earlier, later) is that span negated -- the sign
		 * is the real body's, not the harness's. */
		if (w00015(40L, 100L) == -60.0)
			status |= 0x08;

		/* difftime == a local (double)(a - b) reference over a set
		 * of pairs, including a 10^9-second span, run after the
		 * crossings above so a body that had degraded would show it
		 * here. Every value is exactly representable, so equality is
		 * exact. */
		{
			static const long a[5] = {
				0L, 60L, 1000000000L, 86400L, 123456789L
			};
			static const long b[5] = {
				0L, 0L, 0L, 1L, 123456700L
			};
			int i, ok = 1;
			for (i = 0; i < 5; i++) {
				double want = (double) a[i] - (double) b[i];
				if (w00015(a[i], b[i]) != want)
					ok = 0;
			}
			if (ok)
				status |= 0x10;
		}
	}

	leave(status);
}
