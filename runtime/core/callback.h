/*
 * callback.h -- System V to Microsoft callback trampolines (WP-23).
 *
 * This is the direction WP-22 marked and left: a function pointer the ELF world
 * writes in System V and hands *down* to Windows, which then calls it Microsoft
 * x64. A qsort comparator given to a host qsort, a thread start routine given to
 * CreateThread, an exception filter given to AddVectoredExceptionHandler -- each
 * is a System V body reached by an MS-ABI caller, and the convention has to be
 * bridged at the pointer or it leaks.
 *
 * The leak is asymmetric and one-directional, which is what makes this the half
 * WP-22's core deferred. %rsi, %rdi and %xmm6-%xmm15 are callee-saved to a
 * Microsoft caller and volatile to a System V callee: the caller is entitled to
 * find them intact after the call, and the callee is entitled to have destroyed
 * them. A raw pointer handed across satisfies neither entitlement at once, and
 * the symptom is the Windows caller's own registers quietly changing, not a
 * crash. Spike 3 measured this exact set surviving a correct crossing and named
 * it "the direction the design was nervous about".
 *
 * The trampoline is an ms_abi entry Windows can hold and call, bound to a System
 * V target read from a slot. The convention change -- the argument shuffle from
 * Microsoft's positions to System V's, and the save and restore of the wider
 * Microsoft callee-saved set around the call -- is the compiler's, emitted from
 * the sysv_abi type of the slot, the same crossing spike 3 measured holding and
 * the mirror of DR-0009's down-call wrapper. The trampoline carries the SEH
 * unwind data gcc emits for an ms_abi frame, so it is the one place the host's
 * unwinder may walk to and stop at, which is the seam DR-0012 reserves for it.
 *
 * Why a slot and not a compile-time name: the seam exists to forward an
 * arbitrary caller-supplied pointer, so the target is data, not a symbol. Why
 * one slot per shape and no runtime code generation: a distinct pointer per live
 * callback would need a distinct compiled entry, and manufacturing entries at
 * run time is the self-mapped executable memory DR-0000 records the platform
 * cannot ship. One live callback per shape is what the fixed set of compiled
 * trampolines provides; DR-0020 records the reasoning and the boundary.
 *
 * Everything here compiles -mno-red-zone with the rest of the core (DR-0006).
 */

#ifndef ELFSYSV_RUNTIME_CALLBACK_H
#define ELFSYSV_RUNTIME_CALLBACK_H

#include <stdint.h>

#include "core.h"

/*
 * The seam core.h marks as ELFSYSV_WP23_SEAM is filled by this header's
 * existence. A unit that has included callback.h may assert it:
 *     _Static_assert(ELFSYSV_WP23_FILLED, "seam filled");
 */
#define ELFSYSV_WP23_FILLED 1

/*
 * The System V callback shapes, as the ELF world writes them. The sysv_abi
 * attribute on the pointer type is load-bearing: it is what makes the compiler
 * emit the Microsoft-to-System-V crossing at the call inside the trampoline
 * rather than a same-convention call that would pass the arguments in the wrong
 * registers and leave the caller's saved set destroyed.
 */
typedef __attribute__((sysv_abi)) int32_t
	(*elfsysv_sysv_comparator)(const void *a, const void *b);
typedef __attribute__((sysv_abi)) uint32_t
	(*elfsysv_sysv_threadproc)(void *arg);
typedef __attribute__((sysv_abi)) int32_t
	(*elfsysv_sysv_exfilter)(void *exception_pointers);

/*
 * The Microsoft-faced pointer types Windows holds. These are what a host API
 * that takes a comparator, a LPTHREAD_START_ROUTINE, or a vectored handler
 * expects, and they are what the register functions below return.
 */
typedef __attribute__((ms_abi)) int32_t
	(*elfsysv_ms_comparator)(const void *a, const void *b);
typedef __attribute__((ms_abi)) uint32_t
	(*elfsysv_ms_threadproc)(void *arg);
typedef __attribute__((ms_abi)) int32_t
	(*elfsysv_ms_exfilter)(void *exception_pointers);

/*
 * Bind a System V callback to its shape's trampoline and return the ms_abi
 * pointer to hand to Windows. Passing a null target clears the slot. The
 * returned pointer is a fixed compiled trampoline, one per shape; a second
 * registration of the same shape rebinds the one slot rather than minting a new
 * trampoline, so only the most recent target of a shape is live. NULL comes
 * back only if the trampoline could not be handed out, which at this width it
 * always can.
 */
elfsysv_ms_comparator elfsysv_cb_set_comparator(elfsysv_sysv_comparator fn);
elfsysv_ms_threadproc elfsysv_cb_set_threadproc(elfsysv_sysv_threadproc fn);
elfsysv_ms_exfilter   elfsysv_cb_set_exfilter(elfsysv_sysv_exfilter fn);

/*
 * The trampolines themselves, named so a test can point the host's
 * RtlLookupFunctionEntry at them and confirm they carry unwind data. Ordinary
 * callers reach them through the register functions and never name them.
 */
ELFSYSV_MSABI int32_t  elfsysv_tramp_comparator(const void *a, const void *b);
ELFSYSV_MSABI uint32_t elfsysv_tramp_threadproc(void *arg);
ELFSYSV_MSABI int32_t  elfsysv_tramp_exfilter(void *exception_pointers);

#endif /* ELFSYSV_RUNTIME_CALLBACK_H */
