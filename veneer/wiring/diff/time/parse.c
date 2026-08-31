/* time slice: strptime and strptime_l -- full and partial parses, the
   consumed tail, the refusal on a mismatch, and case-blind month
   names, all in the C locale. */
#define _GNU_SOURCE
#include <stdio.h>
#include <locale.h>
#include <string.h>
#include <time.h>

static void show(const char *tag, const struct tm *tm)
{
    printf("%s %d-%02d-%02d %02d:%02d:%02d\n",
           tag, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int main(void)
{
    struct tm tm;
    char *end;
    locale_t loc;

    memset(&tm, 0, sizeof tm);
    end = strptime("2000-02-29 13:05:07", "%Y-%m-%d %H:%M:%S", &tm);
    printf("full %d %d\n", end != NULL, end && *end == '\0');
    show("full-tm", &tm);

    /* The return points at the unconsumed tail. */
    memset(&tm, 0, sizeof tm);
    end = strptime("29/Feb/2000 rest", "%d/%b/%Y", &tm);
    printf("tail %d %s\n", end != NULL, end ? end : "(null)");
    show("tail-tm", &tm);

    /* Month names parse case-blind. */
    memset(&tm, 0, sizeof tm);
    end = strptime("29/FEB/2000", "%d/%b/%Y", &tm);
    printf("blind %d %d\n", end != NULL, tm.tm_mon);

    /* A mismatch refuses with NULL. */
    memset(&tm, 0, sizeof tm);
    end = strptime("2000-13-40", "%Y-%m-%d", &tm);
    printf("bad-month %d\n", end == NULL);
    end = strptime("not a date", "%Y-%m-%d", &tm);
    printf("garbage %d\n", end == NULL);

    /* %t eats runs of whitespace; %% matches a literal percent. */
    memset(&tm, 0, sizeof tm);
    end = strptime("7 %  1999", "%d %% %Y", &tm);
    printf("literal %d %d %d\n", end != NULL, tm.tm_mday,
           tm.tm_year + 1900);

    loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    printf("newlocale %d\n", loc != (locale_t)0);
    memset(&tm, 0, sizeof tm);
    end = strptime_l("Feb 29 2000", "%b %d %Y", &tm, loc);
    printf("strptime-l %d\n", end != NULL);
    show("strptime-l-tm", &tm);
    freelocale(loc);
    return 0;
}
