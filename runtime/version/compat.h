/* compat.h -- the load-time compatibility check for elfsysv1.dll (WP-25).

The runtime reads a program's version stamp at load and decides whether to run
it. The rule is Cygwin's, inherited down to the direction: a program built
against a lower API runs against a higher runtime, and a program built against a
higher API than the runtime provides is refused -- with a diagnostic naming both
versions, not a crash. Cygwin does this in check_sanity_and_sync (dcrt0.cc); the
mechanism is recorded in DR-0018. */

#ifndef ELFSYSV_COMPAT_H
#define ELFSYSV_COMPAT_H

#include <stddef.h>
#include "elfsysv-version.h"

/* The verdict. OK means run; every other value means refuse, and names why so a
   caller can phrase its own message if it wants one beyond the diagnostic the
   check already fills. */
typedef enum elfsysv_compat_result
{
	ELFSYSV_COMPAT_OK = 0,		 /* program runs against this runtime */
	ELFSYSV_COMPAT_REFUSED_NEWER,	 /* built against a newer API than provided */
	ELFSYSV_COMPAT_REFUSED_GENERATION, /* built against a different name digit */
	ELFSYSV_COMPAT_REFUSED_MAGIC	 /* stamp struct size disagreement */
} elfsysv_compat_result;

/* Decide whether a program may run against a runtime. program is the stamp the
   program carries; runtime is the runtime's own stamp, ordinarily
   &elfsysv_runtime_version. On a refusal, and only then, a human-readable line
   naming both versions is written into diag (truncated to diaglen, always NUL
   terminated when diaglen > 0); on OK diag is left as an empty string. Passing
   diag == NULL or diaglen == 0 suppresses the message and returns the verdict
   alone. The function reads its arguments and returns; it never aborts, so the
   caller owns what a refusal does. */
elfsysv_compat_result elfsysv_check_compat (const elfsysv_version_stamp *program,
					    const elfsysv_version_stamp *runtime,
					    char *diag, size_t diaglen);

/* The verdict's name, for logs and the test's summary. Never NULL. */
const char *elfsysv_compat_result_name (elfsysv_compat_result r);

#endif /* ELFSYSV_COMPAT_H */
