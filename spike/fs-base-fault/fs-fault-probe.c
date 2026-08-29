/*
 * fs-fault-probe -- what does an access through a zeroed %fs base do, and can
 * a vectored handler resume from it?
 *
 * Spike 1 measured the base and found it zero after anything that deschedules
 * the thread. It never measured the next instruction. That gap decides how
 * good a load-time TLS rewriter has to be: if a missed site faults and a
 * handler can emulate it through DR-0003's carrier and resume, the rewriter is
 * an optimization over a sound fallback and may be a heuristic. If a missed
 * site reads something instead, the rewriter has to be exhaustive, and nothing
 * on x86-64 makes byte scanning exhaustive.
 *
 * So the axis here is the instruction and the handler, not the scheduler.
 * Spike 1 owns the question of what clears the base and this reuses its events
 * only to confirm that the answer does not depend on which one did it.
 *
 * Built and driven by measure-fs-base-fault.sh. See README.md for the method
 * and the verdict rule, both written before the run.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cpuid.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROBE_VERSION "fs-fault-probe 1.0"

#ifndef PF_RDWRFSGSBASE_AVAILABLE
#define PF_RDWRFSGSBASE_AVAILABLE 22
#endif

/*
 * Negative controls. Three claims this probe makes cannot be shown by its own
 * output: from outside, a handler that repaired every access and a measurement
 * that never faulted look alike, so do an emulation reading the right place and
 * one whose place is never checked, and so do a decoder that got the length
 * right and one whose length nothing depends on.
 *
 *   SPIKE_FAULT_NO_EMULATE  the handler skips the instruction without supplying
 *                           a value, so every load case must come back holding
 *                           the poison it went in with.
 *   SPIKE_FAULT_BAD_OFFSET  the emulation reads and writes eight bytes off, so
 *                           every value must be wrong. This is the control on
 *                           the finding the fallback rests on: that the offset
 *                           comes from the faulting address rather than from
 *                           anything the handler assumed.
 *   SPIKE_FAULT_BAD_LENGTH  the decoder reports one byte short. This one has no
 *                           clean failure to report, because resuming into the
 *                           middle of an instruction is not a wrong answer, it
 *                           is a dead process -- which is the evidence that the
 *                           decoded length is load-bearing rather than
 *                           decoration.
 *
 * t/run-tests.sh builds all three and watches them fail before it believes the
 * pass.
 */

/* TEB offsets, the two DR-0003's carrier C3 needs. Confirmed by
 * spike/gs-thread-pointer/ against this running kernel. */
#define TEB_STACKBASE	0x08
#define C3_PAD		0x1000

/* The glibc-shaped block the emulation reads and writes. tcbhead_t sits at TP
 * and above; the static TLS block sits below at negative offsets. */
#define TCB_SELF_OFF	0x00	/* self-pointer, reads back as TP           */
#define TCB_GUARD_OFF	0x28	/* stack-guard word                         */
#define TCB_SPARE_OFF	0x40	/* a positive offset with nothing else on it */

#define BLOCK_BELOW	0x1000	/* bytes of static TLS below TP             */
#define BLOCK_ABOVE	0x1000	/* bytes of tcbhead_t and beyond above it   */

/* Byte encodings rather than mnemonics. The 2019 root's assembler knows these
 * only with -mfsgsbase, and a spike that stops compiling on the next toolchain
 * it meets has failed at its one job. */
static inline uint64_t rdfsbase(void)
{
	uint64_t v;
	__asm__ __volatile__(".byte 0xf3,0x48,0x0f,0xae,0xc0" : "=a"(v));
	return v;
}

static inline void wrfsbase(uint64_t v)
{
	__asm__ __volatile__(".byte 0xf3,0x48,0x0f,0xae,0xd0" : : "a"(v));
}

static inline uint64_t gs_read(unsigned off)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:(%1), %0" : "=r"(v) : "r"((uint64_t)off));
	return v;
}

static inline uint64_t rdtsc(void)
{
	unsigned lo, hi;
	__asm__ __volatile__("lfence; rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}

/* DR-0003's carrier, in the stand-in shape spike 6 measured: a word a fixed
 * distance below this thread's stack base, reached through gs:[0x08]. Every
 * thread that faults has to have one, so establishing it is the first thing
 * any thread here does. */
static void c3_establish(uint64_t tp)
{
	*(uint64_t *)(gs_read(TEB_STACKBASE) - C3_PAD) = tp;
}

static uint64_t c3_fetch(void)
{
	return *(uint64_t *)(gs_read(TEB_STACKBASE) - C3_PAD);
}

static int debug;

static void trace(const char *fmt, ...)
{
	va_list ap;

	if (!debug)
		return;
	fputs("fs-fault-probe: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void *xmap(size_t n)
{
	void *p = mmap(NULL, n, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (p == MAP_FAILED) {
		fprintf(stderr, "fs-fault-probe: mmap: %s\n", strerror(errno));
		exit(1);
	}
	return p;
}

/*
 * A TLS block for one thread, laid out the way a glibc image lays one out, and
 * pre-loaded with values distinct enough that a wrong offset cannot be read as
 * a right one. Returns TP.
 */
static uint64_t make_block(void)
{
	unsigned char *p = xmap(BLOCK_BELOW + BLOCK_ABOVE);
	uint64_t tp = (uint64_t)(p + BLOCK_BELOW);

	*(uint64_t *)(tp + TCB_SELF_OFF) = tp;
	*(uint64_t *)(tp + TCB_GUARD_OFF) = 0x5747554152440000ULL ^ tp;
	*(uint64_t *)(tp + TCB_SPARE_OFF) = 0x5350415245000000ULL ^ tp;
	*(uint64_t *)(tp - 8) = 0x4e454731ULL ^ tp;
	*(uint64_t *)(tp - 16) = 0x00000000cafe1234ULL;
	*(uint64_t *)(tp - 24) = 0;
	*(uint64_t *)(tp - 32) = 0;
	*(uint64_t *)(tp - 40) = 0;
	*(uint64_t *)(tp - 64) = 0x4e454734ULL ^ tp;
	return tp;
}

/* ---- the instruction decoder ------------------------------------------- */

/*
 * Only what a compiler emits for TLS, and nothing further. The forms that move
 * data are decoded and emulated; the read-modify-write forms are refused by
 * name rather than guessed at, because emulating them means emulating EFLAGS
 * and a handler that gets the carry flag wrong is worse than one that admits
 * it cannot help. What that refusal costs is a case below rather than a
 * footnote.
 */
struct insn {
	int valid;
	const char *why;	/* set when !valid                          */
	int len;
	int store;
	int mem_size;		/* bytes touched in memory                  */
	int dst_size;		/* destination register width, loads only   */
	int sign_extend;
	int has_reg;		/* a store-immediate has no register source */
	int reg;
	int64_t imm;
	uint64_t ea;		/* offset from the segment base             */
	int seg_fs;
};

static DWORD64 *reg_slot(CONTEXT *c, int n)
{
	switch (n) {
	case 0: return &c->Rax;
	case 1: return &c->Rcx;
	case 2: return &c->Rdx;
	case 3: return &c->Rbx;
	case 4: return &c->Rsp;
	case 5: return &c->Rbp;
	case 6: return &c->Rsi;
	case 7: return &c->Rdi;
	case 8: return &c->R8;
	case 9: return &c->R9;
	case 10: return &c->R10;
	case 11: return &c->R11;
	case 12: return &c->R12;
	case 13: return &c->R13;
	case 14: return &c->R14;
	case 15: return &c->R15;
	}
	return NULL;
}

static int32_t load32(const unsigned char *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return (int32_t)v;
}

static void decode(const unsigned char *p, CONTEXT *c, struct insn *o)
{
	int i = 0, rex = 0, opsize = 4, mod, rm, reg_field;
	int imm_size = 0, riprel = 0, have_base = 0;
	int64_t disp = 0;
	uint64_t base_val = 0, index_val = 0;
	unsigned char op;

	memset(o, 0, sizeof *o);
	o->has_reg = 1;

	for (;;) {
		unsigned char b = p[i];

		if (b == 0x64) { o->seg_fs = 1; i++; continue; }
		if (b == 0x65 || b == 0x2e || b == 0x3e || b == 0x26 ||
		    b == 0x36 || b == 0xf0 || b == 0xf2 || b == 0xf3) { i++; continue; }
		if (b == 0x66) { opsize = 2; i++; continue; }
		if (b == 0x67) { o->why = "a 32-bit address size"; return; }
		break;
	}
	if (p[i] >= 0x40 && p[i] <= 0x4f)
		rex = p[i++];
	if (rex & 8)
		opsize = 8;

	op = p[i++];
	if (op == 0x0f) {
		unsigned char op2 = p[i++];

		switch (op2) {
		case 0xb6: o->mem_size = 1; o->dst_size = opsize; break;
		case 0xb7: o->mem_size = 2; o->dst_size = opsize; break;
		case 0xbe: o->mem_size = 1; o->dst_size = opsize; o->sign_extend = 1; break;
		case 0xbf: o->mem_size = 2; o->dst_size = opsize; o->sign_extend = 1; break;
		default:
			o->why = "a two-byte opcode this handler does not move data for";
			return;
		}
	} else {
		switch (op) {
		case 0x88: o->store = 1; o->mem_size = 1; break;
		case 0x89: o->store = 1; o->mem_size = opsize; break;
		case 0x8a: o->mem_size = 1; o->dst_size = 1; break;
		case 0x8b: o->mem_size = opsize; o->dst_size = opsize; break;
		case 0xc6: o->store = 1; o->has_reg = 0; o->mem_size = 1; imm_size = 1; break;
		case 0xc7: o->store = 1; o->has_reg = 0; o->mem_size = opsize;
			   imm_size = opsize == 8 ? 4 : opsize; break;
		default:
			o->why = "not a data move; emulating it means emulating EFLAGS";
			return;
		}
	}

	mod = p[i] >> 6;
	reg_field = (p[i] >> 3) & 7;
	rm = p[i] & 7;
	i++;

	if (mod == 3) {
		o->why = "a register operand, so no memory access and no fault";
		return;
	}
	if (rm == 4) {
		unsigned char sib = p[i++];
		int idx = ((sib >> 3) & 7) | ((rex & 2) ? 8 : 0);
		int bas = (sib & 7) | ((rex & 1) ? 8 : 0);

		if (!(((sib >> 3) & 7) == 4 && !(rex & 2)))
			index_val = *reg_slot(c, idx) << (sib >> 6);
		if ((sib & 7) == 5 && mod == 0) {
			disp = load32(p + i);
			i += 4;
		} else {
			base_val = *reg_slot(c, bas);
			have_base = 1;
		}
	} else if (rm == 5 && mod == 0) {
		riprel = 1;
		disp = load32(p + i);
		i += 4;
	} else {
		base_val = *reg_slot(c, rm | ((rex & 1) ? 8 : 0));
		have_base = 1;
	}
	(void)have_base;

	if (mod == 1) {
		disp = (int8_t)p[i];
		i += 1;
	} else if (mod == 2) {
		disp = load32(p + i);
		i += 4;
	}

	if (imm_size == 1)
		o->imm = (int8_t)p[i];
	else if (imm_size == 2)
		o->imm = (int16_t)((p[i + 1] << 8) | p[i]);
	else if (imm_size == 4)
		o->imm = load32(p + i);
	i += imm_size;

	o->len = i;
	o->reg = reg_field | ((rex & 4) ? 8 : 0);

	/* A byte operand without REX names AH..BH for reg 4..7, which is not a
	 * slot in CONTEXT and not something a TLS access emits. Refuse it by
	 * name rather than write into the wrong half of a register. */
	if (o->mem_size == 1 && !rex && o->has_reg && o->reg >= 4 &&
	    (o->store || o->dst_size == 1)) {
		o->why = "a legacy high-byte register";
		return;
	}

	if (riprel)
		o->ea = c->Rip + (uint64_t)o->len + (uint64_t)disp;
	else
		o->ea = base_val + index_val + (uint64_t)disp;

	o->valid = 1;
}

/* ---- the handler -------------------------------------------------------- */

enum veh_mode {
	VEH_OFF,		/* let Cygwin have it, as a signal          */
	VEH_OBSERVE,		/* record, then skip the instruction        */
	VEH_EMULATE,		/* record, emulate through C3, resume       */
	VEH_COUNT		/* emulate, count, record nothing per fault */
};

static volatile LONG veh_mode = VEH_OFF;

/* What the last observed fault was. Written only in the single-threaded
 * sections, so no lock.
 *
 * The flag is separate and volatile on purpose. As a plain member it is a
 * static this translation unit can see every write to, the handler is called
 * from Windows rather than from any call site the compiler can find, and at
 * -O1 the caller reads a stale zero back after a fault it certainly took --
 * which reads exactly like an access that never faulted, and cost this spike
 * a wrong verdict on its first run. */
static volatile int last_seen;

static struct {
	DWORD code;
	uint64_t info0;		/* 0 read, 1 write, 8 execute               */
	uint64_t address;
	uint64_t rip;
	uint64_t next_rip;	/* where the handler resumed                */
	struct insn insn;
} last;

static volatile LONG faults_handled;
static volatile LONG faults_refused;
static volatile LONG faults_seen;

static uint64_t emulate(CONTEXT *c, struct insn *o)
{
	uint64_t tp = c3_fetch();
	unsigned char *m;
	uint64_t v = 0;

	if (!tp)
		return 0;
#ifdef SPIKE_FAULT_BAD_OFFSET
	m = (unsigned char *)(uintptr_t)(tp + o->ea + 8);
#else
	m = (unsigned char *)(uintptr_t)(tp + o->ea);
#endif

	if (o->store) {
		v = o->has_reg ? *reg_slot(c, o->reg) : (uint64_t)o->imm;
		memcpy(m, &v, (size_t)o->mem_size);
		return v;
	}

	memcpy(&v, m, (size_t)o->mem_size);
	if (o->sign_extend) {
		if (o->mem_size == 1)
			v = (uint64_t)(int64_t)(int8_t)v;
		else if (o->mem_size == 2)
			v = (uint64_t)(int64_t)(int16_t)v;
	}
#ifdef SPIKE_FAULT_NO_EMULATE
	return v;		/* skip the instruction, supply nothing */
#else
	{
		DWORD64 *d = reg_slot(c, o->reg);

		if (o->dst_size == 8)
			*d = v;
		else if (o->dst_size == 4)
			*d = (uint32_t)v;	/* zero-extends, as the hardware does */
		else if (o->dst_size == 2)
			*d = (*d & ~(DWORD64)0xffff) | (v & 0xffff);
		else
			*d = (*d & ~(DWORD64)0xff) | (v & 0xff);
	}
	return v;
#endif
}

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
	EXCEPTION_RECORD *er = ep->ExceptionRecord;
	CONTEXT *c = ep->ContextRecord;
	LONG mode = veh_mode;
	struct insn in;

	if (mode == VEH_OFF)
		return EXCEPTION_CONTINUE_SEARCH;
	if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
		return EXCEPTION_CONTINUE_SEARCH;

	InterlockedIncrement(&faults_seen);
	decode((const unsigned char *)(uintptr_t)c->Rip, c, &in);
#ifdef SPIKE_FAULT_BAD_LENGTH
	if (in.valid)
		in.len -= 1;
#endif

	if (mode != VEH_COUNT) {
		last_seen = 1;
		last.code = er->ExceptionCode;
		last.info0 = er->NumberParameters >= 2 ?
			(uint64_t)er->ExceptionInformation[0] : ~(uint64_t)0;
		last.address = er->NumberParameters >= 2 ?
			(uint64_t)er->ExceptionInformation[1] : ~(uint64_t)0;
		last.rip = c->Rip;
		last.insn = in;
	}

	if (!in.valid || !in.seg_fs) {
		InterlockedIncrement(&faults_refused);
		return EXCEPTION_CONTINUE_SEARCH;
	}

	if (mode != VEH_OBSERVE)
		emulate(c, &in);
	c->Rip += (DWORD64)in.len;
	if (mode != VEH_COUNT)
		last.next_rip = c->Rip;
	InterlockedIncrement(&faults_handled);
	return EXCEPTION_CONTINUE_EXECUTION;
}

static PVOID veh_token;

static int install_veh(void)
{
	if (veh_token)
		return 0;
	veh_token = AddVectoredExceptionHandler(1, veh);
	return veh_token ? 0 : -1;
}

/* ---- the instruction forms --------------------------------------------- */

/*
 * One function per psABI-shaped access. Each leaves the destination holding
 * POISON on the way in, so a value that never arrived is distinguishable from
 * a value that arrived wrong, and each computes the address of the instruction
 * that follows the access. That second number is what checks the decoder's
 * length: the handler resumes at RIP plus its own decoded length, and if that
 * is not the landing pad the emulation was luck.
 */
#define POISON 0xdeadbeefdeadbeefULL

static uint64_t landing;

#define AFTER "leaq 1f(%%rip), %1\n\t"
#define LAND  "\n1:\n"

static uint64_t f_self(void)		/* movq %fs:0x0, r -- the self-pointer */
{
	uint64_t v = POISON, a;

	__asm__ __volatile__(AFTER "movq %%fs:0x0, %0" LAND
			     : "=r"(v), "=&r"(a) : "0"(v) : "memory");
	landing = a;
	return v;
}

static uint64_t f_neg8(void)		/* movq %fs:-0x8, r -- direct local exec */
{
	uint64_t v = POISON, a;

	__asm__ __volatile__(AFTER "movq %%fs:-0x8, %0" LAND
			     : "=r"(v), "=&r"(a) : "0"(v) : "memory");
	landing = a;
	return v;
}

static uint64_t f_pos(void)		/* movq %fs:0x40, r -- above the TCB head */
{
	uint64_t v = POISON, a;

	__asm__ __volatile__(AFTER "movq %%fs:0x40, %0" LAND
			     : "=r"(v), "=&r"(a) : "0"(v) : "memory");
	landing = a;
	return v;
}

static uint64_t f_indirect(uint64_t off)	/* movq %fs:(r), r -- the IE form */
{
	uint64_t v = POISON, a;

	__asm__ __volatile__(AFTER "movq %%fs:(%2), %0" LAND
			     : "=r"(v), "=&r"(a) : "r"(off), "0"(v) : "memory");
	landing = a;
	return v;
}

static uint64_t f_disp8(uint64_t zero)	/* movq %fs:0x40(r), r -- a base and a disp */
{
	uint64_t v = POISON, a;

	__asm__ __volatile__(AFTER "movq %%fs:0x40(%2), %0" LAND
			     : "=r"(v), "=&r"(a) : "r"(zero), "0"(v) : "memory");
	landing = a;
	return v;
}

static uint64_t f_load32(void)		/* movl %fs:0x0, r32 */
{
	uint64_t v = POISON, a;

	__asm__ __volatile__(AFTER "movl %%fs:0x0, %k0" LAND
			     : "=r"(v), "=&r"(a) : "0"(v) : "memory");
	landing = a;
	return v;
}

static uint64_t f_movzx(void)		/* movzwl %fs:-0x10, r32 */
{
	uint64_t v = POISON, a;

	__asm__ __volatile__(AFTER "movzwl %%fs:-0x10, %k0" LAND
			     : "=r"(v), "=&r"(a) : "0"(v) : "memory");
	landing = a;
	return v;
}

static void f_store(uint64_t val)	/* movq r, %fs:-0x18 */
{
	uint64_t a;

	__asm__ __volatile__("leaq 1f(%%rip), %0\n\t"
			     "movq %1, %%fs:-0x18" LAND
			     : "=&r"(a) : "r"(val) : "memory");
	landing = a;
}

static void f_store_imm(void)		/* movq $imm32, %fs:-0x20 */
{
	uint64_t a;

	__asm__ __volatile__("leaq 1f(%%rip), %0\n\t"
			     "movq $0x5a5a5a5a, %%fs:-0x20" LAND
			     : "=&r"(a) : : "memory");
	landing = a;
}

static void f_rmw(void)			/* addq $1, %fs:-0x28 -- the refusal */
{
	uint64_t a;

	__asm__ __volatile__("leaq 1f(%%rip), %0\n\t"
			     "addq $1, %%fs:-0x28" LAND
			     : "=&r"(a) : : "memory", "cc");
	landing = a;
}

/* ---- outcome bookkeeping ------------------------------------------------ */

#define MAX_CASES 40

struct outcome {
	const char *name;
	const char *note;	/* set when the case could not run at all */
	int ran;
	int failed;
	char detail[120];
};

static struct outcome cases[MAX_CASES];
static int ncases;

static struct outcome *open_case(const char *name)
{
	struct outcome *c = &cases[ncases++];

	c->name = name;
	c->ran = 1;
	trace("case %s", name);
	return c;
}

static void detail(struct outcome *c, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(c->detail, sizeof c->detail, fmt, ap);
	va_end(ap);
}

/* ---- what a zeroed base does, by however it got that way ---------------- */

#define MAX_EVENTS 16

struct event {
	const char *name;
	const char *note;
	int ran;
	int base_survived;
	int faulted;
	DWORD code;
	uint64_t info0;
	uint64_t address;
	uint64_t value;
};

static struct event events[MAX_EVENTS];
static int nevents;

/* The access this section makes, with the handler observing and skipping so
 * that a fault is survivable without a longjmp out of a Windows dispatch. */
static void access_and_record(struct event *e)
{
	uint64_t v;

	last_seen = 0;
	veh_mode = VEH_OBSERVE;
	v = f_self();
	veh_mode = VEH_OFF;

	if (last_seen) {
		e->faulted = 1;
		e->code = last.code;
		e->info0 = last.info0;
		e->address = last.address;
	} else {
		e->value = v;
	}
}

static uint64_t distinct_base(void)
{
	uint64_t b = (uint64_t)xmap(4096);

	*(uint64_t *)b = b;	/* a surviving base reads back as itself */
	return b;
}

static void measure_event(const char *name, void (*provoke)(void))
{
	struct event *e = &events[nevents++];
	uint64_t base = distinct_base();

	e->name = name;
	e->ran = 1;
	wrfsbase(base);
	provoke();
	if (rdfsbase() == base) {
		e->base_survived = 1;
		return;
	}
	access_and_record(e);
}

static void p_explicit(void) { wrfsbase(0); }

static void p_syscall(void)
{
	FILETIME a, b, c, d;

	GetProcessTimes(GetCurrentProcess(), &a, &b, &c, &d);
}

static void p_yield(void) { Sleep(1); }

static HANDLE pp_call, pp_back;

static DWORD WINAPI pp_peer(LPVOID arg)
{
	(void)arg;
	WaitForSingleObject(pp_call, INFINITE);
	SetEvent(pp_back);
	return 0;
}

static void p_blocking_wait(void)
{
	HANDLE t;

	pp_call = CreateEvent(NULL, FALSE, FALSE, NULL);
	pp_back = CreateEvent(NULL, FALSE, FALSE, NULL);
	t = CreateThread(NULL, 0, pp_peer, NULL, 0, NULL);
	if (!t)
		return;
	SetEvent(pp_call);
	WaitForSingleObject(pp_back, INFINITE);
	WaitForSingleObject(t, 5000);
	CloseHandle(t);
	CloseHandle(pp_call);
	CloseHandle(pp_back);
}

static int ncpu_global = 1;

static void p_migration(void)
{
	DWORD_PTR original = SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1);

	SetThreadAffinityMask(GetCurrentThread(),
			      (DWORD_PTR)1 << (ncpu_global > 1 ? 1 : 0));
	SwitchToThread();
	if (original)
		SetThreadAffinityMask(GetCurrentThread(), original);
}

static VOID CALLBACK apc_body(ULONG_PTR arg) { (void)arg; }

static void p_apc(void)
{
	QueueUserAPC(apc_body, GetCurrentThread(), 0);
	SleepEx(0, TRUE);
}

static void quiet_handler(int signo) { (void)signo; }

static void p_signal_sync(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = quiet_handler;
	sigaction(SIGUSR1, &sa, NULL);
	raise(SIGUSR1);
	sa.sa_handler = SIG_DFL;
	sigaction(SIGUSR1, &sa, NULL);
}

static sigjmp_buf segv_return;

static void segv_handler(int signo)
{
	(void)signo;
	siglongjmp(segv_return, 1);
}

/* A real hardware fault turned into a signal by Cygwin's own path, which is a
 * longer trip through the kernel than anything above. The handler here is off,
 * so the vectored handler declines and Cygwin gets it, which is also a check
 * that declining works. */
static void p_signal_fault(void)
{
	struct sigaction sa, old;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = segv_handler;
	sa.sa_flags = SA_NODEFER;
	sigaction(SIGSEGV, &sa, &old);
	if (sigsetjmp(segv_return, 1) == 0)
		*(volatile int *)0x10 = 1;
	sigaction(SIGSEGV, &old, NULL);
}

static HANDLE hijack_target;
static volatile LONG hijack_go, hijack_done;

static DWORD WINAPI hijacker(LPVOID arg)
{
	CONTEXT ctx __attribute__((aligned(16)));

	(void)arg;
	while (!hijack_go)
		Sleep(0);
	Sleep(1);
	if (SuspendThread(hijack_target) != (DWORD)-1) {
		memset(&ctx, 0, sizeof ctx);
		ctx.ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS;
		if (GetThreadContext(hijack_target, &ctx))
			SetThreadContext(hijack_target, &ctx);
		ResumeThread(hijack_target);
	}
	InterlockedExchange((LONG *)&hijack_done, 1);
	return 0;
}

/* Suspend, read the context back, write the same context returned, resume:
 * exactly how Cygwin delivers a signal. The thread has to be doing something
 * while it is hijacked, and what it does here is wait, so this case carries
 * the blocking wait inside it and is reported knowing that. */
static void p_hijack(void)
{
	HANDLE t;

	if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
			     GetCurrentProcess(), &hijack_target,
			     0, FALSE, DUPLICATE_SAME_ACCESS))
		return;
	hijack_go = hijack_done = 0;
	t = CreateThread(NULL, 0, hijacker, NULL, 0, NULL);
	if (!t) {
		CloseHandle(hijack_target);
		return;
	}
	InterlockedExchange((LONG *)&hijack_go, 1);
	while (!hijack_done)
		Sleep(0);
	WaitForSingleObject(t, 5000);
	CloseHandle(t);
	CloseHandle(hijack_target);
}

static volatile LONG burn_stop;
static unsigned long long burn_sink;

static void *burner(void *arg)
{
	(void)arg;
	while (!burn_stop)
		burn_sink++;
	return NULL;
}

static pthread_t *burners;
static int nburners;

static void burners_start(int n)
{
	int i;

	burn_stop = 0;
	burners = calloc((size_t)n, sizeof *burners);
	nburners = 0;
	if (!burners)
		return;
	for (i = 0; i < n; i++)
		if (pthread_create(&burners[i], NULL, burner, NULL) == 0)
			nburners++;
}

static void burners_stop(void)
{
	int i;

	InterlockedExchange((LONG *)&burn_stop, 1);
	for (i = 0; i < nburners; i++)
		pthread_join(burners[i], NULL);
	free(burners);
	burners = NULL;
	nburners = 0;
}

/* The case with no system call in it. The base is lost to the scheduler alone,
 * which is the one loss a runtime could not paper over at a call site, and so
 * the one that decides whether the handler has to exist. */
static void p_preemption(void)
{
	uint64_t base = rdfsbase();
	time_t end = time(NULL) + 5;
	int i;

	burners_start(ncpu_global);
	/* time() is a call, so it is consulted once per two hundred thousand
	 * reads and the inner loop makes none. */
	for (;;) {
		for (i = 0; i < 200000; i++)
			if (rdfsbase() != base)
				goto done;
		if (time(NULL) >= end)
			break;
	}
done:
	burners_stop();
}

static struct event *fresh_event;

static DWORD WINAPI fresh_body(LPVOID arg)
{
	(void)arg;
	if (rdfsbase() != 0) {
		fresh_event->note = "a fresh thread did not start at zero";
		return 0;
	}
	access_and_record(fresh_event);
	return 0;
}

static void measure_thread_start(void)
{
	struct event *e = &events[nevents++];
	HANDLE t;

	e->name = "thread start";
	e->ran = 1;
	fresh_event = e;
	wrfsbase(distinct_base());
	t = CreateThread(NULL, 0, fresh_body, NULL, 0, NULL);
	if (!t) {
		e->note = "could not create the thread";
		return;
	}
	WaitForSingleObject(t, 10000);
	CloseHandle(t);
}

static void measure_fork(void)
{
	struct event *e = &events[nevents++];
	struct event child;
	int fd[2];
	pid_t pid;

	e->name = "fork";
	e->ran = 1;
	if (pipe(fd) != 0) {
		e->note = "pipe refused";
		return;
	}
	wrfsbase(distinct_base());
	pid = fork();
	if (pid == 0) {
		memset(&child, 0, sizeof child);
		/* The child is a new Windows process, so the registration does
		 * not come across the fork and has to be made again. */
		veh_token = NULL;
		if (install_veh() != 0)
			_exit(2);
		if (rdfsbase() == 0)
			access_and_record(&child);
		else
			child.base_survived = 1;
		if (write(fd[1], &child, sizeof child) != (ssize_t)sizeof child)
			_exit(3);
		_exit(0);
	}
	close(fd[1]);
	if (pid < 0) {
		e->note = "fork refused";
		close(fd[0]);
		return;
	}
	if (read(fd[0], &child, sizeof child) != (ssize_t)sizeof child) {
		e->note = "the child said nothing";
	} else {
		e->base_survived = child.base_survived;
		e->faulted = child.faulted;
		e->code = child.code;
		e->info0 = child.info0;
		e->address = child.address;
		e->value = child.value;
	}
	close(fd[0]);
	waitpid(pid, NULL, 0);
}

/* ---- the handler, measured ---------------------------------------------- */

static uint64_t tp_main;
static int any_read;		/* an access through a zeroed base returned */

static inline uint64_t fs_self_raw(void)
{
	uint64_t v;

	__asm__ __volatile__("movq %%fs:0x0, %0" : "=r"(v));
	return v;
}

static void form_case(const char *name, uint64_t got, uint64_t want, int decodable)
{
	struct outcome *c = open_case(name);

	if (!last_seen) {
		c->failed = 1;
		any_read = 1;
		detail(c, "no fault; the access returned 0x%llx",
		       (unsigned long long)got);
		return;
	}
	if (!last.insn.valid) {
		if (decodable) {
			c->failed = 1;
			detail(c, "the decoder refused it: %s", last.insn.why);
		} else {
			detail(c, "refused by name: %s", last.insn.why);
		}
		return;
	}
	if (!decodable) {
		c->failed = 1;
		detail(c, "decoded a form it was written to refuse");
		return;
	}
	if (last.next_rip != landing) {
		c->failed = 1;
		detail(c, "resumed at 0x%llx; the next instruction is at 0x%llx",
		       (unsigned long long)last.next_rip,
		       (unsigned long long)landing);
		return;
	}
	if (got != want) {
		c->failed = 1;
		detail(c, "came back 0x%llx, wanted 0x%llx",
		       (unsigned long long)got, (unsigned long long)want);
		return;
	}
	detail(c, "%d bytes, offset 0x%llx, value 0x%llx", last.insn.len,
	       (unsigned long long)last.address, (unsigned long long)want);
}

/* The three questions the whole spike turns on, asked before anything is
 * emulated: does it fault, does the handler get it, and is the faulting
 * address the TLS offset. The third is not decoration. With the base at zero
 * the effective address is the offset, so a handler is handed the offset by
 * Windows and never has to compute one. */
static void case_sees(void)
{
	struct outcome *c;
	uint64_t v;

	wrfsbase(0);
	last_seen = 0;
	veh_mode = VEH_OBSERVE;
	v = f_self();
	veh_mode = VEH_OFF;

	c = open_case("the access faults");
	if (!last_seen) {
		c->failed = 1;
		any_read = 1;
		detail(c, "it read 0x%llx instead", (unsigned long long)v);
		return;
	}
	if (last.code != EXCEPTION_ACCESS_VIOLATION) {
		c->failed = 1;
		detail(c, "the exception was 0x%08lx", (unsigned long)last.code);
		return;
	}
	detail(c, "access violation, %s, at 0x%llx",
	       last.info0 == 0 ? "read" : last.info0 == 1 ? "write" : "execute",
	       (unsigned long long)last.address);

	c = open_case("the address is the offset");
	{
		uint64_t a0, a40, aneg, astore;

		last_seen = 0; veh_mode = VEH_OBSERVE; (void)f_self();
		veh_mode = VEH_OFF; a0 = last_seen ? last.address : ~(uint64_t)0;

		last_seen = 0; veh_mode = VEH_OBSERVE; (void)f_pos();
		veh_mode = VEH_OFF; a40 = last_seen ? last.address : ~(uint64_t)0;

		last_seen = 0; veh_mode = VEH_OBSERVE; (void)f_neg8();
		veh_mode = VEH_OFF; aneg = last_seen ? last.address : ~(uint64_t)0;

		last_seen = 0; veh_mode = VEH_OBSERVE; f_store(0);
		veh_mode = VEH_OFF;
		astore = last_seen ? last.address : ~(uint64_t)0;

		if (a0 != 0 || a40 != 0x40 || aneg != (uint64_t)-8 ||
		    astore != (uint64_t)-0x18) {
			c->failed = 1;
			detail(c, "0x0 gave 0x%llx, 0x40 gave 0x%llx, -0x8 gave 0x%llx, "
			       "the store gave 0x%llx",
			       (unsigned long long)a0, (unsigned long long)a40,
			       (unsigned long long)aneg, (unsigned long long)astore);
		} else {
			detail(c, "0x0, 0x40, -0x8 and -0x18 each reported as themselves");
		}
	}

	c = open_case("a store faults as a write");
	if (last.info0 != 1) {
		c->failed = 1;
		detail(c, "the store reported information word %llu",
		       (unsigned long long)last.info0);
	} else {
		detail(c, "reported write, so the handler knows which way to move");
	}
}

/* The census. Every form a compiler emits for TLS on this target, plus the one
 * it also emits that this handler refuses on purpose. */
static void case_forms(void)
{
	uint64_t tp = tp_main, v;
	struct sigaction sa, old;

	wrfsbase(0);
	veh_mode = VEH_EMULATE;

	last_seen = 0; v = f_self();
	form_case("mov %fs:0x0,r64", v, tp, 1);

	last_seen = 0; v = f_neg8();
	form_case("mov %fs:-0x8,r64", v, *(uint64_t *)(tp - 8), 1);

	last_seen = 0; v = f_pos();
	form_case("mov %fs:0x40,r64", v, *(uint64_t *)(tp + TCB_SPARE_OFF), 1);

	last_seen = 0; v = f_indirect((uint64_t)-8);
	form_case("mov %fs:(r),r64", v, *(uint64_t *)(tp - 8), 1);

	last_seen = 0; v = f_disp8(0);
	form_case("mov %fs:0x40(r),r64", v, *(uint64_t *)(tp + TCB_SPARE_OFF), 1);

	last_seen = 0; v = f_load32();
	form_case("mov %fs:0x0,r32", v, (uint32_t)tp, 1);

	last_seen = 0; v = f_movzx();
	form_case("movzwl %fs:-0x10,r32", v,
		  (uint64_t)(*(uint16_t *)(tp - 16)), 1);

	last_seen = 0;
	*(uint64_t *)(tp - 0x18) = 0;
	f_store(0x1234567890abcdefULL);
	form_case("mov r64,%fs:-0x18", *(uint64_t *)(tp - 0x18),
		  0x1234567890abcdefULL, 1);

	last_seen = 0;
	*(uint64_t *)(tp - 0x20) = 0;
	f_store_imm();
	form_case("movq $imm,%fs:-0x20", *(uint64_t *)(tp - 0x20),
		  0x5a5a5a5aULL, 1);

	/* The refusal. The handler declines, Cygwin turns the access violation
	 * into SIGSEGV, and the point of the case is that a form outside the
	 * decoder is reported rather than guessed at. */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = segv_handler;
	sa.sa_flags = SA_NODEFER;
	sigaction(SIGSEGV, &sa, &old);
	last_seen = 0;
	if (sigsetjmp(segv_return, 1) == 0)
		f_rmw();
	sigaction(SIGSEGV, &old, NULL);
	form_case("addq $1,%fs:-0x28", 0, 0, 0);

	veh_mode = VEH_OFF;
}

/* A handled fault must leave everything it did not emulate alone. The
 * destination register is the handler's to write; every other register and the
 * flags belong to the interrupted code, and a rewriter's fallback that
 * clobbers a flag breaks a compare that has not reached its branch yet. */
static void case_registers(void)
{
	struct outcome *c = open_case("neighbouring registers and flags");
	uint64_t want10 = 0x1010101010101010ULL, want11 = 0x1111111111111111ULL;
	uint64_t v = POISON, g10 = 0, g11 = 0;
	unsigned char cf = 0;

	wrfsbase(0);
	last_seen = 0;
	veh_mode = VEH_EMULATE;
	__asm__ __volatile__("movq %4, %%r10\n\t"
			     "movq %5, %%r11\n\t"
			     "stc\n\t"
			     "movq %%fs:0x0, %0\n\t"
			     "setc %3\n\t"
			     "movq %%r10, %1\n\t"
			     "movq %%r11, %2\n\t"
			     : "=r"(v), "=r"(g10), "=r"(g11), "=q"(cf)
			     : "r"(want10), "r"(want11)
			     : "r10", "r11", "cc", "memory");
	veh_mode = VEH_OFF;

	if (!last_seen) {
		c->failed = 1;
		any_read = 1;
		detail(c, "no fault; the access returned 0x%llx",
		       (unsigned long long)v);
	} else if (v != tp_main) {
		c->failed = 1;
		detail(c, "the destination came back 0x%llx", (unsigned long long)v);
	} else if (g10 != want10 || g11 != want11) {
		c->failed = 1;
		detail(c, "r10 came back 0x%llx and r11 0x%llx",
		       (unsigned long long)g10, (unsigned long long)g11);
	} else if (!cf) {
		c->failed = 1;
		detail(c, "the carry flag did not survive the fault");
	} else {
		detail(c, "destination written, r10, r11 and the carry flag intact");
	}
}

/* ---- the case with no call site ----------------------------------------- */

static double qpc_hz;

static double seconds_between(LARGE_INTEGER a, LARGE_INTEGER b)
{
	if (qpc_hz <= 0)
		return 0;
	return (double)(b.QuadPart - a.QuadPart) / qpc_hz;
}

static unsigned long long spin_reads, spin_bad, spin_faults;
static double spin_seconds;

static void case_spin(int seconds)
{
	struct outcome *c = open_case("no call site");
	LARGE_INTEGER t0, t1;
	LONG before, after;
	time_t end;
	int i;

	burners_start(ncpu_global);
	wrfsbase(0);
	before = faults_handled;
	veh_mode = VEH_COUNT;
	QueryPerformanceCounter(&t0);
	end = time(NULL) + seconds;
	for (;;) {
		for (i = 0; i < 2000; i++) {
			if (fs_self_raw() != tp_main)
				spin_bad++;
			spin_reads++;
		}
		if (time(NULL) >= end)
			break;
	}
	QueryPerformanceCounter(&t1);
	veh_mode = VEH_OFF;
	after = faults_handled;
	burners_stop();

	spin_faults = (unsigned long long)(unsigned long)(after - before);
	spin_seconds = seconds_between(t0, t1);

	if (spin_reads && spin_faults < spin_reads) {
		c->failed = 1;
		detail(c, "%llu reads but only %llu faults; something read through",
		       spin_reads, spin_faults);
		any_read = 1;
	} else if (spin_bad) {
		c->failed = 1;
		detail(c, "%llu of %llu reads came back wrong", spin_bad, spin_reads);
	} else {
		detail(c, "%llu reads, every one faulted and every one correct",
		       spin_reads);
	}
}

struct worker {
	pthread_t tid;
	unsigned long long reads;
	unsigned long long bad;
	int started;
};

static volatile LONG worker_stop;

static void *worker_body(void *arg)
{
	struct worker *w = arg;
	uint64_t tp = make_block();
	int i;

	c3_establish(tp);
	while (!worker_stop) {
		for (i = 0; i < 2000; i++) {
			if (fs_self_raw() != tp)
				w->bad++;
			w->reads++;
		}
	}
	return NULL;
}

static unsigned long long thread_reads, thread_bad;
static int thread_started;

/* Reentrancy. One handler, many threads faulting into it at once, each with
 * its own carrier and its own block: a thread that came back with a
 * neighbour's thread pointer would be the whole fallback undone. */
static void case_threads(int nthreads, int seconds)
{
	struct outcome *c = open_case("concurrent faults");
	struct worker *w = calloc((size_t)nthreads, sizeof *w);
	int i;

	if (!w) {
		c->note = "out of memory";
		return;
	}
	worker_stop = 0;
	veh_mode = VEH_COUNT;
	for (i = 0; i < nthreads; i++) {
		if (pthread_create(&w[i].tid, NULL, worker_body, &w[i]) != 0)
			continue;
		w[i].started = 1;
		thread_started++;
	}
	if (!thread_started) {
		veh_mode = VEH_OFF;
		c->note = "no worker thread would start";
		free(w);
		return;
	}
	Sleep((DWORD)seconds * 1000);
	InterlockedExchange((LONG *)&worker_stop, 1);
	for (i = 0; i < nthreads; i++)
		if (w[i].started)
			pthread_join(w[i].tid, NULL);
	veh_mode = VEH_OFF;

	for (i = 0; i < nthreads; i++) {
		thread_reads += w[i].reads;
		thread_bad += w[i].bad;
	}
	free(w);

	if (thread_bad) {
		c->failed = 1;
		detail(c, "%llu of %llu reads came back wrong across %d threads",
		       thread_bad, thread_reads, thread_started);
	} else {
		detail(c, "%llu reads across %d threads, none wrong",
		       thread_reads, thread_started);
	}
}

/* ---- what it costs ------------------------------------------------------ */

static double handled_ns, direct_ns;

static void case_cost(unsigned long rounds)
{
	struct outcome *c = open_case("cost of a handled fault");
	LARGE_INTEGER t0, t1;
	volatile uint64_t sink = 0;
	unsigned long i;

	/* The fallback path: every access faults, every fault is emulated. */
	wrfsbase(0);
	veh_mode = VEH_COUNT;
	QueryPerformanceCounter(&t0);
	for (i = 0; i < rounds; i++)
		sink += fs_self_raw();
	QueryPerformanceCounter(&t1);
	veh_mode = VEH_OFF;
	if (rounds)
		handled_ns = seconds_between(t0, t1) * 1e9 / (double)rounds;

	/* What the same access costs once a rewriter has reached it. */
	QueryPerformanceCounter(&t0);
	for (i = 0; i < rounds; i++)
		sink += c3_fetch();
	QueryPerformanceCounter(&t1);
	if (rounds)
		direct_ns = seconds_between(t0, t1) * 1e9 / (double)rounds;

	if (handled_ns <= 0 || direct_ns <= 0) {
		c->note = "the clock said nothing";
		return;
	}
	detail(c, "%.0f ns handled against %.1f ns rewritten, %.0fx",
	       handled_ns, direct_ns, handled_ns / direct_ns);
}

/* ---- capability --------------------------------------------------------- */

static struct {
	int cpuid_bit;
	int win_feature;
	int executes;
	int addresses;		/* a written base is used for translation */
} cap;

static void probe_capability(void)
{
	unsigned eax, ebx, ecx, edx;
	struct sigaction sa, old;
	uint64_t base;

	if (__get_cpuid_max(0, NULL) >= 7) {
		__cpuid_count(7, 0, eax, ebx, ecx, edx);
		cap.cpuid_bit = (ebx & 1u) != 0;
	}
	cap.win_feature = IsProcessorFeaturePresent(PF_RDWRFSGSBASE_AVAILABLE) != 0;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = segv_handler;
	sa.sa_flags = SA_NODEFER;
	sigaction(SIGILL, &sa, &old);
	if (sigsetjmp(segv_return, 1) == 0) {
		(void)rdfsbase();
		cap.executes = 1;
	}
	sigaction(SIGILL, &old, NULL);
	if (!cap.executes)
		return;

	/* Spike 1's precondition, repeated because this spike's whole question
	 * is what happens when it stops holding. A base that is remembered but
	 * not used for translation would make every fault below meaningless. */
	base = distinct_base();
	wrfsbase(base);
	if (rdfsbase() == base) {
		veh_mode = VEH_OFF;
		sigaction(SIGSEGV, &sa, &old);
		if (sigsetjmp(segv_return, 1) == 0)
			cap.addresses = fs_self_raw() == base;
		sigaction(SIGSEGV, &old, NULL);
	}
	wrfsbase(0);
}

/* ---- reporting ---------------------------------------------------------- */

static const char *yesno(int b) { return b ? "yes" : "no"; }

static const char *event_shape(const struct event *e)
{
	if (e->note)
		return "unrun";
	if (e->base_survived)
		return "survived";
	return e->faulted ? "fault" : "read";
}

static void report(FILE *out, int terse, int nthreads, int seconds)
{
	int failed = 0, incomplete = 0, i;
	const char *verdict;

	for (i = 0; i < ncases; i++) {
		if (cases[i].note)
			incomplete++;
		else if (cases[i].failed)
			failed++;
	}
	for (i = 0; i < nevents; i++)
		if (!events[i].note && !events[i].base_survived && !events[i].faulted)
			any_read = 1;

	if (!cap.executes || !cap.addresses)
		verdict = "incomplete";
	else if (any_read)
		verdict = "reads";
	else if (incomplete)
		verdict = "incomplete";
	else
		verdict = failed ? "faults-not-resumable" : "faults-resumable";

	if (!terse) {
		fprintf(out, "== capability\n\n");
		fprintf(out, "    cpuid leaf 7 FSGSBASE      %s\n", yesno(cap.cpuid_bit));
		fprintf(out, "    IsProcessorFeaturePresent  %s\n", yesno(cap.win_feature));
		fprintf(out, "    the instruction executes   %s\n", yesno(cap.executes));
		fprintf(out, "    a written base addresses   %s\n", yesno(cap.addresses));
		fprintf(out, "\n    Spike 1's precondition, repeated. A base that is\n"
			     "    remembered but never used for translation would make\n"
			     "    every fault below mean nothing.\n\n");

		fprintf(out, "== what a zeroed base does, by however it got that way\n\n");
		fprintf(out, "    %-16s %-10s %s\n", "", "outcome", "what came back");
		for (i = 0; i < nevents; i++) {
			struct event *e = &events[i];

			if (e->note) {
				fprintf(out, "    %-16s %-10s %s\n", e->name, "-", e->note);
			} else if (e->base_survived) {
				fprintf(out, "    %-16s %-10s %s\n", e->name, "survived",
					"the base outlived the event; no access made");
			} else if (e->faulted) {
				fprintf(out, "    %-16s %-10s 0x%08lx %s at 0x%llx\n",
					e->name, "fault", (unsigned long)e->code,
					e->info0 == 0 ? "read" :
					e->info0 == 1 ? "write" : "execute",
					(unsigned long long)e->address);
			} else {
				fprintf(out, "    %-16s %-10s 0x%llx\n", e->name, "READ",
					(unsigned long long)e->value);
			}
		}
		fprintf(out, "\n    Spike 1 settled which events clear the base and this does\n"
			     "    not reargue it. What each row adds is the instruction after:\n"
			     "    the base is read back, and if it is gone a psABI access is\n"
			     "    made and its outcome recorded.\n\n");

		fprintf(out, "== the handler\n\n");
		fprintf(out, "    %-32s %-8s %s\n", "", "", "detail");
		for (i = 0; i < ncases; i++) {
			struct outcome *c = &cases[i];

			fprintf(out, "    %-32s %-8s %s\n", c->name,
				c->note ? "unrun" : c->failed ? "FAIL" : "ok",
				c->note ? c->note : c->detail);
		}
		fprintf(out, "\n    The forms are what a compiler emits for TLS on this target.\n"
			     "    The read-modify-write form is in the list to be refused:\n"
			     "    emulating it means emulating EFLAGS, and a handler that\n"
			     "    gets the carry flag wrong is worse than one that declines.\n"
			     "\n"
			     "    The two costs are not the same kind of number. A handled\n"
			     "    fault is serial by construction -- nothing overlaps with a\n"
			     "    kernel dispatch -- and the rewritten access is measured at\n"
			     "    throughput, several in flight at once. So the ratio is the\n"
			     "    most generous reading available to the rewriter, and the\n"
			     "    honest claim is the order of magnitude rather than the\n"
			     "    figure: three, not one.\n\n");

		fprintf(out, "== summary\n\n");
	}

#define K(fmt, ...) fprintf(out, "%s" fmt "\n", terse ? "" : "    ", __VA_ARGS__)
	K("verdict=%s", verdict);
	K("cases=%d", ncases);
	K("cases_failed=%d", failed);
	K("cases_incomplete=%d", incomplete);
	fprintf(out, "%sevent_shape=", terse ? "" : "    ");
	for (i = 0; i < nevents; i++)
		fprintf(out, "%s%s:%s", i ? "," : "", events[i].name,
			event_shape(&events[i]));
	fputc('\n', out);
	fprintf(out, "%sshape=", terse ? "" : "    ");
	for (i = 0; i < ncases; i++)
		fprintf(out, "%s%s:%s", i ? "," : "", cases[i].name,
			cases[i].note ? "unrun" : cases[i].failed ? "fail" : "pass");
	fputc('\n', out);
	K("faults_seen=%lu", (unsigned long)faults_seen);
	K("faults_handled=%lu", (unsigned long)faults_handled);
	K("faults_refused=%lu", (unsigned long)faults_refused);
	K("spin_reads=%llu", spin_reads);
	K("spin_wrong=%llu", spin_bad);
	K("spin_faults=%llu", spin_faults);
	if (spin_seconds > 0)
		K("spin_faults_per_second=%.0f", (double)spin_faults / spin_seconds);
	K("thread_reads=%llu", thread_reads);
	K("thread_wrong=%llu", thread_bad);
	K("threads=%d", thread_started);
	if (handled_ns > 0)
		K("handled_fault_ns=%.0f", handled_ns);
	if (direct_ns > 0)
		K("rewritten_access_ns=%.1f", direct_ns);
	if (handled_ns > 0 && direct_ns > 0)
		K("fault_cost_ratio=%.0f", handled_ns / direct_ns);
	K("load_threads=%d", nthreads);
	K("seconds=%d", seconds);
	K("probe=%s", PROBE_VERSION);
#undef K
}

static void usage(FILE *out)
{
	fputs("Usage:\n"
	      "  fs-fault-probe [options]\n"
	      "\n"
	      "Options:\n"
	      "  -r N, --rounds=N   Rounds in the cost case. [default: 200000]\n"
	      "  -j N, --threads=N  Threads in the concurrency case. [default: two per cpu]\n"
	      "  -s N, --seconds=N  Length of each timed case. [default: 5]\n"
	      "  -t, --terse        The summary block alone, one key=value per line.\n"
	      "  -d, --debug        Name each case on stderr as it starts.\n"
	      "  -V, --version      Print the version and exit.\n"
	      "  -h, --help         Print this message and exit.\n", out);
}

/* Both spellings, because the project's command-line convention says an option
 * takes its value either way and a probe nobody can drive the way the runner
 * drives it is a probe that silently does not run. */
static const char *value(int argc, char **argv, int *i)
{
	if (*i + 1 >= argc) {
		fprintf(stderr, "fs-fault-probe: %s wants a value\n", argv[*i]);
		exit(2);
	}
	return argv[++*i];
}

static long numeric(const char *what, const char *s)
{
	char *end;
	long v;

	errno = 0;
	v = strtol(s, &end, 10);
	if (errno || !*s || *end || v <= 0) {
		fprintf(stderr, "fs-fault-probe: %s wants a positive number, not %s\n",
			what, s);
		exit(2);
	}
	return v;
}

int main(int argc, char **argv)
{
	unsigned long rounds = 200000;
	int seconds = 5, nthreads = 0, terse = 0;
	LARGE_INTEGER hz;
	int i;

	ncpu_global = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (ncpu_global < 1)
		ncpu_global = 1;

	for (i = 1; i < argc; i++) {
		char *a = argv[i];

		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
			puts(PROBE_VERSION);
			return 0;
		} else if (!strcmp(a, "-t") || !strcmp(a, "--terse")) {
			terse = 1;
		} else if (!strcmp(a, "-d") || !strcmp(a, "--debug")) {
			debug = 1;
		} else if (!strcmp(a, "-r") || !strcmp(a, "--rounds")) {
			rounds = (unsigned long)numeric("--rounds", value(argc, argv, &i));
		} else if (!strncmp(a, "--rounds=", 9)) {
			rounds = (unsigned long)numeric("--rounds", a + 9);
		} else if (!strcmp(a, "-j") || !strcmp(a, "--threads")) {
			nthreads = (int)numeric("--threads", value(argc, argv, &i));
		} else if (!strncmp(a, "--threads=", 10)) {
			nthreads = (int)numeric("--threads", a + 10);
		} else if (!strcmp(a, "-s") || !strcmp(a, "--seconds")) {
			seconds = (int)numeric("--seconds", value(argc, argv, &i));
		} else if (!strncmp(a, "--seconds=", 10)) {
			seconds = (int)numeric("--seconds", a + 10);
		} else {
			fprintf(stderr, "fs-fault-probe: unknown option %s\n", a);
			usage(stderr);
			return 2;
		}
	}
	if (!nthreads)
		nthreads = ncpu_global * 2;

	setvbuf(stdout, NULL, _IOLBF, 0);
	QueryPerformanceFrequency(&hz);
	qpc_hz = (double)hz.QuadPart;

	if (install_veh() != 0) {
		fprintf(stderr, "fs-fault-probe: no vectored handler could be installed\n");
		return 4;
	}

	probe_capability();
	if (!cap.executes || !cap.addresses) {
		report(stdout, terse, nthreads, seconds);
		return 4;
	}

	tp_main = make_block();
	c3_establish(tp_main);

	trace("events");
	measure_event("explicit zero", p_explicit);
	measure_event("syscall", p_syscall);
	measure_event("yields", p_yield);
	measure_event("blocking wait", p_blocking_wait);
	measure_event("migration", p_migration);
	measure_event("apc", p_apc);
	measure_event("signal, sync", p_signal_sync);
	measure_event("signal, fault", p_signal_fault);
	measure_event("hijack", p_hijack);
	measure_event("preemption", p_preemption);
	measure_thread_start();
	measure_fork();

	/* The events section runs helper threads and forks, either of which can
	 * leave this thread's carrier behind. Re-establish before the handler
	 * is asked to use it. */
	c3_establish(tp_main);

	case_sees();
	case_forms();
	case_registers();
	case_spin(seconds);
	case_threads(nthreads, seconds);
	case_cost(rounds);

	report(stdout, terse, nthreads, seconds);

	for (i = 0; i < ncases; i++)
		if (cases[i].note)
			return 4;
	if (any_read)
		return 3;
	for (i = 0; i < ncases; i++)
		if (cases[i].failed)
			return 3;
	return 0;
}
