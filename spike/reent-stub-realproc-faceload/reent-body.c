/*
 * The reent-face-bringup live run's body -- item 3's terminal witness, in C.
 *
 * Called from the assembly entry (reent-spec.S), it is the reent-consuming
 * body reent-tls-bringup asks for, reached across the loader crossing this
 * time rather than in a hand-built host probe. strtol is undefined here and
 * satisfied by the image's DT_NEEDED on the WP-53 libc.so.6 veneer, so the
 * compiler routes it through this image's PLT. At run time the veneer's own
 * thunk (resolver.c) resolves strtol into the elfsysv1.dll face the loader
 * mapped and named through AT_BASE -- so the strtol that runs is the face's,
 * the one that consults and writes the reent. reent-bringup's realproc-probe
 * measured this in the real-process host shape; this measures it across enter.S.
 *
 * WHY THE WITNESS IS THE RETURN, NOT errno READ BACK.  The full witness would
 * also read errno back and check ERANGE. But errno's ELF accessors are not
 * forwards: veneer/classification/classification.tsv classes __errno_location
 * (and the errno@@GLIBC_PRIVATE carrier) as `shim` -- "errno numeric values
 * differ" (DR-0000), a value-translation shim over cygwin __errno -- and the
 * veneer emits runtime-resolving thunks only for forward-same/forward-alias
 * FUNC rows, not shims. So the built veneer exports the errno TLS carrier but
 * no __errno/__errno_location thunk to read it through, and a body that read
 * errno back across the veneer does not link (measured: it fails on __errno).
 * strtol itself is a forward and does resolve; strtol on an overflow returns
 * LONG_MAX only by running the face's real body, which is the reent-consuming
 * body executing live across the crossing. That is the reachable witness now;
 * the errno read-back waits on the errno shim, which measure.sh records as the
 * one remaining step rather than this spike asserting it.
 *
 * The return value is the process exit status (WP-41's byte protocol, via the
 * entry stub):
 *
 *   42  strtol overflowed to LONG_MAX -- the reent-consuming forward body ran
 *       across the crossing and returned the value only its real body returns
 *   12  strtol returned but not LONG_MAX (crossed, but did not run the body)
 *
 * A crash before return leaves the crossing's own exit code, which the harness
 * reports as a status outside {42,12} -- the honest "faulted".
 */
extern long strtol(const char *, char **, int);

#define ELFSYSV_LONG_MAX 0x7fffffffffffffffL

int reent_body(void)
{
	long v = strtol("999999999999999999999999999", (char **)0, 10);

	if (v == ELFSYSV_LONG_MAX) return 42;
	return 12;
}
