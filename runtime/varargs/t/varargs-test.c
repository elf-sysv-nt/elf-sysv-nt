/* WP-24 done-condition, made runnable.
 *
 * Four cases. The first is the reason the package exists; the middle two are
 * the done-condition itself; the last exercises the scan direction.
 *
 *   incompat   the two va_list types are different sizes, and a System V list
 *              read the way a Microsoft reader would fetches the descriptor
 *              header where the first argument should be. This is spike 3's
 *              varargs-raw, kept here so the veneer's whole reason is checked
 *              beside the veneer.
 *   printf16   a printf call with sixteen mixed integer and floating arguments
 *              prints what Linux prints. The reference string was taken from
 *              glibc 2.35 (see the README and the reproduce transcript); the
 *              case also cross-checks against the host's own formatter live.
 *   vfprintf   vfprintf reached through a va_list built on the System V side,
 *              with a Microsoft-ABI callee underneath.
 *   sscanf     the scan bridge: one pointer slot per conversion, parsed back.
 *
 * The generated veneer is compiled under the v24_ prefix (see run-tests.sh) so
 * its printf family does not collide with the host libc; every v24_ symbol is
 * the generator's own output, sysv_abi, unchanged but for the name.
 */
#include "vp.h"		/* the prefixed veneer declarations */
#include "../sv2ms.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define SYSV __attribute__((sysv_abi, noinline))

static int failures;

static void ok(const char *name, int cond, const char *detail)
{
	fprintf(stderr, "  %-9s %s%s%s\n", name, cond ? "pass" : "FAIL",
		detail && *detail ? "  " : "", detail ? detail : "");
	if (!cond)
		failures++;
}

/* The 16-argument format and its arguments, shared by printf16 and the host
 * cross-check. Mixed integer and floating, in an order that puts each in a
 * different register slot on the System V side. */
#define FMT16 "%d %f %ld %s %x %c %g %d %lld %u %f %s %d %e %d %f"
#define ARGS16 1, 2.5, 3L, "four", 0x5a, 'Z', 7.125, 8, \
	9LL, 10u, 11.5, "twelve", 13, 1.4e3, 15, 16.0
/* Verified equal to glibc 2.35 printf of FMT16/ARGS16; see the README. */
static const char GOLDEN16[] =
	"1 2.500000 3 four 5a Z 7.125 8 9 10 11.500000 twelve 13 1.400000e+03 15 16.000000";

/* ---- incompat: the two lists are not the same shape --------------------- */

SYSV static long first_as_sysv(const char *fmt, ...)
{
	__sysv_va_list ap;
	long v;
	__sysv_va_start(ap, fmt);
	v = __sysv_va_arg(ap, long);
	__sysv_va_end(ap);
	return v;
}

/* Read the head of a System V list the way an eight-byte Microsoft pointer
 * would: the first word of the descriptor is gp_offset|fp_offset, not the
 * first argument. */
SYSV static long first_as_ms(const char *fmt, ...)
{
	__sysv_va_list ap;
	long v;
	__sysv_va_start(ap, fmt);
	memcpy(&v, ap, sizeof v);	/* the first eight bytes: the descriptor head */
	__sysv_va_end(ap);
	return v;
}

static void case_incompat(void)
{
	char d[128];
	long right = first_as_sysv("%ld", 111L);
	long wrong = first_as_ms("%ld", 111L);

	ok("incompat", sizeof(__sysv_va_list) != sizeof(va_list),
	   (snprintf(d, sizeof d, "sysv va_list %u bytes, ms %u",
		     (unsigned)sizeof(__sysv_va_list), (unsigned)sizeof(va_list)), d));
	ok("incompat", right == 111L, "System V reading recovers the argument");
	ok("incompat", wrong != 111L,
	   "a Microsoft-shaped read of a System V list is not the argument");
}

/* ---- printf16: prints what Linux prints --------------------------------- */

static void case_printf16(void)
{
	char got[256], host[256];
	int rc, saved, hostlen;
	FILE *tf;

	/* the veneer's snprintf path, for an exact string to compare */
	rc = v24_snprintf(got, sizeof got, FMT16, ARGS16);
	ok("printf16", rc == (int)strlen(GOLDEN16) && strcmp(got, GOLDEN16) == 0, got);

	/* the veneer's printf path, its output captured off fd 1 */
	fflush(stdout);
	saved = dup(1);
	tf = tmpfile();
	if (tf && saved >= 0) {
		char cap[256];
		size_t n;
		dup2(fileno(tf), 1);
		v24_printf(FMT16, ARGS16);
		fflush(stdout);
		dup2(saved, 1);
		close(saved);
		rewind(tf);
		n = fread(cap, 1, sizeof cap - 1, tf);
		cap[n] = '\0';
		fclose(tf);
		ok("printf16", strcmp(cap, GOLDEN16) == 0, "printf path matches glibc");
	} else {
		ok("printf16", 0, "could not capture stdout");
	}

	/* live cross-check: the host's own formatter on the same arguments */
	hostlen = snprintf(host, sizeof host, FMT16, ARGS16);
	ok("printf16", hostlen == rc && strcmp(host, got) == 0,
	   "veneer output equals the host formatter live");
}

/* ---- vfprintf: through a System V va_list, MS-ABI callee ----------------- */

/* A System V-side caller that builds a va_list and hands it to the veneer's
 * vfprintf, exactly as a Linux program calling vfprintf would. */
SYSV static int feed_vfprintf(FILE *fp, const char *fmt, ...)
{
	__sysv_va_list ap;
	int r;
	__sysv_va_start(ap, fmt);
	r = v24_vfprintf(fp, fmt, ap);
	__sysv_va_end(ap);
	return r;
}

static void case_vfprintf(void)
{
	FILE *tf = tmpfile();
	char got[256];
	size_t n = 0;
	int rc = -1;

	if (!tf) { ok("vfprintf", 0, "no tmpfile"); return; }
	rc = feed_vfprintf(tf, FMT16, ARGS16);
	fflush(tf);
	rewind(tf);
	n = fread(got, 1, sizeof got - 1, tf);
	got[n] = '\0';
	fclose(tf);
	ok("vfprintf", rc == (int)strlen(GOLDEN16) && strcmp(got, GOLDEN16) == 0,
	   "va_list built System V-side, formatted MS-ABI-side");
}

/* ---- sscanf: the scan bridge -------------------------------------------- */

static void case_sscanf(void)
{
	int a = 0, c = 0;
	double b = 0;
	char s[32] = {0};
	int got = v24_sscanf("42 3.5 hello 99", "%d %lf %31s %x", &a, &b, s, &c);
	char d[96];
	snprintf(d, sizeof d, "n=%d a=%d b=%g s=%s c=%d", got, a, b, s, c);
	ok("sscanf", got == 4 && a == 42 && b == 3.5 && strcmp(s, "hello") == 0 && c == 0x99, d);
}

int main(void)
{
	fprintf(stderr, "WP-24 variadic veneer\n");
	case_incompat();
	case_printf16();
	case_vfprintf();
	case_sscanf();
	fprintf(stderr, "\nverdict=%s failures=%d\n", failures ? "no" : "yes", failures);
	printf("verdict=%s\n", failures ? "no" : "yes");
	return failures ? 1 : 0;
}
