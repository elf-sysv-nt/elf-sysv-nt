/* stdlib slice: seeded generators -- rand, rand_r, random with saved
   state, and the *48 family. Every stream is seeded, so both sides of
   the differential draw the same glibc sequences. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    unsigned seed = 12345;
    unsigned short xsub[3] = { 1, 2, 3 };
    unsigned short seed16[3] = { 7, 8, 9 };
    static char state1[128], state2[128];
    char *old;
    int i;

    srand(2026);
    for (i = 0; i < 3; i++) printf("rand%d %d\n", i, rand());
    for (i = 0; i < 3; i++) printf("randr%d %d\n", i, rand_r(&seed));

    initstate(99u, state1, sizeof state1);
    for (i = 0; i < 2; i++) printf("rnd%d %ld\n", i, random());
    old = initstate(7u, state2, sizeof state2);
    printf("swap %d %ld\n", old == state1, random());
    setstate(state1);
    for (i = 0; i < 2; i++) printf("resume%d %ld\n", i, random());
    srandom(99u);
    /* fresh srandom on the current state restarts the stream */
    printf("restart %ld\n", random());

    srand48(4242);
    for (i = 0; i < 2; i++) printf("d48_%d %.6f\n", i, drand48());
    for (i = 0; i < 2; i++) printf("l48_%d %ld\n", i, lrand48());
    printf("m48 %ld\n", mrand48());
    printf("e48 %.6f\n", erand48(xsub));
    printf("n48 %ld\n", nrand48(xsub));
    printf("j48 %ld\n", jrand48(xsub));
    printf("seed48 %d\n", seed48(seed16) != NULL);
    printf("s48 %ld\n", lrand48());
    return 0;
}
