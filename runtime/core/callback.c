/*
 * callback.c -- the callback trampolines and their target slots (WP-23).
 *
 * Each trampoline is one ms_abi function whose body is a call through a slot
 * typed sysv_abi. That single fact is the whole mechanism: because the slot's
 * type is sysv_abi and the function's is ms_abi, gcc emits the convention
 * crossing at the call -- the argument shuffle from Microsoft's registers to
 * System V's, and the save and restore of %rsi, %rdi and %xmm6-%xmm15, the set
 * a Microsoft caller keeps and a System V callee may destroy. The generated
 * prologue carries .seh_ directives, so the trampoline frame is one the host's
 * own unwinder can walk, which is what lets it be the stopping point DR-0012
 * reserves rather than a frame the host walks past into System V code.
 *
 * The slot is a plain mutable pointer, read at the call rather than propagated
 * as a constant, because the seam forwards a caller-supplied target and the
 * register functions write it at run time. There is one slot per shape and no
 * manufacturing of new entries: DR-0000 forecloses self-mapped executable
 * memory, so a per-instance trampoline is not available and DR-0020 records the
 * one-live-callback-per-shape boundary that follows.
 *
 * Compiled -mno-red-zone with the rest of the core.
 */

#include "callback.h"

/*
 * The target slots. A registration writes one; a trampoline reads one. They are
 * the ELF-world pointers the host is really calling, one indirection removed so
 * the trampoline can be a fixed compiled entry the host can hold.
 */
static elfsysv_sysv_comparator cb_slot_comparator;
static elfsysv_sysv_threadproc cb_slot_threadproc;
static elfsysv_sysv_exfilter   cb_slot_exfilter;

/* ---- the trampolines --------------------------------------------------- */

ELFSYSV_MSABI int32_t elfsysv_tramp_comparator(const void *a, const void *b)
{
	return cb_slot_comparator(a, b);
}

ELFSYSV_MSABI uint32_t elfsysv_tramp_threadproc(void *arg)
{
	return cb_slot_threadproc(arg);
}

ELFSYSV_MSABI int32_t elfsysv_tramp_exfilter(void *exception_pointers)
{
	return cb_slot_exfilter(exception_pointers);
}

/* ---- registration ------------------------------------------------------ */

elfsysv_ms_comparator elfsysv_cb_set_comparator(elfsysv_sysv_comparator fn)
{
	cb_slot_comparator = fn;
	return elfsysv_tramp_comparator;
}

elfsysv_ms_threadproc elfsysv_cb_set_threadproc(elfsysv_sysv_threadproc fn)
{
	cb_slot_threadproc = fn;
	return elfsysv_tramp_threadproc;
}

elfsysv_ms_exfilter elfsysv_cb_set_exfilter(elfsysv_sysv_exfilter fn)
{
	cb_slot_exfilter = fn;
	return elfsysv_tramp_exfilter;
}
