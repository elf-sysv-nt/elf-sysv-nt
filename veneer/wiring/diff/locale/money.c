/* locale slice: strfmon in the C locale -- national and international
   forms, width and precision control, the literal percent, and the
   overflow refusal, each formatted into a fresh buffer before its
   printf. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <monetary.h>

int main(void)
{
    char buf[64];
    locale_t c;
    ssize_t n;

    /* the C locale has no monetary conventions, so %n and %i fall
       back to a plain number with a space-separated blank symbol */
    n = strfmon(buf, sizeof buf, "%n", 1234.567);
    printf("n %zd [%s]\n", n, buf);
    n = strfmon(buf, sizeof buf, "%i", 1234.567);
    printf("i %zd [%s]\n", n, buf);

    /* explicit precision and width are honoured regardless */
    n = strfmon(buf, sizeof buf, "%.2n", 1234.567);
    printf("prec [%s]\n", buf);
    n = strfmon(buf, sizeof buf, "%#5.0n", 42.0);
    printf("width [%s]\n", buf);
    n = strfmon(buf, sizeof buf, "%.0n|%.1n", 7.0, -7.05);
    printf("two [%s]\n", buf);

    /* a literal percent survives */
    n = strfmon(buf, sizeof buf, "x%%y");
    printf("pct %zd [%s]\n", n, buf);

    /* a buffer too small is refused with E2BIG */
    errno = 0;
    n = strfmon(buf, (size_t)4, "%.2n", 123456.78);
    printf("small %d %d\n", n == -1, errno == E2BIG);

    /* the _l twin through a fresh C object agrees */
    c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    n = strfmon_l(buf, sizeof buf, c, "%.2n", 1234.567);
    printf("l [%s]\n", buf);
    freelocale(c);
    return 0;
}
