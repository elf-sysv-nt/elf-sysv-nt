/* stdio slice: formatted output, the printf family. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static int wrap(char *buf, size_t n, const char *f, ...)
{
    va_list ap;
    int r;
    va_start(ap, f);
    r = vsnprintf(buf, n, f, ap);
    va_end(ap);
    return r;
}

int main(void)
{
    char buf[64];
    char *p = NULL;
    int r;
    volatile size_t clip = 4;   /* defeats -Wformat-truncation; the
                                   truncation is the behaviour under test */

    printf("printf %d %u %x %o %c %s\n", -42, 42u, 0xbeef, 0755, 'q', "str");
    printf("width [%5d] [%-5d] [%05d] [%+d] [% d]\n", 7, 7, 7, 7, 7);
    printf("prec [%.3s] [%8.2f] [%.0f] [%e] [%g]\n",
           "abcdef", 3.14159, 2.5, 12345.678, 0.0001);
    printf("long %ld %lld %zu\n", 2147483648L, -9007199254740993LL,
           (size_t)7);

    r = snprintf(buf, clip, "%d", 123456);
    printf("snprintf %d %s\n", r, buf);
    printf("snprintf0 %d\n", snprintf(NULL, 0, "%s!", "measure"));
    r = wrap(buf, sizeof buf, "%x/%x", 10, 11);
    printf("vsnprintf %d %s\n", r, buf);

    r = asprintf(&p, "[%d %s]", 5, "five");
    printf("asprintf %d %s\n", r, p);
    free(p);
    fflush(stdout);
    dprintf(1, "dprintf %d\n", 99);

    printf("sprintf %d\n", sprintf(buf, "%d%d", 12, 34));
    fprintf(stdout, "fprintf %s\n", buf);
    return 0;
}
