/* compat.c -- the load-time compatibility check for elfsysv1.dll (WP-25).

The check is integer and string comparison over two version stamps; it holds no
ABI assumptions and links into the runtime unchanged whichever toolchain builds
it. It is the re-faced form of Cygwin's check_sanity_and_sync at newlib-cygwin
b11613e47, dcrt0.cc, keeping that function's order -- the struct-size backup
first, then the version comparison -- and its direction. DR-0018 records why. */

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "elfsysv-version.h"
#include "compat.h"

/* The running runtime's own stamp, filled from elfsysv-version.h. This is
   Cygwin's cygwin_version: the fixed right-hand side a program's stamp is
   measured against. */
const elfsysv_version_stamp elfsysv_runtime_version = ELFSYSV_VERSION_STAMP_INIT;

const char *
elfsysv_compat_result_name (elfsysv_compat_result r)
{
	switch (r)
	{
	case ELFSYSV_COMPAT_OK:			return "ok";
	case ELFSYSV_COMPAT_REFUSED_NEWER:	return "refused-newer";
	case ELFSYSV_COMPAT_REFUSED_GENERATION:	return "refused-generation";
	case ELFSYSV_COMPAT_REFUSED_MAGIC:	return "refused-magic";
	}
	return "refused-unknown";
}

/* Write a diagnostic if there is somewhere to write one. Kept in one place so
   every refusal names both versions the same way. */
static void
diag_write (char *diag, size_t diaglen, const char *fmt,
	    const elfsysv_version_stamp *program,
	    const elfsysv_version_stamp *runtime)
{
	if (diag == NULL || diaglen == 0)
		return;
	snprintf (diag, diaglen, fmt,
		  program->dll_identifier, program->api_major, program->api_minor,
		  runtime->dll_identifier, runtime->api_major, runtime->api_minor);
}

elfsysv_compat_result
elfsysv_check_compat (const elfsysv_version_stamp *program,
		      const elfsysv_version_stamp *runtime,
		      char *diag, size_t diaglen)
{
	unsigned prog_combined, run_combined;

	if (diag != NULL && diaglen > 0)
		diag[0] = '\0';

	/* The backup check, first, as Cygwin runs it. A disagreement on the size
	   of the stamp itself means the two sides were built against
	   structurally different definitions of it, and no field below can be
	   trusted to mean what it says. */
	if (program->magic != runtime->magic)
	{
		if (diag != NULL && diaglen > 0)
			snprintf (diag, diaglen,
				  "elfsysv: incompatible program -- version stamp size %u does not match the runtime's %u",
				  (unsigned) program->magic, (unsigned) runtime->magic);
		return ELFSYSV_COMPAT_REFUSED_MAGIC;
	}

	/* The generation, the digit in the name. A different identifier is a
	   different runtime the program never meant to load; the counter does not
	   reach across it, so this is refused before the counter is consulted. In
	   the field the mismatched name would already have kept the two apart at
	   resolution, the way cygwin1.dll and a hypothetical cygwin2.dll do not
	   answer for each other; the check is the last line rather than the
	   first. */
	if (program->dll_identifier == NULL || runtime->dll_identifier == NULL
	    || strcmp (program->dll_identifier, runtime->dll_identifier) != 0)
	{
		diag_write (diag, diaglen,
			    "elfsysv: wrong runtime generation -- program built against %s (api %u.%u), runtime is %s (api %u.%u)",
			    program, runtime);
		return ELFSYSV_COMPAT_REFUSED_GENERATION;
	}

	/* The counter, backward only. A program built against a lower or equal
	   combined API runs; one built against a higher combined API expects
	   entry points this runtime does not carry, and is refused. This is the
	   whole of Cygwin's rule -- a newer program does not run on an older
	   runtime -- read on the combined major.minor rather than on major alone,
	   because every additive change bumps the minor and a program built after
	   one needs a runtime that has it. */
	prog_combined = ELFSYSV_VERSION_MAKE_COMBINED (program->api_major,
						       program->api_minor);
	run_combined = ELFSYSV_VERSION_MAKE_COMBINED (runtime->api_major,
						      runtime->api_minor);
	if (prog_combined > run_combined)
	{
		diag_write (diag, diaglen,
			    "elfsysv: program and runtime out of sync -- program built against %s api %u.%u, runtime provides only %s api %u.%u",
			    program, runtime);
		return ELFSYSV_COMPAT_REFUSED_NEWER;
	}

	return ELFSYSV_COMPAT_OK;
}
