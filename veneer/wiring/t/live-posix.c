/*
 * live-posix -- WP-56's eighth live crossing. The bind loop resolves the
 * posix slice's real table against a real elfsysv1.dll, and one of the
 * slice's generated thunks -- swab -- is called for real on NT.
 *
 * The posix slice is the largest all-forward table crossed live so far:
 * wire-posix.gen.c is 108 rows, 102 distinct export names, no shim (an
 * empty wire-posix.shims.tsv), so its bind check is math's, stdlib's,
 * sockets's and locale's shape -- every row must resolve, missing == 0 --
 * not string's exactly-one-null. sockets at 66 rows held the previous
 * largest; posix is bigger, and binding it live shows the resolver scales
 * past that without a miss.
 *
 * What posix is, that the earlier crossed slices were not, is a slice whose
 * bodies overwhelmingly touch a descriptor, the process, or Cygwin's reent
 * and thread state: fork, close, dup, access, the exec family, chdir, the
 * pathconf and getcwd calls all reach the kernel face or the current
 * process through the thread pointer this freestanding harness never
 * establishes. Calling any of them here would corrupt the way string's
 * memcpy and strverscmp did, and for the same reason -- NOSIGFE names the
 * calling convention a thunk needs, not whether the body behind it stands
 * on its own. Even most of posix's own NOSIGFE rows (getpid, getuid,
 * getlogin and kin) read process or cygheap state and are off-limits here.
 *
 * swab is the one row that stands entirely alone: a byte permutation
 * between two caller-provided buffers, with no table, reent, descriptor or
 * locale behind it, marked NOSIGFE in runtime/exports/cygwin-exports.tsv.
 * It copies floor(n/2) pairs from `from` to `to`, swapping the two bytes of
 * each pair. So it crosses cleanly, exactly as string's ffs family,
 * stdlib's abs family, sockets's byte-order family and locale's toascii
 * did. The rest of posix is left for the process bring-up the SIGFE slices
 * wait on.
 *
 * What swab adds that the earlier crossed rows did not is its result shape.
 * ffs, abs, htonl and toascii each hand their answer back in a register;
 * swab returns void and writes its whole result through the caller's
 * destination pointer. This is the first crossed row whose output lands in
 * caller memory rather than in %rax, so it exercises a pointer-out argument
 * class the earlier scalar and struct-by-value returns never reached.
 *
 * The crossing keeps to swab's well-defined range: a positive even length,
 * a positive odd length, and a zero length. It does not probe a negative
 * length, which POSIX leaves for swab to ignore but Cygwin's body does not
 * guard -- a live check confirmed the real body writes through the
 * destination for a negative count rather than returning, so the argument
 * is out of this crossing's scope and belongs to whatever later work pins
 * the divergences a differential records.
 *
 * The one thunk exercised (w00089 swab; see wire-posix.gen.s for the
 * index -> name mapping) is declared here and called directly by its
 * generated label. System V, the convention this whole unit is compiled to,
 * is the shape a real ELF caller reaches it through too.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind resolved every row of the all-forward table (missing 0)
 *   0x02  swab swaps the two bytes of every pair of an even-length buffer,
 *         writing the result through the destination pointer
 *   0x04  with an odd length swab swaps the whole pairs and leaves the
 *         trailing unpaired byte of the destination untouched
 *   0x08  with a zero length swab writes nothing at all, leaving the
 *         destination as it was
 *   0x10  swab matches a local reference swap over a whole 256-byte buffer,
 *         re-checked after the crossings above to show the body stays
 *         correct, not degrading
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_posix[];
extern const unsigned long __esn_wire_posix_n;

/* The wired thunk this specimen calls directly, by its generated label.
 * swab takes two pointers and a signed length and returns void, exactly its
 * real prototype, so the argument and return classes match without reaching
 * into the very libc under test. */
extern void w00089(const void *from, void *to, long n);   /* swab, .globl */

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

void live_posix_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_posix,
		                          __esn_wire_posix_n,
		                          resolve, (void *) rt);
		if (missing == 0 && __esn_wire_posix_n > 0)
			status |= 0x01;

		/* swab swaps the two bytes of every pair, writing the result
		 * through the destination pointer: ABCD -> BADC. */
		{
			unsigned char from[4] = { 'A', 'B', 'C', 'D' };
			unsigned char to[4] = { 0, 0, 0, 0 };
			w00089(from, to, 4);
			if (to[0] == 'B' && to[1] == 'A' &&
			    to[2] == 'D' && to[3] == 'C')
				status |= 0x02;
		}

		/* An odd length swaps the whole pairs -- floor(5/2) == 2 of
		 * them, four bytes -- and leaves the trailing fifth byte of the
		 * destination as it was, here the sentinel 0xAA. */
		{
			unsigned char from[5] = { 1, 2, 3, 4, 5 };
			unsigned char to[5];
			int i;
			for (i = 0; i < 5; i++)
				to[i] = 0xAA;
			w00089(from, to, 5);
			if (to[0] == 2 && to[1] == 1 &&
			    to[2] == 4 && to[3] == 3 && to[4] == 0xAA)
				status |= 0x04;
		}

		/* A zero length writes nothing at all, leaving the destination
		 * untouched -- here the sentinel 0x77. */
		{
			unsigned char from[4] = { 1, 2, 3, 4 };
			unsigned char to[4];
			int i, ok = 1;
			for (i = 0; i < 4; i++)
				to[i] = 0x77;
			w00089(from, to, 0);
			for (i = 0; i < 4; i++)
				if (to[i] != 0x77)
					ok = 0;
			if (ok)
				status |= 0x08;
		}

		/* swab == a local reference swap over a whole 256-byte buffer,
		 * run after the crossings above so a body that had degraded
		 * would show it here. */
		{
			unsigned char from[256];
			unsigned char to[256];
			int i, ok = 1;
			for (i = 0; i < 256; i++) {
				from[i] = (unsigned char)((i * 37 + 11) & 0xFF);
				to[i] = 0x55;
			}
			w00089(from, to, 256);
			for (i = 0; i + 1 < 256; i += 2)
				if (to[i] != from[i + 1] ||
				    to[i + 1] != from[i])
					ok = 0;
			if (ok)
				status |= 0x10;
		}
	}

	leave(status);
}
