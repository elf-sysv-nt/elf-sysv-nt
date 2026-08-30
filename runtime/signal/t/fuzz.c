/* WP-43 fuzz target: the frame, read back the way sigreturn has to read it.
 *
 * Everything else in this package reads bytes it wrote itself a moment
 * earlier. elf_sigframe_check does not. Between the placement and the return
 * the handler ran, with a pointer to its own return state in rdx and nothing
 * stopping it from writing through it, and what comes back is then installed
 * into a register file and iretq'd into. A check that trusts one field of that
 * is a check that lets a handler choose the process's next instruction.
 *
 * So the frame is mutated, truncated and re-checked, and held to four
 * properties on every case:
 *
 *   1. No crash and no undefined behaviour. The frame is placed so its last
 *      byte abuts a guard page, so a read at frame[len] faults rather than
 *      returning adjacent bytes, and the binary is built with
 *      -fsanitize=undefined -fsanitize-undefined-trap-on-error. The recorded
 *      extent is inside the frame and is exactly the thing a handler can
 *      rewrite, which is why this property is the one that matters here.
 *
 *   2. Every refusal says something, because the reason is what a refused
 *      return has to report.
 *
 *   3. Every acceptance is re-derived from the bytes rather than believed:
 *      the authenticator over the address and the extent, the extent inside
 *      the readable region, the return address unchanged, an fpstate that
 *      lies inside the frame with its magic at both ends, and a resume
 *      address and stack pointer that a thread could actually be given.
 *
 *   4. Nothing outside the reason buffer is written, which is checked with a
 *      canary on each side of it.
 *
 * Usage:
 *   fuzz [options]
 *
 * Options:
 *   -n N, --count=N     cases to run [default: 200000]
 *   -s HEX, --seed=HEX  64-bit PRNG seed [default: 0x9e3779b97f4a7c15]
 *   -q, --quiet         errors only
 *   -h, --help          print this message and exit
 *
 * Exit: 0 every case held, 1 a property was violated, 2 usage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../sigpriv.h"
#include "../../../loader/elf/t/harness.h"

static uint64_t rng_state;

static uint64_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return rng_state;
}

static int quiet;

static ELF_SYSV void a_handler(int signo)
{
	(void)signo;
}

static int fail(const char *what, unsigned long i)
{
	printf("wp43 fuzz: case %lu violated: %s\n", i, what);
	return 1;
}

/* The acceptance conditions, recomputed here from the same bytes the checker
 * saw, with none of its intermediate results. */
static int rederive(const elfsysv_sigstate_t *st, uintptr_t frame, size_t len)
{
	const elfsysv_sigframe_t *f = (const elfsysv_sigframe_t *)frame;
	uint64_t top = (uint64_t)f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_TOP];

	if ((frame & 0xf) != 8)
		return 0;
	if (len < sizeof(elfsysv_sigframe_t))
		return 0;
	if (top <= (uint64_t)frame || top - (uint64_t)frame > (uint64_t)len)
		return 0;
	if ((uint64_t)f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_COOKIE] !=
	    elf_sig_frame_cookie(st, (uintptr_t)frame, top))
		return 0;
	if (f->pretcode != (uint64_t)(uintptr_t)elfsysv_sig_return_tramp)
		return 0;
	if (!elf_sig_canonical((uint64_t)f->uc.uc_mcontext.gregs[ELF_REG_RIP]))
		return 0;
	if (f->uc.uc_mcontext.gregs[ELF_REG_RIP] == 0)
		return 0;
	if (!elf_sig_canonical((uint64_t)f->uc.uc_mcontext.gregs[ELF_REG_RSP]))
		return 0;
	if (((uint64_t)f->uc.uc_mcontext.gregs[ELF_REG_RSP] & 7) != 0)
		return 0;
	if (f->info.si_signo < 1 || f->info.si_signo > ELFSYSV_NSIG)
		return 0;

	const elfsysv_fpstate_t *fp = f->uc.uc_mcontext.fpregs;
	if (fp) {
		uint64_t a = (uint64_t)(uintptr_t)fp;
		if (a < (uint64_t)frame || a >= top)
			return 0;
		if (a & 0x3f)
			return 0;
		if (top - a < sizeof(elfsysv_fpstate_t))
			return 0;
		if (f->uc.uc_flags & ELFSYSV_UC_FP_XSTATE) {
			uint32_t xs = fp->sw_reserved.xstate_size;
			if (fp->sw_reserved.magic1 != ELFSYSV_FP_XSTATE_MAGIC1)
				return 0;
			if (xs < 576 || (xs & 7) ||
			    top - a < xs + ELFSYSV_FP_XSTATE_MAGIC2_SIZE)
				return 0;
			uint32_t m2;
			memcpy(&m2, (const char *)fp + xs, sizeof(m2));
			if (m2 != ELFSYSV_FP_XSTATE_MAGIC2)
				return 0;
		}
	}
	return 1;
}

int main(int argc, char **argv)
{
	unsigned long cases = 200000;
	rng_state = 0x9e3779b97f4a7c15ull;

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-q") || !strcmp(a, "--quiet"))
			quiet = 1;
		else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			printf("usage: fuzz [-n cases] [-s seed] [-q]\n");
			return 0;
		} else if ((!strcmp(a, "-n") || !strcmp(a, "--count")) &&
			   i + 1 < argc)
			cases = strtoul(argv[++i], NULL, 0);
		else if ((!strcmp(a, "-s") || !strcmp(a, "--seed")) &&
			 i + 1 < argc)
			rng_state = strtoull(argv[++i], NULL, 0) |
				    (uint64_t)1;
		else {
			printf("fuzz: unknown option %s\n", a);
			return 2;
		}
	}

	size_t xs = elf_sig_xstate_size();
	size_t fpn = xs ? xs + ELFSYSV_FP_XSTATE_MAGIC2_SIZE
			: sizeof(elfsysv_fpstate_t);
	size_t n = sizeof(elfsysv_sigframe_t) + fpn + 128;
	n = (n & ~(size_t)0xf) + 8;	/* so the placed frame is at %16 == 8 */

	unsigned char *zeros = (unsigned char *)calloc(1, n);
	guard_buf g;
	if (!zeros || guard_load(zeros, n, &g) != 0) {
		printf("fuzz: could not build a guarded frame\n");
		return 1;
	}
	free(zeros);

	uintptr_t frame = (uintptr_t)g.base;
	if ((frame & 0xf) != 8) {
		printf("fuzz: guarded frame landed at %% 16 == %u\n",
		       (unsigned)(frame & 0xf));
		return 1;
	}
	uintptr_t top = frame + n;
	uintptr_t fps = (top - fpn) & ~(uintptr_t)0x3f;
	if (fps < frame + sizeof(elfsysv_sigframe_t)) {
		printf("fuzz: the guarded region cannot hold a frame\n");
		return 1;
	}

	elfsysv_sigstate_t st;
	elfsysv_sigaction_t sa;
	elfsysv_sigctx_t ctx;
	elf_sig_placement_t w;

	elf_sig_init(&st);
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = (uintptr_t)a_handler;
	memset(&ctx, 0, sizeof(ctx));
	ctx.rsp = top + ELFSYSV_REDZONE;
	ctx.rip = (uint64_t)(uintptr_t)a_handler;
	ctx.cs = 0x33;
	ctx.ss = 0x2b;

	memset(&w, 0, sizeof(w));
	w.frame = frame;
	w.fpstate = fps;
	w.fpstate_len = fpn;
	w.top = top;
	w.reserved = ELFSYSV_REDZONE;
	elf_sig_build(&st, 10, NULL, &sa, &ctx, &w, 0);

	unsigned char *pristine = (unsigned char *)malloc(n);
	if (!pristine) {
		printf("fuzz: out of memory\n");
		return 1;
	}
	memcpy(pristine, (void *)frame, n);

	struct {
		uint64_t low;
		char why[ELF_SIG_WHY_MAX];
		uint64_t high;
	} pad;

	unsigned long accepted = 0, refused = 0;

	for (unsigned long i = 0; i < cases; i++) {
		memcpy((void *)frame, pristine, n);

		/* One case in sixteen is left intact, so the loop keeps
		 * proving the placer's own bytes are acceptable rather than
		 * only that damage is caught. */
		unsigned muts = (rng() & 0xf) ? (unsigned)(rng() % 12) + 1 : 0;
		for (unsigned m = 0; m < muts; m++) {
			size_t off = (size_t)(rng() % n);
			unsigned char *p = (unsigned char *)frame + off;
			switch (rng() & 3) {
			case 0: *p = (unsigned char)rng(); break;
			case 1: *p ^= (unsigned char)(1u << (rng() & 7)); break;
			case 2: *p = 0; break;
			default: *p = 0xff; break;
			}
		}

		/* And the extent the caller believes is readable varies, so a
		 * checker that reads to its own idea of the end is caught. */
		size_t len = n;
		if ((rng() & 7) == 0)
			len = (size_t)(rng() % (n + 1));

		pad.low = 0xc0ffee0123456789ull;
		pad.high = 0x89abcdef0fedcba9ull;
		memset(pad.why, 0x5a, sizeof(pad.why));

		int r = elf_sigframe_check(&st, frame, len, pad.why);

		if (pad.low != 0xc0ffee0123456789ull ||
		    pad.high != 0x89abcdef0fedcba9ull)
			return fail("the reason buffer was overrun", i);

		if (r) {
			accepted++;
			if (!rederive(&st, frame, len))
				return fail("an acceptance does not re-derive",
					    i);
		} else {
			refused++;
			if (pad.why[0] == '\0')
				return fail("a refusal carried no reason", i);
			int terminated = 0;
			for (size_t k = 0; k < sizeof(pad.why); k++)
				if (pad.why[k] == '\0') {
					terminated = 1;
					break;
				}
			if (!terminated)
				return fail("a reason was not terminated", i);
		}
	}

	if (!quiet)
		printf("wp43 fuzz: %lu cases, %lu accepted, %lu refused, "
		       "no crash and no undefined behaviour\n",
		       cases, accepted, refused);
	free(pristine);
	guard_free(&g);
	return 0;
}
