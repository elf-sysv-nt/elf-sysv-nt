/* memory slice: the allocator's contract -- malloc, calloc, realloc,
   reallocarray, free -- printed as invariants, never as addresses. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

int main(void)
{
    unsigned char *p, *q;
    size_t i;

    p = malloc(1000);
    printf("malloc %d\n", p != NULL);
    memset(p, 0xa5, 1000);
    printf("fill %d\n", p[0] == 0xa5 && p[999] == 0xa5);

    q = calloc(250, 4);
    printf("calloc %d\n", q != NULL);
    for (i = 0; i < 1000 && q[i] == 0; i++)
        ;
    printf("zeroed %d\n", i == 1000);
    free(q);

    p = realloc(p, 4000);
    printf("grow %d\n", p != NULL && p[0] == 0xa5 && p[999] == 0xa5);
    p = realloc(p, 8);
    printf("shrink %d\n", p != NULL && p[0] == 0xa5 && p[7] == 0xa5);
    free(p);

    /* reallocarray refuses the multiplication that overflows. */
    errno = 0;
    p = reallocarray(NULL, (size_t)-1 / 2, 4);
    printf("overflow %d %d\n", p == NULL, errno == ENOMEM);
    p = reallocarray(NULL, 16, 16);
    printf("reallocarray %d\n", p != NULL);
    p = reallocarray(p, 32, 32);
    printf("regrown %d\n", p != NULL);
    free(p);

    /* free(NULL) is a no-op and the program survives to say so. */
    free(NULL);
    printf("freenull ok\n");
    return 0;
}
