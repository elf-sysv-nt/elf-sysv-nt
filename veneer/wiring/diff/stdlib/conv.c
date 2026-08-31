/* stdlib slice: numeric conversions -- the strto* family with endptr
   and ERANGE, the ato* shortcuts, and the base-64 pair. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>

int main(void)
{
    char *end;
    long l;
    unsigned long ul;
    long long ll;
    double d;

    l = strtol("  -42abc", &end, 10);
    printf("strtol %ld rest=%s\n", l, end);
    l = strtol("0x1f", &end, 0);
    printf("strtol0 %ld rest=%s\n", l, end);
    l = strtol("ff", &end, 16);
    printf("strtol16 %ld\n", l);

    errno = 0;
    l = strtol("999999999999999999999999", &end, 10);
    printf("range %d %d\n", l == LONG_MAX, errno == ERANGE);

    ul = strtoul("-1", &end, 10);
    printf("strtoul %d\n", ul == ULONG_MAX);
    ll = strtoll("-9223372036854775808", &end, 10);
    printf("strtoll %lld\n", ll);
    printf("strtoimax %jd\n", strtoimax("123456789012345", &end, 10));
    printf("strtoumax %ju\n", strtoumax("18446744073709551615", &end, 10));

    d = strtod("3.5e2xyz", &end);
    printf("strtod %.1f rest=%s\n", d, end);
    d = strtod("0x1p4", &end);
    printf("strtodhex %.1f\n", d);
    printf("strtof %.2f\n", (double)strtof("2.25", NULL));
    printf("strtold %.3Lf\n", strtold("1.125", NULL));

    printf("atoi %d atol %ld atoll %lld atof %.1f\n",
           atoi("77x"), atol("-88"), atoll("99"), atof("6.5"));

    l = a64l("zzzz");
    printf("a64l %ld back=%s\n", l, l64a(l));
    return 0;
}
