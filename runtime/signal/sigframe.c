/*
 * sigframe.c -- where the frame goes, what is written into it, and what has to
 * be true of it when it comes back.
 *
 * The placement is the package's whole claim on DR-0006. A delivery that is
 * building on the interrupted stack subtracts ELFSYSV_REDZONE before it
 * subtracts anything else, so the highest byte the frame occupies is 128 below
 * the interrupted stack pointer and the reserved bytes are not written. A
 * delivery onto an alternate stack skips the reservation because it is not
 * building on the interrupted stack at all, and the interrupted stack's red
 * zone is preserved by not being addressed.
 *
 * The check is the package's hostile input. Between the placement and the
 * return, arbitrary code ran with a pointer to the frame, so nothing the
 * placer maintained may be assumed on the way back. The extent is authenticated
 * together with the frame address, because an extent a handler can rewrite
 * cannot bound a read; every pointer is bounded before it is followed; and the
 * FPU area is required to carry the magic at both ends before it is installed,
 * since xrstor with a bad state bitmap faults rather than failing.
 */

#include <string.h>

#include "sigpriv.h"

/* ---- layout assertions ------------------------------------------------- */

#define ELF_SASSERT(c, name) \
	typedef char elf_sig_assert_##name[(c) ? 1 : -1]

ELF_SASSERT(sizeof(elfsysv_fpstate_t) == 512, fpstate_size);
ELF_SASSERT(sizeof(elfsysv_fpx_sw_bytes_t) == 48, sw_bytes_size);
ELF_SASSERT(sizeof(elfsysv_siginfo_t) == 128, siginfo_size);
ELF_SASSERT(sizeof(elfsysv_sigset_t) == 128, sigset_size);
ELF_SASSERT(sizeof(elfsysv_stack_t) == 24, stack_size);
ELF_SASSERT(sizeof(elfsysv_mcontext_t) == 256, mcontext_size);
ELF_SASSERT(offsetof(elfsysv_ucontext_t, uc_stack) == 16, uc_stack_off);
ELF_SASSERT(offsetof(elfsysv_ucontext_t, uc_mcontext) == 40, uc_mcontext_off);
ELF_SASSERT(offsetof(elfsysv_ucontext_t, uc_sigmask) == 296, uc_sigmask_off);
/* glibc's oRSP: the offset the compiled world computes for the interrupted
 * stack pointer, from the base of ucontext_t. It is 160 and everything else
 * follows from it being 160. */
ELF_SASSERT(offsetof(elfsysv_ucontext_t, uc_mcontext.gregs) +
		    ELF_REG_RSP * 8 == 160, o_rsp);
ELF_SASSERT(offsetof(elfsysv_sigframe_t, uc) == 8, frame_uc_off);

/* The offsets sigenter.S reaches into elfsysv_sigctx_t by. */
ELF_SASSERT(offsetof(elfsysv_sigctx_t, rsp) == 56, ctx_rsp);
ELF_SASSERT(offsetof(elfsysv_sigctx_t, r11) == 88, ctx_r11);
ELF_SASSERT(offsetof(elfsysv_sigctx_t, rip) == 128, ctx_rip);
ELF_SASSERT(offsetof(elfsysv_sigctx_t, rflags) == 136, ctx_rflags);

/* ---- what this machine can save --------------------------------------- */

static int xstate_probed;
static size_t xstate_bytes;		/* fxsave + header + components      */
static uint64_t xstate_bits;

static void cpuid_count(uint32_t leaf, uint32_t sub, uint32_t r[4])
{
	__asm__ __volatile__("cpuid"
			     : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
			     : "a"(leaf), "c"(sub));
}

static uint64_t xgetbv0(void)
{
	uint32_t lo, hi;
	__asm__ __volatile__(".byte 0x0f,0x01,0xd0"
			     : "=a"(lo), "=d"(hi)
			     : "c"(0));
	return ((uint64_t)hi << 32) | lo;
}

/* Only the components a signal frame carries. AVX and AVX-512 state is what a
 * consumer of an extended frame is looking for; the supervisor components are
 * not ours and are masked out rather than trusted to be clear. */
static uint64_t xstate_wanted_mask(void)
{
	return 0x1 | 0x2 | 0x4		/* x87, SSE, AVX                      */
	       | 0x8 | 0x10		/* MPX bounds, still user state       */
	       | 0x20 | 0x40 | 0x80;	/* AVX-512 opmask, hi256, hi16        */
}

static void xstate_probe(void)
{
	uint32_t r[4];

	xstate_probed = 1;
	xstate_bytes = 0;
	xstate_bits = 0;

	cpuid_count(0, 0, r);
	if (r[0] < 0xd)
		return;
	cpuid_count(1, 0, r);
	if (!(r[2] & (1u << 27)))	/* OSXSAVE: xgetbv is not ours to run */
		return;

	uint64_t enabled = xgetbv0() & xstate_wanted_mask();
	if (!(enabled & 0x3))		/* x87 and SSE at least, or no xsave  */
		return;

	cpuid_count(0xd, 0, r);
	if (r[1] < 576)			/* ebx: bytes for the enabled set     */
		return;
	xstate_bits = enabled;
	xstate_bytes = r[1];
}

size_t elf_sig_xstate_size(void)
{
	if (!xstate_probed)
		xstate_probe();
	return xstate_bytes;
}

uint64_t elf_sig_xstate_mask(void)
{
	if (!xstate_probed)
		xstate_probe();
	return xstate_bits;
}

/* The area a frame reserves for FPU state: the extended size when this machine
 * has one, the bare fxsave image otherwise, plus the trailing magic. */
static size_t fp_area_bytes(void)
{
	size_t n = elf_sig_xstate_size();
	if (n == 0)
		return sizeof(elfsysv_fpstate_t);
	return n + ELFSYSV_FP_XSTATE_MAGIC2_SIZE;
}

size_t elf_sig_frame_bytes(void)
{
	return sizeof(elfsysv_sigframe_t) + fp_area_bytes() + 64 + 16 +
	       ELF_SIG_HEADROOM;
}

/* ---- saving and restoring the FPU ------------------------------------- */

size_t elf_sig_fpsave(void *area, const void *fxsave)
{
	elfsysv_fpstate_t *fp = (elfsysv_fpstate_t *)area;
	size_t xs = elf_sig_xstate_size();
	size_t total = fp_area_bytes();

	memset(area, 0, total);

	if (fxsave) {
		/* The hijack already has the interrupted thread's image; the
		 * runtime's own registers are not the ones being saved. */
		memcpy(fp, fxsave, sizeof(elfsysv_fpstate_t));
	} else if (xs) {
		uint64_t bits = elf_sig_xstate_mask();
		__asm__ __volatile__("xsave64 %0"
				     : "=m"(*(char *)area)
				     : "a"((uint32_t)bits),
				       "d"((uint32_t)(bits >> 32))
				     : "memory");
	} else {
		__asm__ __volatile__("fxsave64 %0"
				     : "=m"(*(char *)area)
				     :
				     : "memory");
	}

	if (xs) {
		elfsysv_xstate_header_t *h =
			(elfsysv_xstate_header_t *)((char *)area + 512);
		if (fxsave)
			h->xstate_bv = 0x3;
		h->xcomp_bv = 0;
		fp->sw_reserved.magic1 = ELFSYSV_FP_XSTATE_MAGIC1;
		fp->sw_reserved.xstate_size = (uint32_t)xs;
		fp->sw_reserved.extended_size = (uint32_t)total;
		fp->sw_reserved.xstate_bv = h->xstate_bv;
		*(uint32_t *)((char *)area + xs) = ELFSYSV_FP_XSTATE_MAGIC2;
	}
	return total;
}

void elf_sig_fprestore(const elfsysv_fpstate_t *fp, uint64_t uc_flags)
{
	if (!fp)
		return;
	if ((uc_flags & ELFSYSV_UC_FP_XSTATE) && elf_sig_xstate_size()) {
		uint64_t bits = fp->sw_reserved.xstate_bv &
				elf_sig_xstate_mask();
		__asm__ __volatile__("xrstor64 %0"
				     :
				     : "m"(*(const char *)fp),
				       "a"((uint32_t)bits),
				       "d"((uint32_t)(bits >> 32))
				     : "memory");
	} else {
		__asm__ __volatile__("fxrstor64 %0"
				     :
				     : "m"(*(const char *)fp)
				     : "memory");
	}
}

/* ---- the authenticator ------------------------------------------------- */

static uint64_t mix(uint64_t x)
{
	x ^= x >> 30;
	x *= 0xbf58476d1ce4e5b9ull;
	x ^= x >> 27;
	x *= 0x94d049bb133111ebull;
	x ^= x >> 31;
	return x;
}

static uint64_t cookie_of(const elfsysv_sigstate_t *st, uint64_t frame,
			  uint64_t top)
{
	uint64_t h = st->cookie ^ mix(frame);
	h = mix(h ^ (top * 0x9e3779b97f4a7c15ull));
	return h | 1;			/* never zero, so zeroed bytes fail  */
}

uint64_t elf_sig_frame_cookie(const elfsysv_sigstate_t *st, uintptr_t frame)
{
	return cookie_of(st, (uint64_t)frame, 0);
}

/* ---- placement --------------------------------------------------------- */

static uintptr_t round_down(uintptr_t v, uintptr_t a)
{
	return v & ~(a - 1);
}

int elf_sig_place(const elfsysv_sigstate_t *st, const elfsysv_sigaction_t *sa,
		  const elfsysv_sigctx_t *ctx, elf_sig_placement_t *where)
{
	uintptr_t sp = (uintptr_t)ctx->rsp;
	uintptr_t floor = 0;
	int on_alt = 0;
	size_t reserved = 0;

	memset(where, 0, sizeof(*where));

	if ((sa->sa_flags & ELFSYSV_SA_ONSTACK) && st->altstack.ss_sp &&
	    !(st->altstack.ss_flags & ELFSYSV_SS_DISABLE) &&
	    !elf_sig_on_altstack(st, sp)) {
		floor = (uintptr_t)st->altstack.ss_sp;
		sp = floor + st->altstack.ss_size;
		on_alt = 1;
	} else {
		/* DR-0006. The reserved bytes are skipped before anything is
		 * subtracted, which is the whole of the repair. */
		sp -= ELFSYSV_REDZONE;
		reserved = ELFSYSV_REDZONE;
	}

	uintptr_t top = sp;
	size_t fpn = fp_area_bytes();

	if (sp < fpn)
		return 0;
	sp -= fpn;
	sp = round_down(sp, 64);
	uintptr_t fp = sp;

	if (sp < sizeof(elfsysv_sigframe_t))
		return 0;
	sp -= sizeof(elfsysv_sigframe_t);
	sp = round_down(sp, 16) - 8;	/* rsp % 16 == 8 at handler entry    */

	if (on_alt && (sp < floor || sp - floor < ELF_SIG_HEADROOM))
		return 0;
	if (!on_alt && sp < ELF_SIG_HEADROOM)
		return 0;

	where->frame = sp;
	where->fpstate = fp;
	where->fpstate_len = fpn;
	where->top = top;
	where->on_altstack = on_alt;
	where->reserved = reserved;
	return 1;
}

/* ---- writing it -------------------------------------------------------- */

void elf_sig_build(const elfsysv_sigstate_t *st, int signo,
		   const elfsysv_siginfo_t *info,
		   const elfsysv_sigaction_t *sa, elfsysv_sigctx_t *ctx,
		   const elf_sig_placement_t *where, uint64_t new_mask)
{
	elfsysv_sigframe_t *f = (elfsysv_sigframe_t *)where->frame;
	elfsysv_greg_t *g = f->uc.uc_mcontext.gregs;

	memset(f, 0, sizeof(*f));

	elf_sig_fpsave((void *)where->fpstate, ctx->fxsave);

	g[ELF_REG_R8] = (elfsysv_greg_t)ctx->r8;
	g[ELF_REG_R9] = (elfsysv_greg_t)ctx->r9;
	g[ELF_REG_R10] = (elfsysv_greg_t)ctx->r10;
	g[ELF_REG_R11] = (elfsysv_greg_t)ctx->r11;
	g[ELF_REG_R12] = (elfsysv_greg_t)ctx->r12;
	g[ELF_REG_R13] = (elfsysv_greg_t)ctx->r13;
	g[ELF_REG_R14] = (elfsysv_greg_t)ctx->r14;
	g[ELF_REG_R15] = (elfsysv_greg_t)ctx->r15;
	g[ELF_REG_RDI] = (elfsysv_greg_t)ctx->rdi;
	g[ELF_REG_RSI] = (elfsysv_greg_t)ctx->rsi;
	g[ELF_REG_RBP] = (elfsysv_greg_t)ctx->rbp;
	g[ELF_REG_RBX] = (elfsysv_greg_t)ctx->rbx;
	g[ELF_REG_RDX] = (elfsysv_greg_t)ctx->rdx;
	g[ELF_REG_RAX] = (elfsysv_greg_t)ctx->rax;
	g[ELF_REG_RCX] = (elfsysv_greg_t)ctx->rcx;
	g[ELF_REG_RSP] = (elfsysv_greg_t)ctx->rsp;
	g[ELF_REG_RIP] = (elfsysv_greg_t)ctx->rip;
	g[ELF_REG_EFL] = (elfsysv_greg_t)ctx->rflags;
	g[ELF_REG_CSGSFS] = (elfsysv_greg_t)((uint64_t)ctx->cs |
					     ((uint64_t)ctx->gs << 16) |
					     ((uint64_t)ctx->fs << 32) |
					     ((uint64_t)ctx->ss << 48));
	g[ELF_REG_OLDMASK] = (elfsysv_greg_t)st->blocked;

	f->uc.uc_flags = elf_sig_xstate_size() ? ELFSYSV_UC_FP_XSTATE : 0;
	f->uc.uc_link = 0;
	f->uc.uc_stack = st->altstack;
	f->uc.uc_stack.ss_flags = where->on_altstack ? ELFSYSV_SS_ONSTACK
						     : st->altstack.ss_flags;
	f->uc.uc_mcontext.fpregs = (elfsysv_fpstate_t *)where->fpstate;
	elf_sigset_from_mask(&f->uc.uc_sigmask, st->blocked);

	f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_TOP] = where->top;
	f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_PLACE] =
		(where->on_altstack ? ELF_SIG_PLACED_ALTSTACK : 0) |
		(where->reserved ? ELF_SIG_PLACED_RESERVED : 0);
	f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_COOKIE] =
		cookie_of(st, (uint64_t)where->frame, (uint64_t)where->top);

	if (info)
		f->info = *info;
	else {
		f->info.si_signo = signo;
		f->info.si_code = 0;
	}
	f->info.si_signo = signo;

	f->pretcode = (uint64_t)(uintptr_t)elfsysv_sig_return_tramp;

	/* Enter the handler. A Linux delivery arrives with rax zero, the three
	 * System V argument registers loaded, and rsp at the frame; a handler
	 * declared either way then reads the arguments it declared. */
	ctx->rsp = (uint64_t)where->frame;
	ctx->rip = (uint64_t)sa->sa_handler;
	ctx->rdi = (uint64_t)signo;
	ctx->rsi = (sa->sa_flags & ELFSYSV_SA_SIGINFO)
			   ? (uint64_t)(uintptr_t)&f->info
			   : 0;
	ctx->rdx = (sa->sa_flags & ELFSYSV_SA_SIGINFO)
			   ? (uint64_t)(uintptr_t)&f->uc
			   : 0;
	ctx->rax = 0;
	ctx->rflags = (ctx->rflags & ~(uint64_t)ELFSYSV_EFLAGS_MASK) |
		      ELFSYSV_EFLAGS_FIXED;
	(void)new_mask;
}

/* ---- reading it back --------------------------------------------------- */

static int refuse(char *why, const char *what)
{
	if (why) {
		size_t i = 0;
		while (what[i] && i < ELF_SIG_WHY_MAX - 1) {
			why[i] = what[i];
			i++;
		}
		why[i] = '\0';
	}
	return 0;
}

int elf_sigframe_check(const elfsysv_sigstate_t *st, uintptr_t frame,
		       size_t len, char *why)
{
	if (why)
		why[0] = '\0';

	if (frame == 0)
		return refuse(why, "frame address is zero");
	if ((frame & 0xf) != 8)
		return refuse(why, "frame is not at rsp%16==8");
	if (len < sizeof(elfsysv_sigframe_t))
		return refuse(why, "readable region is shorter than a frame");
	if (!elf_sig_canonical((uint64_t)frame))
		return refuse(why, "frame address is not canonical");

	const elfsysv_sigframe_t *f = (const elfsysv_sigframe_t *)frame;
	const elfsysv_greg_t *g = f->uc.uc_mcontext.gregs;
	uint64_t top = (uint64_t)f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_TOP];
	uint64_t got = (uint64_t)f->uc.uc_mcontext.__reserved1[ELF_SIG_RSV_COOKIE];

	if (top <= (uint64_t)frame)
		return refuse(why, "recorded extent does not exceed the frame");
	if (top - (uint64_t)frame > (uint64_t)len)
		return refuse(why, "recorded extent runs past readable bytes");
	if (got != cookie_of(st, (uint64_t)frame, top))
		return refuse(why, "frame authenticator does not match");

	if (f->pretcode != (uint64_t)(uintptr_t)elfsysv_sig_return_tramp)
		return refuse(why, "return address in the frame was rewritten");

	if (!elf_sig_canonical((uint64_t)g[ELF_REG_RIP]) ||
	    (uint64_t)g[ELF_REG_RIP] == 0)
		return refuse(why, "resume address is not canonical");
	if (!elf_sig_canonical((uint64_t)g[ELF_REG_RSP]) ||
	    ((uint64_t)g[ELF_REG_RSP] & 0x7) != 0)
		return refuse(why, "resume stack pointer is unusable");

	uint64_t csgsfs = (uint64_t)g[ELF_REG_CSGSFS];
	if ((csgsfs & 0xffff) == 0)
		return refuse(why, "code selector in the frame is null");

	if (f->info.si_signo < 1 || f->info.si_signo > ELFSYSV_NSIG)
		return refuse(why, "siginfo carries no valid signal number");

	const elfsysv_fpstate_t *fp = f->uc.uc_mcontext.fpregs;
	if (fp) {
		uint64_t a = (uint64_t)(uintptr_t)fp;
		if (a < (uint64_t)frame || a >= top)
			return refuse(why, "fpstate lies outside the frame");
		if (a & 0x3f)
			return refuse(why, "fpstate is not 64-byte aligned");
		if (top - a < sizeof(elfsysv_fpstate_t))
			return refuse(why, "fpstate does not fit below the top");
		if (f->uc.uc_flags & ELFSYSV_UC_FP_XSTATE) {
			uint32_t xs = fp->sw_reserved.xstate_size;
			uint32_t ext = fp->sw_reserved.extended_size;
			if (fp->sw_reserved.magic1 != ELFSYSV_FP_XSTATE_MAGIC1)
				return refuse(why, "xstate magic is absent");
			if (xs < 576 || ext < xs +
					      ELFSYSV_FP_XSTATE_MAGIC2_SIZE)
				return refuse(why, "xstate sizes disagree");
			if (top - a < ext)
				return refuse(why, "xstate runs past the top");
			if (fp->sw_reserved.xstate_bv &
			    ~elf_sig_xstate_mask())
				return refuse(why,
					      "xstate names a component this "
					      "machine does not have");
			uint32_t m2 = *(const uint32_t *)((const char *)fp + xs);
			if (m2 != ELFSYSV_FP_XSTATE_MAGIC2)
				return refuse(why, "xstate trailing magic is absent");
		}
	}

	const elfsysv_stack_t *ss = &f->uc.uc_stack;
	if (ss->ss_size > (size_t)1 << 40)
		return refuse(why, "uc_stack size is implausible");
	if (ss->ss_sp && !elf_sig_canonical((uint64_t)(uintptr_t)ss->ss_sp))
		return refuse(why, "uc_stack base is not canonical");

	return 1;
}
