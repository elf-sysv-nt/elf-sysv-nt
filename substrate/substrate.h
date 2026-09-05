/*
 * substrate.h -- the nine calls the core makes to run user code, as a C
 * contract.  This is the C form of doc/design/Substrate-Interface.md, "The
 * nine calls": a vtable a substrate fills in, plus the Linux-terms types the
 * calls take.  The mock, and later N and H, all implement this one header, so
 * the core is written once above it and never learns which substrate is below.
 *
 * The contract, not the signature, is the load-bearing part; each call carries
 * its one-line contract from the spec.  Where a signature had to be made
 * concrete to be compiled and tested (an out-parameter for a realised address,
 * a fault-address slot), the choice is noted rather than invented past what the
 * spec states.
 */
#ifndef SUBSTRATE_H
#define SUBSTRATE_H

#include <stddef.h>
#include <stdint.h>

/*
 * prot is the Linux PROT_* set, by its Linux values, so the core hands the
 * substrate what mmap() handed the core.  A substrate maps these to whatever
 * its host wants (NT page protections under the mock and N, guest page-table
 * bits under H); the core never sees that mapping.
 */
enum sub_prot {
	SUB_PROT_NONE  = 0x0,
	SUB_PROT_READ  = 0x1,
	SUB_PROT_WRITE = 0x2,
	SUB_PROT_EXEC  = 0x4,
};

/*
 * A backing descriptor names what stands behind a VMA.  The spec lists five
 * kinds; the core picks one per mapping.  For a file backing the bytes are
 * reached at `offset` (the as_map argument), which is why file_image/file_len
 * describe the whole file rather than the slice: the substrate does the
 * offsetting, as a real one does against an fd or a section.  Carrying the file
 * as an in-memory image keeps the interface host-neutral -- a real substrate
 * would hold an fd or an NT section here, and the mock says so where it reads
 * this.
 */
enum sub_backing_kind {
	SUB_BACKING_ANON,	/* anonymous, zero-fill on first touch */
	SUB_BACKING_FILE,	/* a file, read from `offset` */
	SUB_BACKING_SECTION,	/* a shared section */
	SUB_BACKING_VDSO,	/* the vDSO */
	SUB_BACKING_STACK,	/* the stack */
};

struct sub_backing {
	enum sub_backing_kind kind;
	const void *file_image;	/* SUB_BACKING_FILE: the file's bytes */
	size_t      file_len;	/* SUB_BACKING_FILE: their length */
};

/*
 * The register state the core reasons about: the x86-64 Linux user_regs_struct,
 * field for field and in its order.  The core builds these in Linux terms and
 * hands whole states across (invariant 5); the substrate maps the set to NT's
 * CONTEXT under N or to WHP registers under H.  fs_base and gs_base are carried
 * because they are part of the struct, but the thread pointer is set through
 * tp_set rather than by writing fs_base here -- see tp_set's contract.
 */
struct sub_regs {
	uint64_t r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;
	uint64_t rax, rcx, rdx, rsi, rdi, orig_rax;
	uint64_t rip, cs, eflags, rsp, ss;
	uint64_t fs_base, gs_base;
	uint64_t ds, es, fs, gs;
};

/*
 * The vtable.  Every call takes the substrate as its first argument -- the
 * `self` a C object carries where a language would carry `this` -- and returns
 * 0 on success or a negative value on failure, except where a call yields a
 * value through an out-parameter.  A tid is the Linux thread id the core
 * assigned; a substrate handle never crosses this line (invariant 3).
 */
struct substrate {
	void *self;

	/*
	 * as_map -- realise a VMA, or part of one.  After success a load or
	 * store the prot permits reads or writes the backing, and the range is
	 * the core's to record; the substrate keeps no map of its own.  An
	 * anonymous page reads zero on first touch.  Idempotent against the
	 * core's tree: re-realising a described range is not an error, because
	 * clone and demand paging both re-issue it.  Failure leaves the range
	 * exactly as it was.  *addr is the requested base (NULL lets the
	 * substrate choose) and is set to the realised base on success.
	 */
	int (*as_map)(struct substrate *s, void **addr, size_t len,
		      const struct sub_backing *backing, uint64_t offset,
		      int prot);

	/*
	 * as_unmap -- drop a range.  After success it faults on access until
	 * re-mapped and no longer appears in the map.  A range spanning or
	 * splitting VMAs is dropped exactly, leaving neighbours untouched.
	 */
	int (*as_unmap)(struct substrate *s, void *addr, size_t len);

	/*
	 * as_protect -- change protection at page granularity, no other effect;
	 * contents survive.  This is the one call a substrate may realise only
	 * to the page: the core keeps protection in its tree and asks for the
	 * page-rounded envelope (N cannot protect below a page; H is exact).
	 */
	int (*as_protect)(struct substrate *s, void *addr, size_t len,
			  int prot);

	/*
	 * as_clone -- duplicate the address space (fork's address-space half).
	 * The child's contents equal the parent's at the call, copy-on-write:
	 * a later write on either side is private to that side.  The new child
	 * substrate is returned through *child.
	 */
	int (*as_clone)(struct substrate *s, struct substrate **child);

	/*
	 * thread_start -- begin executing user code for tid from the register
	 * state ctx, thread pointer at tls.  The core supplies the whole state;
	 * the substrate invents none of it.  Called again on a tid stopped by
	 * thread_interrupt, it resumes that thread from ctx (the spec's
	 * "thread_start/re-entry resumes it").
	 */
	int (*thread_start)(struct substrate *s, int tid,
			    const struct sub_regs *ctx, uint64_t tls);

	/*
	 * thread_interrupt -- force tid out of user code and into the kernel
	 * soon, from another thread, spinning or blocked.  Soon is bounded, not
	 * immediate: a thread in the substrate's own kernel path finishes it
	 * first.  Resumable, so it is an interrupt, not a teardown.  An
	 * interrupt raised while tid is not interruptible is latched and taken
	 * at the next interruptible point, exactly once.
	 */
	int (*thread_interrupt)(struct substrate *s, int tid);

	/*
	 * thread_context / set_context -- read and replace the user register
	 * state of a thread not currently running user code (after an
	 * interrupt, or at a gate boundary).  set_context honours the red zone:
	 * the core builds a signal frame 128 bytes below the interrupted rsp
	 * (DR-0030, DR-0050) and the substrate must not clobber that gap.
	 */
	int (*thread_context)(struct substrate *s, int tid,
			      struct sub_regs *ctx);
	int (*set_context)(struct substrate *s, int tid,
			   const struct sub_regs *ctx);

	/*
	 * tp_set -- set the thread pointer for tid.  Under N the FS base does
	 * not survive a deschedule (spike 1), so the pointer is a runtime-owned
	 * word reached through %gs (DR-0003) and this writes it; under H it
	 * writes the guest FS MSR.  The one call whose two realisations differ
	 * in capability, not just mechanism.
	 */
	int (*tp_set)(struct substrate *s, int tid, uint64_t base);

	/*
	 * user_copy_in / user_copy_out -- the only way the core touches user
	 * memory; it never dereferences a user address directly (invariant 2).
	 * A copy that hits an unmapped or wrongly-protected user address fails
	 * with the fault address in *fault, and never crashes the kernel; the
	 * core turns that into EFAULT.  uaddr is the user address as a Linux
	 * pointer value.
	 */
	int (*user_copy_in)(struct substrate *s, void *dst, uint64_t uaddr,
			    size_t len, uint64_t *fault);
	int (*user_copy_out)(struct substrate *s, uint64_t uaddr,
			     const void *src, size_t len, uint64_t *fault);
};

/*
 * The runtime-side machinery that sits beside the nine core-facing calls, and
 * is not one of them.  The interrupt and thread-pointer groups exercise the
 * gate flag and the thread-pointer carrier the design names, and every
 * substrate provides these for its own userland: the gate the core enters and
 * leaves, and the %gs carrier (N) or FS base (H) a thread reads its pointer
 * through.  They are declared here, resolved from whichever substrate is
 * linked, so the conformance suite reads them the same way against any of them.
 */

/* The current thread's pointer, as its userland ABI reads it: the %gs carrier
 * word under N and the mock, the FS base under H.  tp_set feeds this. */
extern uint64_t substrate_thread_pointer(void);

/* The gate boundary.  A thread becomes non-interruptible on entry and
 * interruptible on exit; a thread_interrupt latched during the window is taken
 * at exit.  substrate_gate_exit returns how many latched interrupts it took
 * (0 or 1), which is the "exactly once" the interrupt contract promises. */
extern int substrate_gate_enter(struct substrate *s, int tid);
extern int substrate_gate_exit(struct substrate *s, int tid);

/*
 * The factory the conformance suite calls to obtain a fresh substrate to
 * certify.  Each substrate -- the mock, and later N and H -- provides one under
 * this name, so the suite links against whichever is under test and never names
 * a concrete substrate itself.
 */
extern struct substrate *substrate_create(void);

#endif /* SUBSTRATE_H */
