/*
 * gate.h -- the syscall gate, the door userland reaches instead of the syscall
 * instruction.  Substrate N runs user code as native NT threads and cannot trap
 * `syscall` (spike 1), so the core publishes the gate's address in the auxiliary
 * vector as AT_SYSINFO and the program calls it.  gate_entry is that address;
 * everything else here is how the C side dispatches what it delivers.
 */
#ifndef CORE_GATE_H
#define CORE_GATE_H

#include <stdint.h>

#include "substrate.h"

/*
 * A captured syscall: the number and its six arguments, in the order the Linux
 * convention passes them.  gate_entry (gate.S) builds one of these on the kernel
 * stack from %rax and %rdi %rsi %rdx %r10 %r8 %r9, and hands its address to
 * gate_dispatch.  Passing one pointer sidesteps the Linux-to-Win64 argument
 * remapping that the host compiler would otherwise force into the assembly.
 *
 * The widths are fixed, never `long`.  The host compiler is LLP64, where `long`
 * is 32 bits, while every word the gate pushes and every value the Linux ABI
 * passes is 64.  Declared as `long`, this structure reads each argument as half
 * a register -- a1 becomes the high half of %rax, a2 the low half of %rdi -- and
 * a returned negative errno goes back to userland zero-extended rather than
 * sign-extended.  Nothing about that is visible in a build log.
 */
struct sysframe {
	int64_t nr;
	int64_t a1, a2, a3, a4, a5, a6;
};

/*
 * The gate entry, in assembly.  Reached by a plain `call` from user code with
 * the Linux syscall register convention -- number in %rax, arguments in
 * %rdi %rsi %rdx %r10 %r8 %r9, result in %rax, %rcx and %r11 clobbered.  It
 * switches to the kernel stack, calls gate_dispatch, restores the user state,
 * and returns.  Its address is what goes in AT_SYSINFO.
 */
extern void gate_entry(void);

/* The C half.  Runs on the kernel stack; its return value becomes %rax. */
int64_t gate_dispatch(const struct sysframe *f);

/*
 * Wire the gate before any user code runs: the substrate and tid it brackets
 * the gate window against, and the top of the kernel stack gate_entry switches
 * onto.  kstack_top is one past the highest usable byte, 16-aligned.
 */
void gate_init(struct substrate *s, int tid, void *kstack_top);

#endif /* CORE_GATE_H */
