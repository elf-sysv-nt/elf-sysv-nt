/*
 * live-signal -- WP-56's sixteenth live crossing, crossed by its bind alone.
 * The bind loop (wire.c) resolves the signal slice's real table against a real
 * elfsysv1.dll; no signal body is called.
 *
 * The signal slice is wire-signal.gen.c: 29 rows, 12 forwards and 17 shims --
 * the sigset operators (sigemptyset, sigfillset, sigaddset, sigdelset,
 * sigismember) and the disposition and delivery calls (signal, sigaction,
 * sigprocmask, sigpending, sigsuspend, sigqueue, sigtimedwait, sigwaitinfo,
 * sigaltstack) as shims, with kill, killpg, raise, psignal and the System V
 * XSI conveniences (sighold, sigrelse, sigignore, sigset, sigpause) as
 * forwards. Every row is SIGFE and none is stateless, so the crossing asks the
 * same question memory and filesystem did: does a Cygwin-faced DLL export the
 * whole set, or does the bind leave rows a shim body must synthesise?
 *
 * Measurement answers cleanly: the bind leaves exactly two rows null, and they
 * are exactly the rows a real shim must synthesise -- __sysv_signal and
 * sysv_signal, the System V unreliable-signal disposition setters, which glibc
 * exports but Cygwin has no ABI for. Cygwin exports plain signal (BSD reliable
 * semantics) and sigaction, and a translating body must build the System V
 * one-shot, no-mask disposition on top of them; there is no export to alias
 * onto, as memory's mmap64 aliased onto mmap. Every other signal export glibc
 * names, Cygwin exports under the same name -- the sigset operators, the
 * delivery calls, and the XSI conveniences alike. So signal sits between
 * filesystem's eleven and memory's none: two rows for a shim to synthesise,
 * twenty-seven that forward.
 *
 * Crossed by its bind alone, not by call: no signal row is stateless. The
 * sigset operators look pure -- sigemptyset only clears a caller's mask -- but
 * Cygwin's are SIGFE, entering the runtime's cygtls on the way in, and a
 * freestanding harness never brings that up; the delivery calls stand on the
 * process's signal state outright. So calling a body here would read or mutate
 * state the harness never initialised -- the same trap fnmatch sprang in
 * live-filesystem. The bodies are left to the two bars that reach them:
 * diff-slice.sh on the pinned el8 image, and process bring-up.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx,
 * so 31 is the only passing status (five checks):
 *
 *   0x01  the bind left exactly the __sysv_signal and sysv_signal rows
 *         unresolved and every other row filled -- the finding as a check:
 *         signal needs a two-row shim, unlike memory's none
 *   0x02  every filled slot lands inside the DLL's mapped image span, so a
 *         resolved thunk tail-jumps into the real body region, not unmapped
 *         space
 *   0x04  the resolver discriminates: sigaction resolves; __sysv_signal does
 *         not (Cygwin has no System V disposition export); a junk name does not
 *   0x08  distinct exported names reach distinct bodies (sigaction,
 *         sigprocmask, kill)
 *   0x10  the bind is idempotent, per DR-0049: a rebind leaves the same two
 *         rows null and every filled slot equal to a fresh resolve of its
 *         export name
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_signal[];
extern const unsigned long __esn_wire_signal_n;

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

static int str_eq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

/* Resolve one export by name from a loaded PE image -- the same walk
 * runtime/face/t/elfcall.c uses, adapted to wire.h's resolver shape so it
 * can stand in for the runtime's eventual GetProcAddress callback. */
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

/* SizeOfImage from the PE32+ optional header: opt starts past the DOS stub,
 * the PE signature and the 20-byte file header, and SizeOfImage sits 56 bytes
 * into it -- the same header this file's pe_export already walks. */
static uint32_t pe_size_of_image(const uint8_t *base)
{
	uint32_t lfanew = rd32(base + 0x3C);
	const uint8_t *opt = base + lfanew + 4 + 20;

	return rd32(opt + 56);
}

/* The finding as a predicate: a row is expected null exactly when its export
 * name is one of the two System V disposition setters Cygwin does not carry. */
static int expected_null(const char *export_name)
{
	return str_eq(export_name, "__sysv_signal") ||
	       str_eq(export_name, "sysv_signal");
}

void live_signal_main(uint64_t *sp, terminator_fn leave)
{
	uint64_t status = 0;
	uint64_t *p;
	const uint8_t *rt = 0;

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
		size_t i;
		uintptr_t base = (uintptr_t) rt;
		uintptr_t end = base + pe_size_of_image(rt);

		(void) __esn_wire_bind(__esn_wire_signal,
		       __esn_wire_signal_n,
		       resolve, (void *) rt);

		/* The finding as a check: exactly the two System V rows are
		 * left null, and every other row is filled. */
		{
			int as_expected = 1;

			for (i = 0; i < __esn_wire_signal_n; i++) {
				int want_null =
				    expected_null(__esn_wire_signal[i].export_name);
				int is_null = __esn_wire_signal[i].fn == 0;

				if (want_null != is_null)
					as_expected = 0;
			}
			if (as_expected && __esn_wire_signal_n > 0)
				status |= 0x01;
		}

		/* Every filled slot lands inside the mapped image span. */
		{
			int all_in = 1;

			for (i = 0; i < __esn_wire_signal_n; i++) {
				uintptr_t fn = (uintptr_t) __esn_wire_signal[i].fn;

				if (fn == 0)
					continue;
				if (fn < base || fn >= end)
					all_in = 0;
			}
			if (all_in && __esn_wire_signal_n > 0)
				status |= 0x02;
		}

		/* The resolver discriminates: a real forward resolves, the
		 * System V name does not (Cygwin has no such export), a junk
		 * name does not. */
		if (pe_export(rt, "sigaction") != 0 &&
		    pe_export(rt, "__sysv_signal") == 0 &&
		    pe_export(rt, "__no_such_signal_export_zzq") == 0)
			status |= 0x04;

		/* Distinct exported names reach distinct bodies. */
		{
			void *a = pe_export(rt, "sigaction");
			void *b = pe_export(rt, "sigprocmask");
			void *c = pe_export(rt, "kill");

			if (a && b && c && a != b && b != c && a != c)
				status |= 0x08;
		}

		/* Idempotent rebind, per DR-0049: the same two rows null again,
		 * every filled slot equal to a fresh resolve of its export
		 * name. */
		{
			int same = 1;

			(void) __esn_wire_bind(__esn_wire_signal,
			       __esn_wire_signal_n,
			       resolve, (void *) rt);
			for (i = 0; i < __esn_wire_signal_n; i++) {
				void *fresh = pe_export(rt,
				    __esn_wire_signal[i].export_name);

				if (__esn_wire_signal[i].fn != fresh)
					same = 0;
			}
			if (same && __esn_wire_signal_n > 0)
				status |= 0x10;
		}
	}

	leave(status);
}
