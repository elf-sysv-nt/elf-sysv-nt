/*
 * elfcall -- WP-27's last done-when clause: a static ELF, entered through
 * WP-41's branch, calls a real export of the faced runtime and returns.
 *
 * The stub loaded the runtime and put its module base in AT_BASE, which a
 * static image otherwise reads as 0 (the runtime-load decision record). This
 * specimen walks the auxv to that base, resolves exports out of the image's
 * own PE export directory, and calls them the way every ELF program will:
 * System V convention, straight at the export, which is the face WP-27 put
 * there. The exports are the NOSIGFE leaves t/crossing.c chose, so the calls
 * cross the face without the runtime's process state mattering, through both
 * the generic int face and the typed fp thunks.
 *
 * Freestanding, cross-compiled, no libc: every helper is written here. It
 * reports the way WP-41's specimen does, one bit per check through the
 * terminator the stub put in %rdx, so 127 is the only passing status.
 *
 *   0x01  AT_BASE is present and nonzero
 *   0x02  strlen resolves from the PE export directory
 *   0x04  strlen("elfsysv") is 7            (generic int face)
 *   0x08  memcmp orders both ways and ties  (generic int face)
 *   0x10  labs(-42) and labs(42) are 42     (generic int face)
 *   0x20  ldexp(1.0, 8) is 256.0            (typed fp thunk)
 *   0x40  atan2(0.0, 1.0) is 0.0            (typed fp thunk)
 */

#include <stdint.h>

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

/* The System V shapes of the exports called. This is the ABI the compiler
 * speaks natively here, so no attribute: the call is the product's own. long
 * is 64 bits on this target, spelled int64_t to say so. */
typedef uint64_t (*strlen_fn)(const char *);
typedef int      (*memcmp_fn)(const void *, const void *, uint64_t);
typedef int64_t  (*labs_fn)(int64_t);
typedef double   (*ldexp_fn)(double, int);
typedef double   (*atan2_fn)(double, double);

/* Byte-wise reads: the RVAs land anywhere, and unaligned access through a
 * cast is not something this specimen gets to assume. */
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

/* Resolve one export by name from a loaded PE image, the loader's own walk:
 * DOS header to e_lfanew, PE signature, the PE32+ optional header, data
 * directory zero, then the name table to an ordinal to a function RVA. The
 * exports this test asks for are code, not forwarders, so a forwarder check
 * is not carried. */
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

void elfcall_main(uint64_t *sp, terminator_fn leave)
{
	uint64_t status = 0;
	uint64_t *p;
	const uint8_t *rt = 0;
	strlen_fn f_strlen;
	memcmp_fn f_memcmp;
	labs_fn f_labs;
	ldexp_fn f_ldexp;
	atan2_fn f_atan2;

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
	if (rt)
		status |= 0x01;

	f_strlen = rt ? (strlen_fn) pe_export(rt, "strlen") : 0;
	if (f_strlen)
		status |= 0x02;
	if (f_strlen && f_strlen("elfsysv") == 7)
		status |= 0x04;

	f_memcmp = rt ? (memcmp_fn) pe_export(rt, "memcmp") : 0;
	if (f_memcmp && f_memcmp("abc", "abd", 3) < 0 &&
	    f_memcmp("abd", "abc", 3) > 0 && f_memcmp("abc", "abc", 3) == 0)
		status |= 0x08;

	f_labs = rt ? (labs_fn) pe_export(rt, "labs") : 0;
	if (f_labs && f_labs(-42) == 42 && f_labs(42) == 42)
		status |= 0x10;

	f_ldexp = rt ? (ldexp_fn) pe_export(rt, "ldexp") : 0;
	if (f_ldexp && f_ldexp(1.0, 8) == 256.0)
		status |= 0x20;

	f_atan2 = rt ? (atan2_fn) pe_export(rt, "atan2") : 0;
	if (f_atan2 && f_atan2(0.0, 1.0) == 0.0)
		status |= 0x40;

	leave(status);
}
