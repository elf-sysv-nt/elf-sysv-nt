/*
 * live-misc -- WP-56's tenth live crossing. The bind loop resolves the misc
 * slice's real table against a real elfsysv1.dll, and two of the slice's
 * generated thunks -- insque and remque -- are called for real on NT.
 *
 * The misc slice is wire-misc.gen.c: 33 rows, all forwards, no shim (an
 * empty shim set), so its bind check is math's, stdlib's, sockets's,
 * locale's, posix's and time's shape -- every row must resolve, missing ==
 * 0 -- not string's exactly-one-null.
 *
 * What misc is, that the earlier crossed slices were not, is a slice whose
 * demanded bodies overwhelmingly read process, locale or reent state: the
 * err/warn family and error write to stderr, getopt walks a global parse
 * state, the hsearch and tsearch families own a table through the thread's
 * reent, getentropy and getrandom reach the kernel, and wordexp forks a
 * shell. Calling any of those here would corrupt the way string's memcpy
 * and strverscmp did, and for the same reason -- NOSIGFE names the calling
 * convention a thunk needs, not whether the body behind it stands on its
 * own.
 *
 * insque (w00016) and remque (w00019; see wire-misc.gen.s for the index ->
 * name mapping) are the two rows that stand entirely alone. Both are the
 * historical System V doubly-linked-queue primitives, and their whole
 * contract is pointer arithmetic over a caller-owned list: insque(elem,
 * prev) splices elem in after prev, remque(elem) unsplices it, each
 * touching only the q_forw and q_back words at the head of the caller's own
 * nodes. runtime/exports/cygwin-exports.tsv marks both NOSIGFE. There is no
 * table, clock, reent, locale or kernel behind either, so they cross a
 * freestanding harness cleanly, exactly as string's ffs family, stdlib's
 * abs family, sockets's byte-order family, locale's toascii, posix's swab
 * and time's difftime did. The rest of misc is left for the process
 * bring-up the SIGFE-fenced slices wait on.
 *
 * What insque and remque add that the earlier crossed rows did not is the
 * shape of what they touch. difftime returned a double from two integers,
 * swab wrote a byte buffer, the abs and byte-order families returned scalars
 * in %rax; insque and remque take two caller pointers and edit a linked
 * structure through them, reading back one node to reach the next. The
 * queue node here is the specimen's own struct with two leading pointer
 * words -- the q_forw, q_back layout the primitives' contract fixes for
 * every libc, so nothing here depends on a header the cross toolchain and
 * the real DLL might lay out differently. That is the same discipline every
 * earlier crossing kept: scalars and raw words the specimen owns, never a
 * libc struct whose two sides might disagree.
 *
 * The two thunks are declared here with insque's and remque's own
 * prototypes and called directly by their generated labels. System V, the
 * convention this whole unit is compiled to, is the shape a real ELF caller
 * reaches them through too.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind resolved every row of the all-forward table (missing 0)
 *   0x02  after building a three-node queue, the head links forward to the
 *         middle and back to nothing
 *   0x04  the middle node links forward to the tail and back to the head --
 *         insque wrote both directions
 *   0x08  the tail links forward to nothing and back to the middle
 *   0x10  after remque of the middle node the head and tail close over the
 *         gap (head -> tail, tail -> head), re-checked after the crossings
 *         above so a body that had degraded would show it here
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_misc[];
extern const unsigned long __esn_wire_misc_n;

/* The queue node: two leading pointer words, exactly the q_forw, q_back
 * layout the insque/remque contract fixes. The tag is the specimen's own
 * and the primitives never touch it. */
struct qe {
	struct qe *forw;
	struct qe *back;
	long tag;
};

/* The wired thunks this specimen calls directly, by their generated labels.
 * insque and remque take void * queue pointers and return nothing, exactly
 * their real prototypes, without reaching into the very libc under test. */
extern void w00016(void *elem, void *prev);   /* insque, .globl */
extern void w00019(void *elem);               /* remque, .globl */

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

void live_misc_main(uint64_t *sp, terminator_fn leave)
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
		missing = __esn_wire_bind(__esn_wire_misc,
					  __esn_wire_misc_n,
					  resolve, (void *) rt);
		if (missing == 0 && __esn_wire_misc_n > 0)
			status |= 0x01;

		/* Three nodes, each pre-filled with a sentinel in both link
		 * words, so a body that failed to write a link would leave
		 * the sentinel and fail the check below rather than pass by
		 * luck. guard is a distinct address never spliced in. */
		{
			struct qe guard;
			struct qe a, b, c;

			a.forw = a.back = &guard;
			b.forw = b.back = &guard;
			c.forw = c.back = &guard;
			a.tag = 1;
			b.tag = 2;
			c.tag = 3;

			/* insque(&a, NULL): a starts a fresh queue, both
			 * links cleared. insque(&b, &a): b after a.
			 * insque(&c, &b): c after b. */
			w00016(&a, (void *) 0);
			w00016(&b, &a);
			w00016(&c, &b);

			if (a.forw == &b && a.back == (struct qe *) 0)
				status |= 0x02;
			if (b.forw == &c && b.back == &a)
				status |= 0x04;
			if (c.forw == (struct qe *) 0 && c.back == &b)
				status |= 0x08;

			/* remque(&b): the head and tail close over the gap.
			 * Run after the crossings above so a body that had
			 * degraded would show it here. */
			w00019(&b);
			if (a.forw == &c && c.back == &a)
				status |= 0x10;
		}
	}

	leave(status);
}
