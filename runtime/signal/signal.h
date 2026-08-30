/*
 * signal.h -- signal delivery onto an ELF-side stack (WP-43).
 *
 * Cygwin delivers a signal by hijacking the target thread: it suspends it,
 * reads its register file, points it at a delivery stub and resumes. What the
 * stub then builds is a Cygwin frame for a Cygwin handler. An ELF process
 * expects something else, and this package is the something else: the frame a
 * Linux kernel builds, at the addresses the psABI and the Linux headers agree
 * on, from the register file the hijack captured.
 *
 * Three things follow from that and they are the whole package.
 *
 * The frame is a Linux rt_sigframe. A return address first, then ucontext_t,
 * then siginfo_t, with the extended FPU state above them and uc_mcontext.fpregs
 * pointing at it. A handler compiled against Linux headers walks uc_mcontext by
 * offset -- glibc's own unwinder does, and so does every crash reporter -- so
 * the offsets are asserted here rather than described.
 *
 * The frame is placed 128 bytes below the interrupted stack pointer. DR-0006
 * settled that the red zone is honoured at the delivery site rather than
 * compiled around, and this is the delivery site: elf_sigframe_place subtracts
 * ELFSYSV_REDZONE before it subtracts anything else, which is what a kernel
 * does and what Cygwin's sigdelayed does not. The alternate stack case skips
 * the reservation because it is not building on the interrupted stack at all.
 *
 * The return is an iretq. A handler returns to a trampoline, the trampoline
 * calls elfsysv_sigreturn, and the restore ends in a same-privilege iretq that
 * takes rip, rflags and rsp from a frame on the stack the runtime owns. The
 * alternative -- the setcontext shape, which pushes the return address below
 * the destination stack pointer and rets -- writes into the red zone this
 * package exists to preserve, so it is not available here. DR-0030 records the
 * choice and the measurement behind it.
 *
 * What the handler may have done to the frame is the hostile input. It ran
 * arbitrary code with a pointer to its own return state, and elfsysv_sigreturn
 * reads that state back and installs it in the register file. So
 * elf_sigframe_check re-derives every invariant from the bytes rather than
 * trusting the ones the placer maintained, and it is fuzzed against a guard
 * page.
 */

#ifndef ELFSYSV_RUNTIME_SIGNAL_H
#define ELFSYSV_RUNTIME_SIGNAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The psABI reservation. The one constant this package exists to respect. */
#define ELFSYSV_REDZONE 128

/*
 * Everything that crosses between this package's assembly and its C, and
 * everything the ELF world calls or is called by, is System V. That is not the
 * host compiler's default: gcc targeting x86_64-pc-cygwin emits and expects
 * the Microsoft convention, so a handler pointer or a trampoline entry that
 * does not say System V is compiled to read its arguments from the wrong
 * registers. WP-23 met the same seam from the other side and DR-0009 named it;
 * here it is written on every declaration that touches the assembly. Under the
 * cross compiler the attribute is what the target already does.
 */
#define ELF_SYSV __attribute__((sysv_abi))

/* Linux's _NSIG. Signal numbers run 1..64 and the mask is 64 bits wide. */
#define ELFSYSV_NSIG 64

/* ---- sigset_t ----------------------------------------------------------
 *
 * The kernel's set is 64 bits and the userspace one is 1024, and both are
 * true at once: the kernel writes eight bytes into a field the C library
 * declared as 128. The declared shape is the one a compiled handler sees, so
 * it is the one declared here, and the eight bytes that carry meaning are the
 * first word.
 */
#define ELFSYSV_SIGSET_WORDS 16

typedef struct {
	unsigned long __val[ELFSYSV_SIGSET_WORDS];
} elfsysv_sigset_t;

static inline void elf_sigemptyset(elfsysv_sigset_t *s)
{
	for (int i = 0; i < ELFSYSV_SIGSET_WORDS; i++)
		s->__val[i] = 0;
}

static inline void elf_sigset_from_mask(elfsysv_sigset_t *s, uint64_t m)
{
	elf_sigemptyset(s);
	s->__val[0] = (unsigned long)m;
}

static inline uint64_t elf_sigset_mask(const elfsysv_sigset_t *s)
{
	return (uint64_t)s->__val[0];
}

static inline uint64_t elf_sigbit(int signo)
{
	return (signo >= 1 && signo <= ELFSYSV_NSIG)
		       ? ((uint64_t)1 << (signo - 1))
		       : 0;
}

/* ---- siginfo_t ---------------------------------------------------------
 *
 * 128 bytes, three leading ints and a union. Only the members a delivery on
 * this platform can fill are named; the rest is the padding that keeps the
 * size and the member offsets what a compiled consumer computed.
 */
typedef union {
	int sival_int;
	void *sival_ptr;
} elfsysv_sigval_t;

typedef struct {
	int si_signo;
	int si_errno;
	int si_code;
	int __pad0;
	union {
		int __pad[28];
		struct {			/* SI_USER, SI_QUEUE, kill */
			int32_t si_pid;
			uint32_t si_uid;
			elfsysv_sigval_t si_value;
		} rt;
		struct {			/* SIGSEGV, SIGBUS */
			void *si_addr;
			short si_addr_lsb;
		} fault;
		struct {			/* SIGCHLD */
			int32_t si_pid;
			uint32_t si_uid;
			int si_status;
			long si_utime;
			long si_stime;
		} chld;
	} _sifields;
} elfsysv_siginfo_t;

/* ---- the FPU state -----------------------------------------------------
 *
 * The 512-byte fxsave image, with the last 48 bytes of its reserved tail
 * carrying the software record that tells a consumer an xsave area follows.
 * A consumer that knows only fxsave reads the first 512 bytes and is right; a
 * consumer that reads sw_reserved learns the extended size and the state
 * bitmap, and finds FP_XSTATE_MAGIC2 at the end of the extended area, which is
 * how it knows the area was not truncated.
 */
#define ELFSYSV_FP_XSTATE_MAGIC1 0x46505853u
#define ELFSYSV_FP_XSTATE_MAGIC2 0x46505845u
#define ELFSYSV_FP_XSTATE_MAGIC2_SIZE 4

typedef struct {
	uint32_t magic1;
	uint32_t extended_size;		/* fxsave + xstate + magic2          */
	uint64_t xstate_bv;
	uint32_t xstate_size;		/* fxsave + xstate, without magic2   */
	uint32_t padding[7];
} elfsysv_fpx_sw_bytes_t;

typedef struct {
	uint16_t cwd, swd, ftw, fop;
	uint64_t rip, rdp;
	uint32_t mxcsr, mxcsr_mask;
	uint32_t st_space[32];
	uint32_t xmm_space[64];
	uint32_t reserved2[12];
	union {
		uint32_t reserved3[12];
		elfsysv_fpx_sw_bytes_t sw_reserved;
	};
} elfsysv_fpstate_t;

/* The xsave header, immediately above the fxsave image. */
typedef struct {
	uint64_t xstate_bv;
	uint64_t xcomp_bv;
	uint64_t reserved[6];
} elfsysv_xstate_header_t;

/* ---- mcontext_t and ucontext_t ----------------------------------------- */

enum {
	ELF_REG_R8 = 0, ELF_REG_R9, ELF_REG_R10, ELF_REG_R11,
	ELF_REG_R12, ELF_REG_R13, ELF_REG_R14, ELF_REG_R15,
	ELF_REG_RDI, ELF_REG_RSI, ELF_REG_RBP, ELF_REG_RBX,
	ELF_REG_RDX, ELF_REG_RAX, ELF_REG_RCX, ELF_REG_RSP,
	ELF_REG_RIP, ELF_REG_EFL, ELF_REG_CSGSFS, ELF_REG_ERR,
	ELF_REG_TRAPNO, ELF_REG_OLDMASK, ELF_REG_CR2,
	ELF_NGREG
};

typedef long long elfsysv_greg_t;

typedef struct {
	elfsysv_greg_t gregs[ELF_NGREG];
	elfsysv_fpstate_t *fpregs;
	unsigned long long __reserved1[8];
} elfsysv_mcontext_t;

typedef struct {
	void *ss_sp;
	int ss_flags;
	size_t ss_size;
} elfsysv_stack_t;

typedef struct elfsysv_ucontext {
	unsigned long uc_flags;
	struct elfsysv_ucontext *uc_link;
	elfsysv_stack_t uc_stack;
	elfsysv_mcontext_t uc_mcontext;
	elfsysv_sigset_t uc_sigmask;
	elfsysv_fpstate_t __fpregs_mem;
} elfsysv_ucontext_t;

/* uc_flags bits Linux sets. UC_FP_XSTATE says fpregs is an xsave area rather
 * than a bare fxsave image; the two SHSTK bits are not ours to set and are
 * named so a reader does not think the field is one bit wide. */
#define ELFSYSV_UC_FP_XSTATE 0x1
#define ELFSYSV_UC_SIGCONTEXT_SS 0x2
#define ELFSYSV_UC_STRICT_RESTORE_SS 0x4

/* The frame, low address first. Linux's rt_sigframe exactly. */
typedef struct {
	uint64_t pretcode;
	elfsysv_ucontext_t uc;
	elfsysv_siginfo_t info;
} elfsysv_sigframe_t;

/* ---- sigaction --------------------------------------------------------- */

#define ELFSYSV_SA_NOCLDSTOP 0x00000001
#define ELFSYSV_SA_NOCLDWAIT 0x00000002
#define ELFSYSV_SA_SIGINFO 0x00000004
#define ELFSYSV_SA_ONSTACK 0x08000000
#define ELFSYSV_SA_RESTART 0x10000000
#define ELFSYSV_SA_NODEFER 0x40000000
#define ELFSYSV_SA_RESETHAND 0x80000000

#define ELFSYSV_SIG_DFL ((uintptr_t)0)
#define ELFSYSV_SIG_IGN ((uintptr_t)1)

#define ELFSYSV_SIG_BLOCK 0
#define ELFSYSV_SIG_UNBLOCK 1
#define ELFSYSV_SIG_SETMASK 2

#define ELFSYSV_SS_ONSTACK 1
#define ELFSYSV_SS_DISABLE 2
#define ELFSYSV_MINSIGSTKSZ 2048

typedef struct {
	uintptr_t sa_handler;		/* SIG_DFL, SIG_IGN or a function    */
	elfsysv_sigset_t sa_mask;
	int sa_flags;
	void (*sa_restorer)(void);
} elfsysv_sigaction_t;

/* ---- the register file the hijack captured -----------------------------
 *
 * Deliberately not a host CONTEXT. sig_host.c translates one of those into
 * this and back, and everything above that translation is written against a
 * plain register file so it can be driven by a test without a host thread in
 * the loop.
 */
typedef struct {
	uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
	uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
	uint64_t rip, rflags;
	uint16_t cs, ss, fs, gs;
	const void *fxsave;		/* 512-byte image, or NULL           */
} elfsysv_sigctx_t;

/* ---- the per-process signal state -------------------------------------- */

typedef struct {
	elfsysv_sigaction_t act[ELFSYSV_NSIG + 1];
	uint64_t blocked;		/* the calling thread's mask         */
	elfsysv_stack_t altstack;
	uint64_t cookie;		/* frame authenticator, see below    */
	int initialized;
} elfsysv_sigstate_t;

/* Disposition of one delivery attempt. */
typedef enum {
	ELF_SIG_DELIVERED = 0,		/* ctx rewritten, resume the thread  */
	ELF_SIG_IGNORED,		/* SIG_IGN, or ignored by default    */
	ELF_SIG_BLOCKED,		/* held for later                    */
	ELF_SIG_DEFAULT,		/* SIG_DFL with no ignore default    */
	ELF_SIG_REFUSED			/* the frame could not be placed     */
} elf_sig_disposition_t;

/* Where a frame was placed, and how far it stayed off the interrupted stack.
 * The reservation is reported rather than asserted so a test can hold the
 * placer to DR-0006 by arithmetic instead of by inspection. */
typedef struct {
	uintptr_t frame;		/* the pretcode word                 */
	uintptr_t fpstate;		/* the extended area                 */
	size_t fpstate_len;
	uintptr_t top;			/* one past the highest byte written */
	int on_altstack;
	size_t reserved;		/* bytes skipped below the old rsp   */
} elf_sig_placement_t;

void elf_sig_init(elfsysv_sigstate_t *st);
int elf_sig_action(elfsysv_sigstate_t *st, int signo,
		   const elfsysv_sigaction_t *act, elfsysv_sigaction_t *old);
int elf_sig_procmask(elfsysv_sigstate_t *st, int how,
		     const elfsysv_sigset_t *set, elfsysv_sigset_t *old);
int elf_sig_altstack(elfsysv_sigstate_t *st, uintptr_t cur_sp,
		     const elfsysv_stack_t *ss, elfsysv_stack_t *old);

/* True when `sp` is inside the enabled alternate stack. */
int elf_sig_on_altstack(const elfsysv_sigstate_t *st, uintptr_t sp);

/* SA_RESTART, as a question asked of an interrupted host call: the wrapper
 * that was interrupted asks whether to reissue the call or to fail EINTR. */
int elf_sig_restart_after(const elfsysv_sigstate_t *st, int signo);

/* Build the frame and rewrite `ctx` so that resuming the thread enters the
 * handler. `where` is filled on ELF_SIG_DELIVERED. */
elf_sig_disposition_t elf_sig_deliver(elfsysv_sigstate_t *st, int signo,
				      const elfsysv_siginfo_t *info,
				      elfsysv_sigctx_t *ctx,
				      elf_sig_placement_t *where);

/* How many bytes a frame needs at most, so an alternate stack can be sized. */
size_t elf_sig_frame_bytes(void);

/* The extended state this machine saves, decided once by cpuid. */
size_t elf_sig_xstate_size(void);
uint64_t elf_sig_xstate_mask(void);

/* The authenticator over a placed frame. Not a substitute for validation: it
 * is the cheap first question, and elf_sigframe_check asks the rest. */
uint64_t elf_sig_frame_cookie(const elfsysv_sigstate_t *st, uintptr_t frame,
			      uint64_t top);

/* Read a frame back the way sigreturn must: as bytes a handler could have
 * rewritten. `len` is what is readable from `frame` upward; `why` takes a
 * reason on refusal and may be NULL. Returns 1 to accept. */
#define ELF_SIG_WHY_MAX 96
int elf_sigframe_check(const elfsysv_sigstate_t *st, uintptr_t frame,
		       size_t len, char *why);

/* The rflags bits a return may set. Everything else is the host's. */
#define ELFSYSV_EFLAGS_MASK 0x00050DD5u
#define ELFSYSV_EFLAGS_FIXED 0x00000202u

/* ---- the asm halves ---------------------------------------------------- */

/* The address a handler returns to. Placed in the frame's pretcode word. */
extern void elfsysv_sig_return_tramp(void);

/* Restore a register file and iretq into it. Does not return. */
extern ELF_SYSV void elfsysv_sig_restore(const elfsysv_greg_t *gregs)
	__attribute__((noreturn));

/* Resume a register file the way SetThreadContext would, for tests that drive
 * a delivery without a host thread. Does not return. */
extern ELF_SYSV void elfsysv_sig_resume(const elfsysv_sigctx_t *ctx)
	__attribute__((noreturn));

/* Called by the return trampoline with the frame address. Does not return
 * unless the frame is refused, in which case it returns the refusal. */
ELF_SYSV int elfsysv_sigreturn(uintptr_t frame);

/* The state the trampoline finds its way back to. One per thread. */
extern __thread elfsysv_sigstate_t *elfsysv_sig_current;

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_RUNTIME_SIGNAL_H */
