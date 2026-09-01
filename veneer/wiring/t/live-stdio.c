/*
 * live-stdio -- WP-56's thirteenth live crossing, and the first that crosses
 * a slice by its bind alone. The bind loop resolves the stdio slice's real
 * table against a real elfsysv1.dll and every one of its rows reaches an
 * export, but no stdio thunk is called: unlike every slice crossed before it,
 * stdio offers the freestanding harness no body it may safely enter.
 *
 * The stdio slice is wire-stdio.gen.c: 97 rows, no shim (an empty
 * wire-stdio.shims.tsv), so its bind check is math's, stdlib's, sockets's,
 * posix's, time's, misc's, wchar's and terminal's shape -- every row must
 * resolve, missing == 0. That holds: all 88 distinct export names the table
 * reaches (fopen, vfprintf, fread, ... and the version/LFS aliases collapsed
 * onto their base, fopen64 -> fopen, fgetpos64 -> fgetpos, and their kin) are
 * exported by the real DLL.
 *
 * What stdio is, that no earlier crossed slice was, is a slice whose bodies
 * are categorically beyond a freestanding caller. Every crossing before this
 * one exercised a body by choosing the slice's NOSIGFE forwards -- the ones
 * standing on no reent, locale, table or kernel: math's fabs, string's ffs,
 * stdlib's abs, sockets's htonl, locale's toascii, time's difftime, misc's
 * insque, terminal's cfmakeraw. stdio has no such row to offer. Of its 97
 * wired rows exactly one is marked NOSIGFE in runtime/exports/cygwin-exports.
 * tsv -- cuserid -- and cuserid is no pure mask: its body reads the process's
 * user through the cygheap this freestanding specimen never establishes, so
 * calling it here would fault or read uninitialised state exactly the way
 * calling locale's setlocale would. The other 96 rows are SIGFE: their thunks
 * want the signal-frame entry the specimen deliberately compiles without, and
 * the printf/scanf/FILE families behind them stand on _REENT besides. NOSIGFE
 * names the calling convention a thunk needs, not whether the body behind it
 * stands on its own (live-locale.c's lesson); stdio is where the two part
 * ways completely, and the slice offers not even locale's single exception.
 *
 * So this crossing calls nothing. It certifies the one thing a freestanding
 * harness can certify of stdio against a real DLL -- that the bind is real and
 * faithful -- and leaves the bodies to the two bars that can reach them: the
 * per-slice differential (diff-slice.sh, on the pinned el8 image, over the
 * WP-T2 environment) for their observable glibc behaviour, and process
 * bring-up for their live NT behaviour. The decision this branch adds records
 * the rule, since the SIGFE-heavy slices still uncrossed (memory, signal,
 * process, identity, io-mux, threads, regex, syslog, sysv-ipc, io, system)
 * inherit it: a SIGFE slice with no NOSIGFE-and-pure row crosses live by its
 * bind, and its bodies wait.
 *
 * The specimen reads the table the bind filled and the DLL's own PE header,
 * never a libc datum, so every check is a property of the crossing itself.
 * Reports one bit per check through the terminator the stub puts in %rdx, so
 * 31 is the only passing status (five checks), a pass meaning the bind-only
 * crossing holds and reproduces:
 *
 *   0x01  the bind resolved every row of the 97-row all-forward table
 *         (missing 0, n > 0): every stdio name reaches an export
 *   0x02  every filled slot lands inside the DLL's mapped image
 *         [base, base + SizeOfImage): a resolved thunk tail-jumps into the
 *         real body region, not off into unmapped space
 *   0x04  the resolver discriminates rather than handing back any address: a
 *         real export (fopen) resolves non-null, while the un-collapsed LFS
 *         alias name (fopen64, which the DLL does not export) and a junk name
 *         both resolve null -- so missing == 0 above is a fact about the names
 *   0x08  distinct exported names reach distinct bodies: fopen, fclose and
 *         vfprintf resolve to three different addresses
 *   0x10  the bind is idempotent (wire.h's contract): a second bind returns
 *         missing 0 and leaves every slot equal to a fresh resolve of its
 *         name -- a rebind after a runtime reload is a plain re-run
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

extern struct esn_wire_ent __esn_wire_stdio[];
extern const unsigned long __esn_wire_stdio_n;

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

void live_stdio_main(uint64_t *sp, terminator_fn leave)
{
	uint64_t status = 0;
	uint64_t *p;
	const uint8_t *rt = 0;
	size_t missing;

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

		missing = __esn_wire_bind(__esn_wire_stdio,
		                          __esn_wire_stdio_n,
		                          resolve, (void *) rt);
		if (missing == 0 && __esn_wire_stdio_n > 0)
			status |= 0x01;

		/* Every filled slot lands inside the mapped image span. */
		{
			int all_in = 1;

			for (i = 0; i < __esn_wire_stdio_n; i++) {
				uintptr_t fn = (uintptr_t) __esn_wire_stdio[i].fn;

				if (fn < base || fn >= end)
					all_in = 0;
			}
			if (all_in && __esn_wire_stdio_n > 0)
				status |= 0x02;
		}

		/* The resolver discriminates: a real export resolves, the
		 * un-collapsed LFS alias name and a junk name do not. */
		if (pe_export(rt, "fopen") != 0 &&
		    pe_export(rt, "fopen64") == 0 &&
		    pe_export(rt, "__no_such_stdio_export_zzq") == 0)
			status |= 0x04;

		/* Distinct exported names reach distinct bodies. */
		{
			void *a = pe_export(rt, "fopen");
			void *b = pe_export(rt, "fclose");
			void *c = pe_export(rt, "vfprintf");

			if (a && b && c && a != b && b != c && a != c)
				status |= 0x08;
		}

		/* Idempotent rebind: missing 0 again, every slot equal to a
		 * fresh resolve of its name. */
		{
			size_t missing2 = __esn_wire_bind(__esn_wire_stdio,
			                                  __esn_wire_stdio_n,
			                                  resolve, (void *) rt);
			int same = (missing2 == 0);

			for (i = 0; i < __esn_wire_stdio_n; i++) {
				void *fresh = pe_export(rt,
				        __esn_wire_stdio[i].export_name);

				if (__esn_wire_stdio[i].fn != fresh)
					same = 0;
			}
			if (same && __esn_wire_stdio_n > 0)
				status |= 0x10;
		}
	}

	leave(status);
}
