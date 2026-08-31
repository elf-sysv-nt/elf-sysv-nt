/* stdlib slice: sorting and arithmetic -- qsort, qsort_r, bsearch, and
   the abs/div families. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static int cmp(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static int cmp_r(const void *a, const void *b, void *dir)
{
    int d = *(int *)dir;
    return d * (*(const int *)a - *(const int *)b);
}

int main(void)
{
    int v[] = { 9, 1, 8, 2, 7, 3, 6, 4, 5 };
    int key = 7, down = -1, i;
    int *hit;
    div_t q;
    ldiv_t lq;
    lldiv_t llq;
    imaxdiv_t iq;

    qsort(v, 9, sizeof v[0], cmp);
    for (i = 0; i < 9; i++) printf("s%d %d\n", i, v[i]);

    hit = bsearch(&key, v, 9, sizeof v[0], cmp);
    printf("found %d\n", hit != NULL && *hit == 7);
    key = 42;
    printf("missing %d\n", bsearch(&key, v, 9, sizeof v[0], cmp) == NULL);

    qsort_r(v, 9, sizeof v[0], cmp_r, &down);
    printf("desc %d %d\n", v[0], v[8]);

    printf("abs %d %ld %lld %jd\n",
           abs(-5), labs(-6L), llabs(-7LL), imaxabs((intmax_t)-8));
    q = div(17, 5);
    lq = ldiv(-17L, 5L);
    llq = lldiv(17LL, -5LL);
    iq = imaxdiv((intmax_t)-17, (intmax_t)-5);
    printf("div %d %d\n", q.quot, q.rem);
    printf("ldiv %ld %ld\n", lq.quot, lq.rem);
    printf("lldiv %lld %lld\n", llq.quot, llq.rem);
    printf("imaxdiv %jd %jd\n", iq.quot, iq.rem);
    return 0;
}
