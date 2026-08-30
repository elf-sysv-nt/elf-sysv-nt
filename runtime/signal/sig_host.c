/*
 * sig_host.c -- the hijack, and the translation on both sides of it.
 *
 * This is the only file in the package that knows what a host thread is.
 * Cygwin delivers a signal by suspending the target, reading its register
 * file, pointing it somewhere else and resuming; spike 3 built exactly that
 * mechanism to measure with, and spike 7 put a frame back into it. What is
 * here is the same three calls with the frame this package builds, so the
 * delivery an ELF process sees is a Linux one and the mechanism underneath it
 * is still Cygwin's.
 *
 * The translation is deliberately narrow. A host CONTEXT is a large structure
 * with debug registers, segment state and a floating-point image, and a
 * delivery has business with the general registers, the flags and the
 * floating-point image alone. Everything else is read, left alone, and written
 * back untouched: SetThreadContext takes the same buffer GetThreadContext
 * filled, with the fields a delivery changes changed and nothing else.
 */

#include <windows.h>
#include <string.h>

#include "sigpriv.h"
#include "sig_host.h"

static void ctx_from_host(const CONTEXT *c, elfsysv_sigctx_t *s)
{
	memset(s, 0, sizeof(*s));
	s->rax = c->Rax;
	s->rbx = c->Rbx;
	s->rcx = c->Rcx;
	s->rdx = c->Rdx;
	s->rsi = c->Rsi;
	s->rdi = c->Rdi;
	s->rbp = c->Rbp;
	s->rsp = c->Rsp;
	s->r8 = c->R8;
	s->r9 = c->R9;
	s->r10 = c->R10;
	s->r11 = c->R11;
	s->r12 = c->R12;
	s->r13 = c->R13;
	s->r14 = c->R14;
	s->r15 = c->R15;
	s->rip = c->Rip;
	s->rflags = c->EFlags;
	s->cs = c->SegCs;
	s->ss = c->SegSs;
	s->fs = c->SegFs;
	s->gs = c->SegGs;
	/* The host's XMM_SAVE_AREA32 is an fxsave image in the layout the psABI
	 * frame wants, so it is copied rather than re-derived. */
	s->fxsave = &c->FltSave;
}

/* There is no translation back. The delivery does not rewrite the register
 * file from outside: it redirects the thread to the trampoline and lets the
 * thread do it, so the only two fields that go back to the host are the
 * instruction pointer and the register carrying the pending record. Everything
 * else the target restores for itself from the copy taken here. */

int elfsysv_sig_hijack(void *hthread, elfsysv_sigstate_t *st, int signo,
		       const elfsysv_siginfo_t *info,
		       elfsysv_sig_pending_t *p)
{
	CONTEXT c __attribute__((aligned(16)));

	memset(&c, 0, sizeof(c));
	c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS |
			 CONTEXT_FLOATING_POINT;

	if (SuspendThread((HANDLE)hthread) == (DWORD)-1)
		return -1;

	if (!GetThreadContext((HANDLE)hthread, &c)) {
		ResumeThread((HANDLE)hthread);
		return -1;
	}

	memset(p, 0, sizeof(*p));
	ctx_from_host(&c, &p->ctx);
	memcpy(p->fxsave, &c.FltSave, sizeof(p->fxsave));
	p->ctx.fxsave = p->fxsave;
	p->st = st;
	p->signo = signo;
	if (info) {
		p->info = *info;
		p->has_info = 1;
	}
	p->disposition = -1;

	/* The target builds its own frame. All that changes here is where it
	 * resumes and which register carries the record; everything else in
	 * the file is preserved in the record and restored from it. */
	c.Rip = (DWORD64)(uintptr_t)elfsysv_sig_enter;
	c.R11 = (DWORD64)(uintptr_t)p;

	if (!SetThreadContext((HANDLE)hthread, &c)) {
		ResumeThread((HANDLE)hthread);
		return -1;
	}

	ResumeThread((HANDLE)hthread);
	return 0;
}

uintptr_t elfsysv_sig_thread_sp(void *hthread)
{
	CONTEXT c __attribute__((aligned(16)));

	memset(&c, 0, sizeof(c));
	c.ContextFlags = CONTEXT_CONTROL;
	if (SuspendThread((HANDLE)hthread) == (DWORD)-1)
		return 0;
	if (!GetThreadContext((HANDLE)hthread, &c)) {
		ResumeThread((HANDLE)hthread);
		return 0;
	}
	ResumeThread((HANDLE)hthread);
	return (uintptr_t)c.Rsp;
}
