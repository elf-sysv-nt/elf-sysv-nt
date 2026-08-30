/* WP-36 unit certification: the version matcher against version tables laid
 * out in memory the way an object carries them -- no cross build, the same
 * approach WP-35's unit mode takes with synthetic hash tables.
 *
 * It exercises the two answers the done-condition names. Binding: a reference
 * to sym@GLIBC_2.14 reaches the definition at GLIBC_2.14 and not the one at
 * GLIBC_2.2.5 in the same object, an unversioned reference reaches the default
 * (@@) definition and skips the hidden (@) one, and a version-node carries its
 * predecessor so GLIBC_2.14 implies GLIBC_2.2.5. Refusal: a consumer requiring
 * a present version loads, a weak requirement that is absent is tolerated, and
 * a non-weak requirement that is absent is refused with the message a real
 * ld.so gives. */
#define _GNU_SOURCE
#include "../elf_version.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int failures;
static void ck(const char *what, int ok)
{
	printf("    %-58s %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failures++;
}

/* One shared string table; every version_info indexes into it. */
static char     g_str[512];
static uint32_t g_strn = 1;              /* offset 0 is the empty string */
static uint32_t S(const char *name)
{
	uint32_t o = g_strn;
	strcpy(g_str + o, name);
	g_strn += (uint32_t) strlen(name) + 1;
	return o;
}

/* ---- the provider: defines GLIBC_2.2.5 and GLIBC_2.14 ------------------- */
static uint8_t       prov_vd[256];       /* .gnu.version_d */
static Elf64_Versym  prov_vs[8];         /* .gnu.version, one per dynsym */
static elf_lookup_object prov_lo;
static elf_version_object prov;

/* symbol indices in the provider */
enum { SYM_UNDEF = 0, SYM_AT_214 = 1, SYM_AT_225 = 2 };

static void build_provider(uint32_t off_soname, uint32_t off_225, uint32_t off_214)
{
	uint8_t *p = prov_vd;
	Elf64_Verdef vd; Elf64_Verdaux ad;

	/* node 1: the base (the object's own name), vd_ndx 1, one aux */
	memset(&vd, 0, sizeof vd);
	vd.vd_version = 1; vd.vd_flags = ELF_VER_FLG_BASE; vd.vd_ndx = 1;
	vd.vd_cnt = 1; vd.vd_aux = sizeof vd; vd.vd_next = sizeof vd + sizeof ad;
	memcpy(p, &vd, sizeof vd);
	ad.vda_name = off_soname; ad.vda_next = 0;
	memcpy(p + sizeof vd, &ad, sizeof ad);
	p += vd.vd_next;

	/* node 2: GLIBC_2.2.5, vd_ndx 2, one aux */
	memset(&vd, 0, sizeof vd);
	vd.vd_version = 1; vd.vd_ndx = 2; vd.vd_cnt = 1;
	vd.vd_aux = sizeof vd; vd.vd_next = sizeof vd + sizeof ad;
	memcpy(p, &vd, sizeof vd);
	ad.vda_name = off_225; ad.vda_next = 0;
	memcpy(p + sizeof vd, &ad, sizeof ad);
	p += vd.vd_next;

	/* node 3: GLIBC_2.14, vd_ndx 3, two aux -- itself then predecessor 2.2.5 */
	memset(&vd, 0, sizeof vd);
	vd.vd_version = 1; vd.vd_ndx = 3; vd.vd_cnt = 2;
	vd.vd_aux = sizeof vd; vd.vd_next = 0;
	memcpy(p, &vd, sizeof vd);
	ad.vda_name = off_214; ad.vda_next = sizeof ad;
	memcpy(p + sizeof vd, &ad, sizeof ad);
	ad.vda_name = off_225; ad.vda_next = 0;
	memcpy(p + sizeof vd + sizeof ad, &ad, sizeof ad);

	/* versym: the default def of sym at 2.14 (ndx 3, not hidden), the
	 * non-default at 2.2.5 (ndx 2, hidden). */
	prov_vs[SYM_UNDEF]  = 0;
	prov_vs[SYM_AT_214] = 3;
	prov_vs[SYM_AT_225] = (Elf64_Versym) (2 | ELF_VERSYM_HIDDEN);

	prov_lo.name = "libprov.so";
	prov.lo = &prov_lo;
	memset(&prov.vi, 0, sizeof prov.vi);
	prov.vi.strtab = g_str; prov.vi.strsz = sizeof g_str;
	prov.vi.versym = prov_vs;
	prov.vi.verdef = prov_vd; prov.vi.verdefnum = 3;
}

/* ---- a consumer: verneed requiring versions from libprov.so ------------- */
static uint8_t       cons_vr[256];
static Elf64_Versym  cons_vs[4];
static elf_lookup_object cons_lo;
static elf_version_object cons;

/* Build one verneed record for libprov.so with two vernaux: the first for
 * `name_a' (flags_a, local ndx 2), the second for `name_b' (flags_b, ndx 3).
 * A consumer versym at index 1 references local ndx 2 (name_a). */
static void build_consumer(uint32_t off_file, uint32_t off_a, uint16_t flags_a,
                           uint32_t off_b, uint16_t flags_b)
{
	Elf64_Verneed vn; Elf64_Vernaux vna;
	uint8_t *p = cons_vr;

	memset(&vn, 0, sizeof vn);
	vn.vn_version = 1; vn.vn_cnt = 2; vn.vn_file = off_file;
	vn.vn_aux = sizeof vn; vn.vn_next = 0;
	memcpy(p, &vn, sizeof vn);

	memset(&vna, 0, sizeof vna);
	vna.vna_flags = flags_a; vna.vna_other = 2; vna.vna_name = off_a;
	vna.vna_next = sizeof vna;
	memcpy(p + sizeof vn, &vna, sizeof vna);

	memset(&vna, 0, sizeof vna);
	vna.vna_flags = flags_b; vna.vna_other = 3; vna.vna_name = off_b;
	vna.vna_next = 0;
	memcpy(p + sizeof vn + sizeof vna, &vna, sizeof vna);

	cons_vs[0] = 0; cons_vs[1] = 2;   /* the reference at symidx 1 wants ndx 2 */
	cons_lo.name = "libcons.so";
	cons.lo = &cons_lo;
	memset(&cons.vi, 0, sizeof cons.vi);
	cons.vi.strtab = g_str; cons.vi.strsz = sizeof g_str;
	cons.vi.versym = cons_vs;
	cons.vi.verneed = cons_vr; cons.vi.verneednum = 1;
}

int main(void)
{
	uint32_t off_soname = S("libprov.so");
	uint32_t off_225 = S("GLIBC_2.2.5");
	uint32_t off_214 = S("GLIBC_2.14");
	uint32_t off_99  = S("GLIBC_9.9");
	elf_version_object objs[1];
	elf_version_ctx ctx;
	char msg[128]; const char *bad; unsigned weak;
	int r;

	build_provider(off_soname, off_225, off_214);
	objs[0] = prov;

	printf("WP-36 version matcher\n");

	/* object_defines: the two nodes, the predecessor, and an absent one. */
	ck("provider defines GLIBC_2.14",
	   elf_version_object_defines(&prov, "GLIBC_2.14", 0) == 1);
	ck("provider defines GLIBC_2.2.5 (its own node)",
	   elf_version_object_defines(&prov, "GLIBC_2.2.5", 0) == 1);
	ck("GLIBC_2.14 implies GLIBC_2.2.5 as a predecessor",
	   elf_version_object_defines(&prov, "GLIBC_2.2.5", 0) == 1);
	ck("provider does not define GLIBC_9.9",
	   elf_version_object_defines(&prov, "GLIBC_9.9", 0) == 0);

	/* binding: a reference to sym@GLIBC_2.14 binds the 2.14 body, not 2.2.5. */
	memset(&ctx, 0, sizeof ctx);
	ctx.req_name = "GLIBC_2.14"; ctx.objs = objs; ctx.nobjs = 1;
	ck("ref@GLIBC_2.14 binds the 2.14 default definition",
	   elf_version_match(&prov_lo, SYM_AT_214, &ctx) == 1);
	ck("ref@GLIBC_2.14 rejects the GLIBC_2.2.5 definition",
	   elf_version_match(&prov_lo, SYM_AT_225, &ctx) == -1);

	/* an unversioned reference binds the default (@@), skips the hidden (@). */
	memset(&ctx, 0, sizeof ctx);
	ctx.req_name = NULL; ctx.objs = objs; ctx.nobjs = 1;
	ck("unversioned ref binds the default (@@) definition",
	   elf_version_match(&prov_lo, SYM_AT_214, &ctx) == 1);
	ck("unversioned ref rejects the hidden (@) definition",
	   elf_version_match(&prov_lo, SYM_AT_225, &ctx) == -1);

	/* ctx_init reads the required version name off the consumer's verneed. */
	build_consumer(off_soname, off_214, 0 /*non-weak*/, off_99, ELF_VER_FLG_WEAK);
	r = elf_version_ctx_init(&ctx, &cons, 1, objs, 1, NULL);
	ck("ctx_init resolves the reference's required version",
	   r == 0 && ctx.req_name && strcmp(ctx.req_name, "GLIBC_2.14") == 0);

	/* check_needed: 2.14 present (satisfied), 9.9 weak absent (tolerated). */
	weak = 99; bad = (const char *) 1;
	r = elf_version_check_needed(&cons, "libcons.so", objs, 1,
	                             msg, sizeof msg, &bad, &weak);
	ck("required GLIBC_2.14 present, weak GLIBC_9.9 tolerated",
	   r == 0 && weak == 1 && bad == NULL);

	/* the same requirement made NON-weak is refused, with ld.so's message. */
	build_consumer(off_soname, off_214, 0, off_99, 0 /*non-weak now*/);
	msg[0] = 0; bad = NULL;
	r = elf_version_check_needed(&cons, "libcons.so", objs, 1,
	                             msg, sizeof msg, &bad, NULL);
	ck("non-weak GLIBC_9.9 absent refuses the load",
	   r == -1);
	ck("refusal names the version and the requiring object",
	   strcmp(msg, "version `GLIBC_9.9' not found (required by libcons.so)") == 0);
	ck("refusal reports the library that should have provided it",
	   bad && strcmp(bad, "libprov.so") == 0);

	printf("\n%d checks, %d failed\n",
	       15, failures);
	return failures ? 1 : 0;
}
