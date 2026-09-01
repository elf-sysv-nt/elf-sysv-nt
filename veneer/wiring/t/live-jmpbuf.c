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
 * this specimen's three allocations (one jmp_buf per round below) and in
 * keeping with "declare the shape you call through, not the shape you
 * read" (no real libc allocator's behaviour is under test here; the
 * frameless face's own lazy-allocation branch is).
 *
 * setjmp/longjmp's global-bound row (__jmpbuf_setjmp / __jmpbuf_longjmp)
 * proved live first; this specimen adds the other three jmp_buf rows the
 * earlier increment's file comment flagged as sharing the same two macro
 * bodies with only a symver alias differing --
 * _setjmp/_longjmp (__jmpbuf__setjmp / __jmpbuf__longjmp) and siglongjmp
 * (__jmpbuf_siglongjmp). There is no sigsetjmp row in the census -- only
 * five jmp_buf-translating rows exist, and siglongjmp is a restore with
 * no paired save of its own -- so the siglongjmp round below restores a
 * buffer that setjmp's own row saved, which is a legitimate probe of
 * wire_jmpbuf_restore's macro instantiation for that symver alias even
 * though no real caller would mix the two spellings this way.
 *
 * Reports one bit per check through the terminator the stub puts in
 * %rdx. All seven defined bits set (0xF7 -- 0x08 is retired, see below) is
 * the only passing status:
 *
 *   0x01  the bind loop resolved all 10 runtime rows (missing == 0)
 *   0x02  the setjmp face's first return is 0, it has stashed a nonzero
 *         real-buffer pointer, and the longjmp face's second return
 *         restores that same pointer unchanged -- setjmp/longjmp's row
 *         pair (__jmpbuf_setjmp / __jmpbuf_longjmp)
 *   0x04  that same round's second return carries the value sent (42) --
 *         proof the real Cygwin body actually transferred control back,
 *         not just that it was reached
 *   0x10  the same two checks as 0x02, for the _setjmp/_longjmp row pair
 *         (__jmpbuf__setjmp / __jmpbuf__longjmp)
 *   0x20  the same check as 0x04, for that round
 *   0x40  the same two checks as 0x02, for a buffer saved by setjmp and
 *         restored by siglongjmp (__jmpbuf_setjmp / __jmpbuf_siglongjmp)
 *   0x80  the same check as 0x04, for that round
 *
 * (0x08 is retired from the setjmp/longjmp-only predecessor of this
 * file, which used it for a stash-unchanged check now folded into 0x02
 * above; nothing in the current status word sets it.)
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
extern int __jmpbuf__setjmp(void *jb) __attribute__((returns_twice));
extern void __jmpbuf__longjmp(void *jb, int val) __attribute__((noreturn));
extern void __jmpbuf_siglongjmp(void *jb, int val) __attribute__((noreturn));

/* A bump allocator standing in for a real malloc: freestanding and
 * -nostdlib like the rest of this specimen, so nothing else here can
 * supply the symbol wire-jmpbuf-face.inc's save macro calls through
 * malloc@GOTPCREL. Three 256-byte Cygwin jmp_bufs -- one per round below
 * -- are the only allocations this specimen ever asks for. */
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

/* One save/restore round through a curated pair of frameless-face labels,
 * folded into a macro so the three rows below (setjmp/longjmp,
 * _setjmp/_longjmp, and setjmp/siglongjmp) share one body instead of
 * three hand-copied ones. Expanded inline in live_jmpbuf_main rather than
 * factored into a real function: SETFN carries
 * __attribute__((returns_twice)), and this macro's caller is the frame
 * whose state that attribute is telling GCC to keep honest across the
 * call, the same requirement the file comment on the original
 * setjmp/longjmp round already worked out the hard way (see the
 * README's account of that increment). BUF must be a static or
 * function-scope volatile unsigned char[64], zeroed before first use so
 * the save side's lazy-allocation branch takes the fresh-buffer path.
 * BIT_OK is set once the forward leg stashes a nonzero real-buffer
 * pointer and the restore leg reads the identical pointer back; BIT_RET
 * is set once the restore leg's return value is the sent value (42),
 * which only happens if the real Cygwin body actually transferred
 * control back rather than merely being reached.
 */
#define JMPBUF_ROUND(SETFN, RESTOREFN, BUF, BIT_OK, BIT_RET) \
	do { \
		volatile uint64_t before_, after_; \
		volatile int rv_; \
		int k_; \
		rv_ = SETFN((void *) (BUF)); \
		if (rv_ == 0) { \
			before_ = 0; \
			for (k_ = 7; k_ >= 0; k_--) \
				before_ = (before_ << 8) | (BUF)[k_]; \
			RESTOREFN((void *) (BUF), 42); \
			/* RESTOREFN never returns; reaching here sets \
			 * neither bit on this path. */ \
		} else { \
			if (rv_ == 42) \
				status |= (BIT_RET); \
			after_ = 0; \
			for (k_ = 7; k_ >= 0; k_--) \
				after_ = (after_ << 8) | (BUF)[k_]; \
			if (before_ != 0 && after_ == before_) \
				status |= (BIT_OK); \
		} \
	} while (0)

void live_jmpbuf_main(uint64_t *sp, terminator_fn leave)
{
	volatile uint64_t status = 0;
	uint64_t *p;
	const uint8_t *rt = 0;
	size_t missing;
	/* El8-shaped jmp_bufs: 64 bytes each, opaque to every conforming
	 * caller; the frameless save face stashes the real Cygwin buffer's
	 * pointer in the first eight. Zeroed (static) so each save side's
	 * lazy-allocation branch takes the "fresh buffer" path, per the
	 * jmp_buf-frameless-face decision. One buffer per round: sharing a
	 * buffer across rounds would leave a later round's save
	 * overwriting an earlier round's already-allocated real Cygwin
	 * buffer, which is a different (and already-covered, by
	 * diff/runtime/jmpbuf-lifecycle.c's two-hundred-round case) shape
	 * than what this specimen is asking about. */
	static volatile unsigned char buf[64] __attribute__((aligned(16)));
	static volatile unsigned char buf2[64] __attribute__((aligned(16)));
	static volatile unsigned char buf3[64] __attribute__((aligned(16)));

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

		/* setjmp/longjmp's global-bound row -- the pair the
		 * predecessor of this file proved live first. */
		JMPBUF_ROUND(__jmpbuf_setjmp, __jmpbuf_longjmp, buf,
		             0x02, 0x04);

		/* _setjmp/_longjmp's weak/underscore row. */
		JMPBUF_ROUND(__jmpbuf__setjmp, __jmpbuf__longjmp, buf2,
		             0x10, 0x20);

		/* siglongjmp restoring a buffer setjmp saved -- see the
		 * file comment for why there is no matching sigsetjmp row
		 * to save it instead. */
		JMPBUF_ROUND(__jmpbuf_setjmp, __jmpbuf_siglongjmp, buf3,
		             0x40, 0x80);
	}

	leave(status);
}
