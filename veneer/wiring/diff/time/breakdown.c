/* time slice: the broken-down forms -- gmtime, localtime, mktime,
   timegm, timelocal, difftime, asctime, ctime and the _r twins -- over
   fixed epochs under TZ=UTC, so every line is a constant. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void show(const char *tag, const struct tm *tm)
{
    printf("%s %d-%02d-%02d %02d:%02d:%02d wday=%d yday=%d dst=%d\n",
           tag, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec,
           tm->tm_wday, tm->tm_yday, tm->tm_isdst);
}

int main(void)
{
    time_t t;
    struct tm tm, *p;
    char buf[64];

    setenv("TZ", "UTC", 1);
    tzset();

    t = 0;
    p = gmtime(&t);
    show("epoch", p);

    t = 951782400; /* 2000-02-29 00:00:00 UTC, the century leap day */
    p = gmtime_r(&t, &tm);
    show("leap", p);

    p = localtime(&t);
    show("leap-local", p);
    p = localtime_r(&t, &tm);
    show("leap-local-r", p);

    /* mktime and timegm agree with the breakdown under TZ=UTC, and
       mktime normalizes an overflowing field. */
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100; tm.tm_mon = 1; tm.tm_mday = 29; tm.tm_isdst = 0;
    printf("mktime %ld\n", (long)mktime(&tm));
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100; tm.tm_mon = 1; tm.tm_mday = 29;
    printf("timegm %ld\n", (long)timegm(&tm));
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100; tm.tm_mon = 1; tm.tm_mday = 29; tm.tm_isdst = 0;
    printf("timelocal %ld\n", (long)timelocal(&tm));
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100; tm.tm_mon = 0; tm.tm_mday = 60; tm.tm_isdst = 0;
    printf("normalized %ld\n", (long)mktime(&tm));
    printf("carried %d %d\n", tm.tm_mon, tm.tm_mday);

    printf("difftime %.0f\n", difftime(951782400, 0));
    printf("difftime-neg %.0f\n", difftime(0, 86400));

    t = 86399; /* 1970-01-01 23:59:59 UTC, a Thursday */
    p = gmtime(&t);
    strcpy(buf, asctime(p));
    printf("asctime %s", buf);
    printf("asctime-r %d\n", asctime_r(p, buf) == buf);
    printf("asctime-r-text %s", buf);
    strcpy(buf, ctime(&t));
    printf("ctime %s", buf);
    printf("ctime-r %d\n", ctime_r(&t, buf) == buf);
    return 0;
}
