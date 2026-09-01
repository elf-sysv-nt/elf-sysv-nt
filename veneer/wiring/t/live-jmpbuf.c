/*
 * live-jmpbuf -- WP-56's third live crossing: the bind loop resolves the
 * runtime slice's real table against a real elfsysv1.dll, and this time
 * the five jmp_buf-translating shims (DR-0051's frameless face,
 * wire-jmpbuf-faces.gen.S) are exercised for real on NT, not just its
 * thunks.
 *
 * live-runtime.c proved the runtime slice's bind table resolves and two
 * of its ordinary thunks reach the real DLL body, but it left every
 * jmp_buf row out of scope on purpose (see that file's comment) and
 * found, exercising swapcontext, that "reaches the real body" and
 * "performs the control transfer it documents" can come apart under this
 * harness's minimal process state. setjmp/longjmp's contract is a
 * plainer round trip than a context switch -- no signal mask to save, no
 * second stack -- so this specimen asks the harder question directly:
 * does a real call through the frameless face actually resume at the
 * setjmp call site with the longjmp'd value, not just reach some real
 * code.
 *
 * The frameless face never calls into wire.c's ordinary bind-and-tail-jump
 * machinery for its own body (wire-jmpbuf-face.inc jmps through the same
 * table wire-runtime.gen.s's thunks read, but the face itself is
 * hand-written, not generated from wire-runtime.gen.c), so this specimen
 * still needs the full runtime table bound first -- the face's jmp target
 * is a slot in that same table -- and links wire-jmpbuf-faces.gen.S
 * alongside wire-runtime.gen.c to get both the table and the face bodies.
 *
 * The save-side macro (wire_jmpbuf_save) lazily allocates a real 256-byte
 * Cygwin jmp_buf via malloc on first use; this specimen is freestanding
 * and -nostdlib like live-math and live-runtime, so it supplies its own
 * malloc -- a plain bump allocator over a static arena, sufficient for
 * this specimen's one allocation and in keeping with "declare the shape
 * you call through, not the shape you read" (no real libc allocator's
 * behaviour is under test here; the frameless face's own lazy-allocation
 * branch is).
 *
 * Only setjmp/_setjmp's global-bound row and longjmp's are called
 * directly (by the frameless face's internal generated labels,
 * __jmpbuf_setjmp and __jmpbuf_longjmp -- see wire-jmpbuf-faces.gen.S);
 * the two weak/underscore spellings and siglongjmp share the same
 * wire_jmpbuf_save/restore macro bodies with a different symver alias, so
 * proving one pair proves the mechanism the other three rows share.
 *
 * Reports one bit per check through the terminator the stub puts in
 * %rdx, so 15 is the only passing status:
 *
 *   0x01  the bind loop resolved all 10 runtime rows (missing == 0)
 *   0x02  the setjmp face's first return is 0 and it has stashed a
 *         nonzero real-buffer pointer in the caller's el8-shaped buffer
 *   0x04  the longjmp face's call is never seen to return normally, and
 *         the setjmp call site's second return carries the value sent
 *         (42) -- proof the real Cygwin body actually transferred
 *         control back, not just that it was reached
 *   0x08  the stashed real-buffer pointer is unchanged across the round
 *         trip -- the same real Cygwin buffer, not reallocated
 */

#include <stdint.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_runtime[];
extern const unsigned long __esn_wire_runtime_n;

/* The frameless face's own generated labels (wire-jmpbuf-faces.gen.S),
 * called directly the same way live-runtime.c calls w00003/w00009 by
 * their generated names rather than through symbol versioning, which
 * this freestanding, single-object specimen has no dynamic linker to
 * apply. */
extern int __jmpbuf_setjmp(void *jb) __attribute__((returns_twice));
extern void __jmpbuf_longjmp(void *jb, int val) __attribute__((noreturn));

/* A bump allocator standing in for a real malloc: freestanding and
 * -nostdlib like the rest of this specimen, so nothing else here can
 * supply the symbol wire-jmpbuf-face.inc's save macro calls through
 * malloc@GOTPCREL. One 256-byte Cygwin jmp_buf is the only allocation
 * this specimen ever asks for. */
static unsigned char heap[4096] __attribute__((aligned(16)));
static unsigned long heap_off;

void *malloc(size_t n)
{
	void *p;
	n = (n + 15UL) & ~(size_t)15UL;
	if (n > sizeof heap - heap_off)
		return 0;
	p = heap + heap_off;
	heap_off += n;
	return p;
}

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

/* Resolve one export by name from a loaded PE image -- unchanged from
 * live-math.c and live-runtime.c. */
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

void live_jmpbuf_main(uint64_t *sp, terminator_fn leave)
{
	volatile uint64_t status = 0;
	uint64_t *p;
	const uint8_t *rt = 0;
	size_t missing;
	/* El8-shaped jmp_buf: 64 bytes, opaque to every conforming caller;
	 * the frameless save face stashes the real Cygwin buffer's pointer
	 * in the first eight. Zeroed so the save side's lazy-allocation
	 * branch takes the "fresh buffer" path, per the jmp_buf-frameless-
	 * face decision. */
	static volatile unsigned char buf[64] __attribute__((aligned(16)));
	volatile uint64_t stashed_before, stashed_after;
	volatile int rv;
	int i;

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
		missing = __esn_wire_bind(__esn_wire_runtime, __esn_wire_runtime_n,
		                          resolve, (void *) rt);
		if (missing == 0)
			status |= 0x01;

		rv = __jmpbuf_setjmp((void *) buf);
		if (rv == 0) {
			/* Read the stashed pointer byte by byte, not through a
			 * cast-and-dereference of the volatile array: buf's
			 * declared type is unsigned char, and every byte access
			 * to it is unambiguously well-defined regardless of
			 * alignment or strict-aliasing rules, where reading it
			 * through a repointed (void * volatile *) leaves the
			 * access's defined-ness resting on how the compiler
			 * treats a qualifier-and-type-punned volatile object. */
			stashed_before = 0;
			for (i = 7; i >= 0; i--)
				stashed_before = (stashed_before << 8) | buf[i];
			if (stashed_before != 0)
				status |= 0x02;
			__jmpbuf_longjmp((void *) buf, 42);
			/* The face's contract is that this call never returns;
			 * reaching here is itself a failure, and neither 0x04
			 * nor 0x08 is ever set on this path. */
		} else {
			if (rv == 42)
				status |= 0x04;
			stashed_after = 0;
			for (i = 7; i >= 0; i--)
				stashed_after = (stashed_after << 8) | buf[i];
			if (stashed_after != 0 && stashed_after == stashed_before)
				status |= 0x08;
		}
	}

	leave(status);
}
/* WIP: WP-56 increment scaffold -- extending live-jmpbuf.c to also
 * round-trip _setjmp/_longjmp and setjmp/siglongjmp, the three jmp_buf
 * rows the plain setjmp/longjmp pass above did not independently
 * exercise. Placeholder line removed by the next commit. */
