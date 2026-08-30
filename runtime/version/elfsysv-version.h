/* elfsysv-version.h -- the compatibility counter for elfsysv1.dll (WP-25).

This is elfsysv1.dll's answer to the file Cygwin keeps as
winsup/cygwin/include/cygwin/version.h. It carries the runtime's own API
major and minor, the identifier whose trailing digit names the runtime
generation, and the stamp a program built against this runtime carries so the
runtime can read at load what it was built against. The enforcement that reads
it lives in compat.h and compat.c.

The discipline is inherited rather than invented; DR-0000 fixes the floor as
Cygwin re-faced, DR-0007 hands the versioning of that surface to this package,
and DR-0018 records the mechanism this header realises. Cygwin's own values are
CYGWIN_VERSION_API_MAJOR/MINOR and CYGWIN_VERSION_DLL_IDENTIFIER at newlib-cygwin
b11613e47. */

#ifndef ELFSYSV_VERSION_H
#define ELFSYSV_VERSION_H

#include <stdint.h>

/* The identifier whose trailing digit names the runtime generation. It is the
   load-bearing part of the name elfsysv1.dll: a program resolves the runtime by
   this name, so a generation break is a different DLL a program never loads by
   accident, exactly as cygwin1.dll's "cygwin1" works. The digit changes only on
   a break that no compatibility counter can bridge; everything the counter can
   bridge stays within one generation. */
#define ELFSYSV_VERSION_DLL_IDENTIFIER "elfsysv1"

/* The API major and minor counters. Major is the axis reserved for an
   incompatible change within a generation; minor is bumped, additively, for
   every API addition. The pair starts at the first release rather than at the
   first break -- retrofitting a counter later means guessing which shipped
   binaries predate which change, and there is no honest way to guess it. First
   release is 0.1; its meaning and the rule for bumping either counter are in
   CHANGELOG.md. */
#define ELFSYSV_VERSION_API_MAJOR 0
#define ELFSYSV_VERSION_API_MINOR 1

/* Fold the pair into one comparable number. Cygwin multiplies major by 1000 and
   adds minor, and warns against writing a minor as a zero-padded literal that C
   would read as octal; the same caution applies here. A minor may run past 999
   only if a generation ever needs a thousand additive changes, which would be a
   sign the major axis was never used. */
#define ELFSYSV_VERSION_MAKE_COMBINED(maj, min) (((maj) * 1000) + (min))
#define ELFSYSV_VERSION_API_COMBINED \
	ELFSYSV_VERSION_MAKE_COMBINED (ELFSYSV_VERSION_API_MAJOR, \
				      ELFSYSV_VERSION_API_MINOR)

/* The stamp a versioned object carries. Both a program and the runtime are
   objects that carry one: the program's is emitted by its startup code at build
   time and records the runtime it was built against; the runtime's is
   elfsysv_runtime_version below, filled from the macros in this header. The
   runtime reads the program's stamp at load and compares, the way Cygwin reads
   a program's per_process (user_data) api_major/api_minor against its own
   cygwin_version.

   magic is the size of this struct, and doubles as Cygwin's magic_biscuit does:
   a backup compatibility check that fails loudly if the two sides were built
   against structurally different definitions of the stamp itself. */
typedef struct elfsysv_version_stamp
{
	uint32_t    magic;		/* = ELFSYSV_VERSION_MAGIC */
	uint16_t    api_major;		/* API major built against */
	uint16_t    api_minor;		/* API minor built against */
	const char *dll_identifier;	/* runtime generation, e.g. "elfsysv1" */
} elfsysv_version_stamp;

#define ELFSYSV_VERSION_MAGIC ((uint32_t) sizeof (elfsysv_version_stamp))

/* An initialiser a program's startup code uses to stamp itself with the runtime
   it was built against; expands to the values baked into this header at the
   program's compile time. The runtime's own stamp uses the same values through
   elfsysv_runtime_version. */
#define ELFSYSV_VERSION_STAMP_INIT \
	{ ELFSYSV_VERSION_MAGIC, \
	  ELFSYSV_VERSION_API_MAJOR, \
	  ELFSYSV_VERSION_API_MINOR, \
	  ELFSYSV_VERSION_DLL_IDENTIFIER }

/* The running runtime's own stamp, the right-hand side of every check. Defined
   in compat.c. */
extern const elfsysv_version_stamp elfsysv_runtime_version;

#endif /* ELFSYSV_VERSION_H */
