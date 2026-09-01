/* runtime slice: the jmp_buf shim's buffer-lifecycle contract
   (DR-0051). El8's jmp_buf is opaque to every conforming caller --
   POSIX only requires it round-trip through the same implementation's
   own setjmp/longjmp -- so this case exercises two shapes the wiring
   layer's frameless face adds and el8's own contract does not need to
   think about: a fresh, explicitly zeroed jmp_buf's first use (the
   shim's lazy-allocation path, taken when the stashed word reads
   zero) and one jmp_buf reused for many setjmp/longjmp round trips in
   a row (the common `while (setjmp(buf)) ...` idiom, which must keep
   its one real buffer for the whole loop rather than allocating fresh
   on every pass). Neither shape is directly observable -- a
   conforming program cannot see whether a real buffer was allocated
   once, many times, or not at all -- so both are exercised through
   the ordinary return-value protocol run enough times that a shim
   which mis-tracked the stash (lost it, reallocated over it, aliased
   two buffers together) would eventually deliver a wrong value or
   crash rather than silently pass. */
#include <setjmp.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    jmp_buf fresh;
    jmp_buf reused;
    volatile int r;
    volatile int i;
    volatile int total;

    /* lazy allocation: an explicitly zeroed jmp_buf's first ever
       use -- the shim's stashed-pointer check must treat this as
       "not yet real" and allocate, not follow a garbage pointer. */
    memset(&fresh, 0, sizeof fresh);
    r = setjmp(fresh);
    if (r == 0)
        longjmp(fresh, 11);
    printf("fresh buffer: setjmp returned %d\n", r);

    /* the same buffer, set up and jumped through a second time: the
       stashed real buffer from the first use must still be there and
       still work, not be re-treated as fresh. */
    r = setjmp(fresh);
    if (r == 0)
        longjmp(fresh, 22);
    printf("fresh buffer reused once more: returned %d\n", r);

    /* the while(setjmp(buf)) idiom: one buffer, two hundred round
       trips, summing the value each pass delivers -- the shape that
       must keep exactly one real buffer alive for the loop's whole
       lifetime rather than allocating a new one, or losing track of
       the old one, on any later pass. */
    memset(&reused, 0, sizeof reused);
    total = 0;
    i = 0;
    while ((i = setjmp(reused)) < 200) {
        total = total + i;
        longjmp(reused, i + 1);
    }
    printf("reused-buffer loop: last value %d, sum %d\n", i, total);

    return 0;
}
