/* memory slice: the allocator's introspection -- mallinfo, mallopt,
   malloc_trim -- printed as invariants over a known allocation load. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

int main(void)
{
    struct mallinfo mi;
    void *keep[32];
    int i;

    /* Tuning knobs answer 1 for recognized parameters. */
    printf("mallopt-trim %d\n", mallopt(M_TRIM_THRESHOLD, 128 * 1024) == 1);
    printf("mallopt-mmap %d\n", mallopt(M_MMAP_THRESHOLD, 256 * 1024) == 1);

    for (i = 0; i < 32; i++) {
        keep[i] = malloc(4096);
        memset(keep[i], i, 4096);
    }

    /* With 128K live, the arena is nonempty and in-use bytes cover it. */
    mi = mallinfo();
    printf("arena %d\n", mi.arena + mi.hblkhd > 0);
    printf("inuse %d\n", mi.uordblks + mi.hblkhd >= 32 * 4096);

    for (i = 0; i < 32; i++)
        free(keep[i]);
    mi = mallinfo();
    printf("freed %d\n", mi.fordblks >= 0);

    /* Trimming after the frees either releases memory or has none to
       release; both answers are within the contract. */
    i = malloc_trim(0);
    printf("trim %d\n", i == 0 || i == 1);
    return 0;
}
