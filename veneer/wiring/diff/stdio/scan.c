/* stdio slice: formatted input, the scanf family over strings. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>

static int vwrap(const char *s, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vsscanf(s, f, ap);
    va_end(ap);
    return r;
}

int main(void)
{
    int a = 0, b = 0, n = 0, r;
    unsigned u = 0;
    double d = 0;
    char w[32], set[32];

    r = sscanf("10 20", "%d %d", &a, &b);
    printf("sscanf %d %d %d\n", r, a, b);
    r = sscanf("ff 10", "%x %o", &u, &a);
    printf("hexoct %d %u %d\n", r, u, a);
    r = sscanf("2.75x", "%lf", &d);
    printf("float %d %.2f\n", r, d);
    r = sscanf("  lead trail", "%31s", w);
    printf("word %d %s\n", r, w);
    r = sscanf("abba9z", "%31[ab]", set);
    printf("set %d %s\n", r, set);
    r = sscanf("12,34", "%d,%d%n", &a, &b, &n);
    printf("count %d %d\n", r, n);
    printf("miss %d\n", sscanf("xyz", "%d", &a));
    printf("eofret %d\n", sscanf("", "%d", &a));
    r = vwrap("7:8", "%d:%d", &a, &b);
    printf("vsscanf %d %d %d\n", r, a, b);
    return 0;
}
