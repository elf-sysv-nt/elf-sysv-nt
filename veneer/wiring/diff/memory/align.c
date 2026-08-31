/* memory slice: the aligned allocators -- memalign and valloc -- and
   malloc_usable_size, printed as alignment and capacity invariants. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

int main(void)
{
    long page = sysconf(_SC_PAGESIZE);
    void *p;
    size_t u;

    printf("page %d\n", page >= 4096);

    p = memalign(64, 100);
    printf("memalign %d %d\n", p != NULL, (uintptr_t)p % 64 == 0);
    memset(p, 1, 100);
    free(p);

    p = memalign(4096, 10);
    printf("memalign4k %d %d\n", p != NULL, (uintptr_t)p % 4096 == 0);
    free(p);

    p = valloc(100);
    printf("valloc %d %d\n", p != NULL, (uintptr_t)p % (size_t)page == 0);
    free(p);

    p = malloc(100);
    u = malloc_usable_size(p);
    printf("usable %d\n", u >= 100);
    memset(p, 2, u);
    printf("usable-writable ok\n");
    free(p);

    printf("usable-null %d\n", malloc_usable_size(NULL) == 0);
    return 0;
}
