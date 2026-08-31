/* wchar slice: the wide-to-number converters and the collation and
   width answers — wcstol and kin with the end pointer contract and
   ERANGE at the rim, wcstod's fractions, wcsftime formatting a fixed
   UTC moment, wcwidth/wcswidth on ASCII, and the C-locale collators
   and case-insensitive comparers agreeing with the obvious. */
#define _GNU_SOURCE
#include <wchar.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

int main(void)
{
    wchar_t *end;
    wchar_t buf[64];
    struct tm tm = { 0 };
    long l;
    double d;

    l = wcstol(L"  1234rest", &end, 10);
    printf("wcstol %ld tail %d\n", l, wcscmp(end, L"rest") == 0);
    printf("wcstol hex %ld\n", wcstol(L"0xffl", &end, 16));
    printf("wcstoul %lu\n", wcstoul(L"4294967295", NULL, 10));
    printf("wcstoll %lld\n", wcstoll(L"-9007199254740993", NULL, 10));
    printf("wcstoull %llu\n", wcstoull(L"18446744073709551615", NULL, 10));
    errno = 0;
    l = wcstol(L"999999999999999999999999", NULL, 10);
    printf("wcstol rim %d %d\n", l == LONG_MAX, errno == ERANGE);

    d = wcstod(L"3.5e2xyz", &end);
    printf("wcstod %d tail %d\n", d == 350.0, wcscmp(end, L"xyz") == 0);
    printf("wcstof %d\n", wcstof(L"0.5", NULL) == 0.5f);
    printf("wcstold %d\n", wcstold(L"2.0", NULL) == 2.0L);

    tm.tm_year = 99; tm.tm_mon = 11; tm.tm_mday = 31;
    tm.tm_hour = 23; tm.tm_min = 59; tm.tm_sec = 58;
    printf("wcsftime %d\n",
           (int)wcsftime(buf, 64, L"%Y-%m-%d %H:%M:%S", &tm));
    printf("formatted %ls\n", buf);

    printf("wcwidth a %d\n", wcwidth(L'a'));
    printf("wcwidth nul %d\n", wcwidth(L'\0'));
    printf("wcswidth %d\n", wcswidth(L"abc", 3));

    printf("wcscoll %d\n", wcscoll(L"abc", L"abd") < 0);
    printf("wcsxfrm len %d\n", (int)wcsxfrm(buf, L"abc", 64));
    printf("casecmp %d\n", wcscasecmp(L"HeLLo", L"hello") == 0);
    printf("ncasecmp %d\n", wcsncasecmp(L"HeLLxx", L"hellyy", 4) == 0);
    return 0;
}
