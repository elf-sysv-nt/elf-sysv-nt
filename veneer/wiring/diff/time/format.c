/* time slice: strftime and strftime_l over one fixed breakdown in the
   C locale -- the conversion set, the week numberings, and the
   too-small buffer refusing with 0. */
#define _GNU_SOURCE
#include <stdio.h>
#include <locale.h>
#include <string.h>
#include <time.h>

int main(void)
{
    struct tm tm;
    char buf[128];
    size_t n;
    locale_t loc;

    memset(&tm, 0, sizeof tm);
    /* 2000-02-29 13:05:07, the Tuesday of the century leap day */
    tm.tm_year = 100; tm.tm_mon = 1; tm.tm_mday = 29;
    tm.tm_hour = 13; tm.tm_min = 5; tm.tm_sec = 7;
    tm.tm_wday = 2; tm.tm_yday = 59;

    n = strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    printf("iso %zu %s\n", n, buf);
    strftime(buf, sizeof buf, "%a %A %b %B", &tm);
    printf("names %s\n", buf);
    strftime(buf, sizeof buf, "%C %y %j %e %I %p %R %T", &tm);
    printf("mixed %s\n", buf);
    strftime(buf, sizeof buf, "%U %W %G %V %u %w", &tm);
    printf("weeks %s\n", buf);
    strftime(buf, sizeof buf, "%c", &tm);
    printf("locale-c %s\n", buf);
    strftime(buf, sizeof buf, "%x|%X", &tm);
    printf("dt %s\n", buf);
    strftime(buf, sizeof buf, "%%|%n|%t|", &tm);
    printf("literals %s\n", buf);

    /* A buffer too small for the expansion refuses with 0. */
    n = strftime(buf, 4, "%Y-%m-%d", &tm);
    printf("small %zu\n", n);

    loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    printf("newlocale %d\n", loc != (locale_t)0);
    n = strftime_l(buf, sizeof buf, "%Y %b %a", &tm, loc);
    printf("strftime-l %zu %s\n", n, buf);
    freelocale(loc);
    return 0;
}
