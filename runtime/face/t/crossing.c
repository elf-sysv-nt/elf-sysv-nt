/*
 * crossing -- WP-27's rerun of the crossing certification against the real
 * DLL.
 *
 * WP-22 and WP-23 certified the convention crossing at stand-in width: the
 * entry points and trampolines were compiled into the test binary itself.
 * This test points the same instruments at the faced DLL. It loads
 * elfsysv1.dll -- rebadged, so it coexists with the host's own cygwin1.dll
 * in this process -- resolves exports whose faces the generators wrote, and
 * calls them the way an ELF caller will: System V convention, straight at
 * the export.
 *
 * Two instruments, both WP-22's. The typed sysv_abi calls check that values
 * cross -- arguments in, result out -- through a generic int face and
 * through the typed fp thunks. sysv_cross_probe from probe.S, unchanged,
 * poisons the System V callee-saved set around a call into the DLL and
 * reports any register that moved; its leaky control proves the check can
 * fail. The exports here are NOSIGFE leaves on purpose: they cross the face
 * without needing the second runtime initialized, which keeps this test
 * about the face. The sigfe-fenced surface and the variadic veneer need the
 * DLL's runtime brought up beneath a real ELF process, which is the later
 * milestones' work.
 *
 * Built and driven by t/crossing.sh with the host gcc, -mno-red-zone.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* probe.S, linked in unchanged from runtime/core/t. */
extern uint32_t sysv_cross_probe(void *fn, uint64_t arg);
extern void elfsysv_leaky_sysv(uint64_t);

#define SYSV_FULL_MASK 0x3fu

/* The System V shapes of the exports this test calls. */
typedef uint64_t (__attribute__((sysv_abi)) *strlen_fn)(const char *);
typedef long     (__attribute__((sysv_abi)) *labs_fn)(long);
typedef int      (__attribute__((sysv_abi)) *memcmp_fn)(const void *,
							const void *, uint64_t);
typedef double   (__attribute__((sysv_abi)) *atan2_fn)(double, double);
typedef double   (__attribute__((sysv_abi)) *ldexp_fn)(double, int);

static unsigned checks, failures;

static void want(int ok, const char *fmt, ...)
{
	va_list ap;
	checks++;
	if (ok)
		return;
	failures++;
	fputs("FAIL: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static void *need(HMODULE dll, const char *name)
{
	void *p = (void *)GetProcAddress(dll, name);
	want(p != NULL, "the DLL does not export %s", name);
	return p;
}

int main(int argc, char **argv)
{
	HMODULE dll;
	strlen_fn f_strlen;
	labs_fn f_labs;
	memcmp_fn f_memcmp;
	atan2_fn f_atan2;
	ldexp_fn f_ldexp;
	static const char probe_str[] = "the crossing holds";
	uint32_t m;
	double d;

	if (argc != 2) {
		fprintf(stderr, "usage: crossing <path-to-elfsysv1.dll>\n");
		return 2;
	}

	dll = LoadLibraryA(argv[1]);
	if (!dll) {
		fprintf(stderr, "FAIL: LoadLibrary(%s): error %lu\n",
			argv[1], (unsigned long)GetLastError());
		return 1;
	}

	f_strlen = (strlen_fn)need(dll, "strlen");
	f_labs   = (labs_fn)need(dll, "labs");
	f_memcmp = (memcmp_fn)need(dll, "memcmp");
	f_atan2  = (atan2_fn)need(dll, "atan2");
	f_ldexp  = (ldexp_fn)need(dll, "ldexp");
	if (failures)
		return 1;

	/* Values across the generic int face. */
	want(f_strlen(probe_str) == sizeof probe_str - 1,
	     "strlen through the face miscounted");
	want(f_labs(-5) == 5, "labs through the face lost its argument");
	want(f_memcmp("abc", "abc", 3) == 0,
	     "memcmp through the face saw equal blocks differ");
	want(f_memcmp("abc", "abd", 3) < 0,
	     "memcmp through the face lost the third argument's ordering");

	/* Values across the typed fp thunks: doubles in both register files. */
	d = f_atan2(0.0, 1.0);
	want(d == 0.0, "atan2(0,1) through the face returned %g", d);
	d = f_atan2(1.0, 1.0);
	want(d > 0.785 && d < 0.786,
	     "atan2(1,1) through the face returned %g", d);
	d = f_ldexp(1.5, 4);
	want(d == 24.0, "ldexp(1.5,4) through the face returned %g", d);

	/* The register check: a System V caller's callee-saved set survives a
	 * call into the DLL through the face, and the control still lights. */
	m = sysv_cross_probe((void *)f_strlen, (uint64_t)(uintptr_t)probe_str);
	want(m == 0, "the crossing into the DLL moved registers, mask 0x%x", m);
	m = sysv_cross_probe((void *)f_labs, 42);
	want(m == 0, "labs through the face moved registers, mask 0x%x", m);
	m = sysv_cross_probe((void *)elfsysv_leaky_sysv, 0);
	want(m == SYSV_FULL_MASK,
	     "the leaky control lit mask 0x%x, not 0x%x", m, SYSV_FULL_MASK);

	FreeLibrary(dll);

	printf("verdict=%s\nchecks=%u\nfailures=%u\n",
	       failures ? "no" : "yes", checks, failures);
	return failures ? 1 : 0;
}
