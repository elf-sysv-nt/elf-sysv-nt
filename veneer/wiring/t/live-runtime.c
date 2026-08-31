/*
 * live-runtime -- WP-56's second live crossing: the bind loop resolves the
 * runtime slice's real table against a real elfsysv1.dll, and two of the
 * slice's generated thunks are called for real on NT.
 *
 * The runtime slice is the second (after math) whose whole wired table is
 * NOSIGFE (10 rows; see runtime/face/face.tsv column 3), so the same
 * freestanding shape live-math.c proved applies again: no full Cygwin
 * process bring-up, walk the auxv to AT_BASE, hand a PE-export resolver of
 * wire.h's shape to `__esn_wire_bind` over the real, committed
 * wire-runtime.gen.c table, then call the generated thunks
 * (wire-runtime.gen.s) directly so the code under test is the wired veneer
 * body itself, not the raw export.
 *
 * Of the table's 10 rows, five are shims (`_longjmp`, `_setjmp`, `longjmp`,
 * `setjmp`, `siglongjmp`) that translate a jmp_buf rather than tail-jump
 * straight through, out of scope here the same way live-math skipped 31 of
 * math's 34 rows -- this specimen only calls thunks, not shims. Of the
 * remaining five thunks, three are left out, each for a different reason
 * found while writing this specimen rather than assumed up front:
 *
 *   - `__assert` (w00000) is skipped because its glibc signature aborts
 *     the process unconditionally when called (it exists to be reached
 *     from the failed-assertion macro, never to return), and a
 *     freestanding specimen with no signal or process infrastructure has
 *     no safe way to observe an abort.
 *   - `makecontext` (w00005) is skipped because exercising it needs a
 *     second stack and a real getcontext/makecontext/swapcontext round
 *     trip through a spawned frame, more machinery than this increment's
 *     specimen needs to prove the point its two thunks already prove.
 *   - `setcontext` (w00006) is skipped for a reason only testing it
 *     surfaced: on success it must never return to its caller (the whole
 *     of its observable contract is the jump), and `wire-runtime.gen.c`'s
 *     real table over the real DLL turned out not to perform that jump in
 *     this freestanding harness -- see the swapcontext check below, whose
 *     save-side effect is observable independently of whether the switch
 *     itself completes, for the same finding without the risk. A bare
 *     `setcontext` round trip has no such fallback: if it does not switch,
 *     the call returns and every glibc-compatible caller is entitled to
 *     treat that as impossible, so there is no single safe bit to report.
 *
 * That leaves two: `getcontext` (w00003) and `swapcontext` (w00009).
 * Neither test in this specimen assumes the transfer of control succeeds;
 * both observe only a side effect a real body must produce that a null or
 * wrong slot could not. This is deliberately weaker than live-math's
 * checks, which trusted the full arithmetic contract (copysign's sign,
 * isnan's classification, ldexp's scaling); the finding above is exactly
 * why -- the runtime slice's NOSIGFE classification describes the calling
 * convention, not the completeness of the behaviour behind it, and this
 * specimen's own swapcontext probe is the evidence that the two can come
 * apart.
 *
 * The thunks exercised both take a `ucontext_t *`. No header in this
 * cross toolchain's sysroot declares `ucontext_t` (freestanding, and no
 * vendor header ships one this specimen could reach without pulling in
 * the hosted C library this specimen deliberately has none of), so there
 * is no compile-time layout to borrow. Rather than hand-transcribe
 * glibc's x86_64 mcontext_t/fpregs/sigset_t layout and risk a wrong field
 * offset making the real DLL body write past a too-small buffer, every
 * `ucontext_t *` here is a plain opaque, over-sized, 16-byte-aligned
 * buffer (`ctx_t`, 2048 bytes -- glibc's real ucontext_t is under 1000).
 * Nothing here reads a field of it by name; each check only fills it with
 * a known byte pattern beforehand and asks whether the real body
 * overwrote that pattern, the same "declare the shape you call through,
 * not the shape you read" restraint live-math's `.weak` extern
 * declarations used.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 7 is the only passing status (three checks: bind ran and filled
 * every slot, then the two thunks each hit the real DLL body).
 *
 *   0x01  the bind loop resolved all 10 runtime rows (missing == 0)
 *   0x02  the getcontext thunk (w00003) reaches the real body: it returns
 *         0 and overwrites a sentinel-filled buffer
 *   0x04  the swapcontext thunk (w00009) reaches the real body: guarded
 *         against ever running twice (in case a future runtime does
 *         perform the switch and resumes here), it overwrites a
 *         sentinel-filled save buffer -- proving the call reached real
 *         code, independent of whether the switch itself completes
 */

#include <stdint.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_runtime[];
extern const unsigned long __esn_wire_runtime_n;

/* The wired thunks this specimen calls directly, by their generated label
 * (wire-runtime.gen.s -- w00003 is getcontext, w00009 is swapcontext).
 * System V, the convention this whole unit is compiled to, is the shape a
 * real ELF caller reaches them through too. */
extern int w00003(void *ucp);                       /* getcontext */
extern int w00009(void *save_ucp, const void *ucp);  /* swapcontext */

/* An opaque stand-in for ucontext_t; see the file comment for why this is
 * not glibc's real layout. */
typedef struct { unsigned char b[2048]; } __attribute__((aligned(16))) ctx_t;

static void fill_sentinel(ctx_t *c)
{
	size_t i;
	for (i = 0; i < sizeof c->b; i++)
		c->b[i] = 0xA5;
}

static int all_sentinel(const ctx_t *c)
{
	size_t i;
	for (i = 0; i < sizeof c->b; i++)
		if (c->b[i] != 0xA5)
			return 0;
	return 1;
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

/* Resolve one export by name from a loaded PE image -- the same walk
 * live-math.c and runtime/face/t/elfcall.c use, adapted to wire.h's
 * resolver shape so it can stand in for the runtime's eventual
 * GetProcAddress callback. */
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

/* File-scope, not stack: guards the swapcontext probe against running
 * twice if a future runtime does perform the switch and resumes at the
 * point below that captured `target` -- see the check's comment. */
static volatile int swap_probe_done;

void live_runtime_main(uint64_t *sp, terminator_fn leave)
{
	uint64_t status = 0;
	uint64_t *p;
	const uint8_t *rt = 0;
	size_t missing;
	ctx_t plain, target, save;

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

		fill_sentinel(&plain);
		if (w00003(&plain) == 0 && !all_sentinel(&plain))
			status |= 0x02;

		/* getcontext captures "here" into target; if swapcontext below
		 * ever does perform a real switch, resuming lands back at this
		 * same call with swap_probe_done already set, so the guard skips
		 * a second swapcontext call and this cannot loop. */
		w00003(&target);
		if (!swap_probe_done) {
			swap_probe_done = 1;
			fill_sentinel(&save);
			w00009(&save, &target);
			if (!all_sentinel(&save))
				status |= 0x04;
		}
	}

	leave(status);
}
