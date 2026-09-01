/*
 * The reent-face-bringup live run's body -- item 3's terminal witness, in C.
 *
 * Called from the assembly entry (reent-spec.S), it is the reent-consuming
 * body reent-tls-bringup asks for, reached across the loader crossing this
 * time rather than in a hand-built host probe. strtol and __errno are
 * undefined here and satisfied by the image's DT_NEEDED on the WP-53 libc.so.6
 * veneer, so the compiler routes both through this image's PLT. At run time
 * the veneer's own thunk (resolver.c) resolves each into the elfsysv1.dll
 * face the loader mapped and named through AT_BASE -- so the strtol that runs
 * is the face's, writing the reent, and the __errno that reads it hands back
 * the same reent's errno slot. reent-bringup's realproc-probe measured this in
 * the real-process host shape; this measures it across enter.S.
 *
 * The return value is the process exit status (WP-41's byte protocol, via the
 * entry stub). It encodes exactly what held so a fault or a partial cross is
 * legible in the exit code rather than silent:
 *
 *   42  strtol overflowed to LONG_MAX AND errno came back ERANGE -- the witness
 *   10  strtol returned LONG_MAX but errno was not ERANGE (body ran, reent unset)
 *   11  errno was ERANGE but the return was not LONG_MAX (unexpected)
 *   12  neither held (the call crossed but recorded nothing in the reent)
 *
 * A crash before return (a fault reaching the face across the crossing, or the
 * reent read on the ELF-frame stack) leaves the crossing's own exit code, which
 * the harness reports as a status outside {42,10,11,12} -- the honest "faulted".
 */
extern long  strtol(const char *, char **, int);
extern int  *__errno(void);

#define ELFSYSV_ERANGE   34			/* newlib errno.h, as the face uses */
#define ELFSYSV_LONG_MAX 0x7fffffffffffffffL

int reent_body(void)
{
	*__errno() = 0;
	long v = strtol("999999999999999999999999999", (char **)0, 10);
	int e = *__errno();

	int ret_ok   = (v == ELFSYSV_LONG_MAX);
	int errno_ok = (e == ELFSYSV_ERANGE);

	if (ret_ok && errno_ok)  return 42;
	if (ret_ok && !errno_ok) return 10;
	if (!ret_ok && errno_ok) return 11;
	return 12;
}
