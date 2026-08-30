/* compat_test.c -- WP-25 certification.

Two objects carry version stamps: a "program", stamped at its build time with
the runtime it was built against, and a "runtime", carrying its own. The check
reads the program's stamp against the runtime's at load, exactly as Cygwin reads
a program's per_process api_major/api_minor against cygwin_version. This test
drives that check across the matrix and asserts the verdict, with the two
headline directions -- a lower minor running against a higher runtime, and the
reverse refused with a diagnostic rather than a crash -- checked by name.

The check is plain integer and string comparison, so this runs and prints a real
verdict on the host toolchain; the same compat.c compiles under the cross
toolchain as the runtime code it is, which the driver confirms separately. */

#include <stdio.h>
#include <string.h>

#include "../elfsysv-version.h"
#include "../compat.h"

static elfsysv_version_stamp
stamp (const char *id, unsigned maj, unsigned min)
{
	elfsysv_version_stamp s;
	s.magic = ELFSYSV_VERSION_MAGIC;
	s.api_major = (uint16_t) maj;
	s.api_minor = (uint16_t) min;
	s.dll_identifier = id;
	return s;
}

static int failures;
static int quiet;

/* Assert one verdict. When the verdict is a refusal, also assert a non-empty
   diagnostic was produced -- the done-condition is a diagnostic, not a crash and
   not a silent no. Prints the diagnostic so a reader sees the actual wording. */
static void
expect (const char *what, const elfsysv_version_stamp *prog,
	const elfsysv_version_stamp *run, elfsysv_compat_result want)
{
	char diag[256];
	elfsysv_compat_result got = elfsysv_check_compat (prog, run, diag, sizeof diag);
	int ok = (got == want);

	if (ok && want != ELFSYSV_COMPAT_OK && diag[0] == '\0')
		ok = 0;			/* a refusal must speak */
	if (ok && want == ELFSYSV_COMPAT_OK && diag[0] != '\0')
		ok = 0;			/* an OK must stay silent */

	if (!ok)
		failures++;
	if (!quiet || !ok)
	{
		printf ("  %-42s prog=%s %u.%u  run=%s %u.%u  -> %-18s %s\n",
			what,
			prog->dll_identifier, prog->api_major, prog->api_minor,
			run->dll_identifier, run->api_major, run->api_minor,
			elfsysv_compat_result_name (got), ok ? "OK" : "WRONG");
		if (diag[0] != '\0')
			printf ("      diagnostic: %s\n", diag);
	}
}

int
main (int argc, char **argv)
{
	int terse = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp (argv[i], "--terse") == 0) terse = 1;
		else if (strcmp (argv[i], "--quiet") == 0) quiet = 1;
	}

	/* The two headline directions the milestone names, made concrete with a
	   runtime ahead of one program and behind another. */
	elfsysv_version_stamp run5 = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 0, 5);
	elfsysv_version_stamp prog3 = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 0, 3);
	elfsysv_version_stamp prog7 = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 0, 7);

	printf ("elfsysv1.dll compatibility counter -- runtime api %u.%u, generation %s\n\n",
		elfsysv_runtime_version.api_major, elfsysv_runtime_version.api_minor,
		elfsysv_runtime_version.dll_identifier);

	printf ("the backward-only rule, both directions:\n");
	expect ("lower minor runs on higher runtime", &prog3, &run5, ELFSYSV_COMPAT_OK);
	expect ("higher minor refused on lower runtime", &prog7, &run5, ELFSYSV_COMPAT_REFUSED_NEWER);

	/* The rest of the matrix. */
	elfsysv_version_stamp same = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 0, 5);
	elfsysv_version_stamp maj0 = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 0, 9);
	elfsysv_version_stamp maj1 = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 1, 0);
	elfsysv_version_stamp gen2 = stamp ("elfsysv2", 0, 3);
	elfsysv_version_stamp bad;

	printf ("\nthe rest of the matrix:\n");
	expect ("equal versions run", &same, &run5, ELFSYSV_COMPAT_OK);
	expect ("lower major runs on higher major", &maj0, &maj1, ELFSYSV_COMPAT_OK);
	expect ("higher major refused (combined)", &maj1, &maj0, ELFSYSV_COMPAT_REFUSED_NEWER);
	expect ("different generation refused", &gen2, &run5, ELFSYSV_COMPAT_REFUSED_GENERATION);

	/* The struct-size backup, Cygwin's magic_biscuit: a stamp built against a
	   structurally different definition is caught before any field is read. */
	bad = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 0, 3);
	bad.magic = ELFSYSV_VERSION_MAGIC + 8;
	expect ("wrong stamp size refused", &bad, &run5, ELFSYSV_COMPAT_REFUSED_MAGIC);

	/* And the real runtime stamp as the right-hand side, so the shipped value
	   is exercised and not only synthetic ones: a program built against the
	   very first release runs, one built a minor ahead of the shipped runtime
	   does not. */
	elfsysv_version_stamp prog_first = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER, 0, 1);
	elfsysv_version_stamp prog_ahead = stamp (ELFSYSV_VERSION_DLL_IDENTIFIER,
						  elfsysv_runtime_version.api_major,
						  elfsysv_runtime_version.api_minor + 1);
	printf ("\nagainst the shipped runtime stamp:\n");
	expect ("first-release program runs", &prog_first, &elfsysv_runtime_version, ELFSYSV_COMPAT_OK);
	expect ("one minor ahead refused", &prog_ahead, &elfsysv_runtime_version, ELFSYSV_COMPAT_REFUSED_NEWER);

	if (terse)
		printf ("\n%%%%%% wp25 compat: %d cases, %d failures\n", 11, failures);
	printf ("\n%s: %d failures\n", failures ? "FAIL" : "PASS", failures);
	return failures ? 1 : 0;
}
