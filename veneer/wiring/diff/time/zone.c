/* time slice: tzset over explicit POSIX zone strings -- no zone files
   consulted, so both sides read the same rules -- localtime and
   mktime observing the offset and the DST split. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void show(const char *tag, const struct tm *tm)
{
    printf("%s %d-%02d-%02d %02d:%02d dst=%d\n",
           tag, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_isdst);
}

int main(void)
{
    time_t t;
    struct tm tm;

    setenv("TZ", "UTC", 1);
    tzset();
    printf("utc %s %s %ld %d\n", tzname[0], tzname[1],
           (long)timezone, daylight);

    /* A fixed five-hour zone, no DST arm. */
    setenv("TZ", "EST5", 1);
    tzset();
    printf("est %s %ld %d\n", tzname[0], (long)timezone, daylight);
    t = 951782400; /* 2000-02-29 00:00 UTC */
    localtime_r(&t, &tm);
    show("est-leap", &tm);

    /* The rule-carrying form: DST switches at explicit dates. */
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
    printf("edt %s %s %ld %d\n", tzname[0], tzname[1],
           (long)timezone, daylight);
    t = 946684800; /* 2000-01-01 00:00 UTC: winter, standard time */
    localtime_r(&t, &tm);
    show("winter", &tm);
    t = 962409600; /* 2000-07-01 00:00 UTC: summer, daylight time */
    localtime_r(&t, &tm);
    show("summer", &tm);

    /* mktime under the zone: local midnight is five hours later in
       winter, four in summer. */
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100; tm.tm_mon = 0; tm.tm_mday = 1; tm.tm_isdst = -1;
    printf("mk-winter %ld\n", (long)mktime(&tm));
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 100; tm.tm_mon = 6; tm.tm_mday = 1; tm.tm_isdst = -1;
    printf("mk-summer %ld\n", (long)mktime(&tm));
    return 0;
}
