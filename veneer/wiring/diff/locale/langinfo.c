/* locale slice: nl_langinfo in the C locale -- the codeset, the
   calendar names at both ends, the format strings, the radix, the
   yes/no expressions, and the _l twin agreeing through an explicit
   object. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>

int main(void)
{
    locale_t c;

    printf("codeset %s\n", nl_langinfo(CODESET));
    printf("day1 %s day7 %s\n", nl_langinfo(DAY_1), nl_langinfo(DAY_7));
    printf("abday1 %s\n", nl_langinfo(ABDAY_1));
    printf("mon1 %s mon12 %s\n", nl_langinfo(MON_1), nl_langinfo(MON_12));
    printf("abmon1 %s\n", nl_langinfo(ABMON_1));
    printf("dfmt [%s]\n", nl_langinfo(D_FMT));
    printf("tfmt [%s]\n", nl_langinfo(T_FMT));
    printf("dtfmt [%s]\n", nl_langinfo(D_T_FMT));
    printf("ampm [%s][%s]\n", nl_langinfo(AM_STR), nl_langinfo(PM_STR));
    printf("radix [%s] thousep [%s]\n",
           nl_langinfo(RADIXCHAR), nl_langinfo(THOUSEP));
    printf("yes [%s] no [%s]\n", nl_langinfo(YESEXPR), nl_langinfo(NOEXPR));
    printf("crncy [%s]\n", nl_langinfo(CRNCYSTR));

    /* the _l twin through a fresh C object answers the same */
    c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    printf("l-agree %d\n", c != (locale_t)0 &&
           strcmp(nl_langinfo_l(CODESET, c), nl_langinfo(CODESET)) == 0 &&
           strcmp(nl_langinfo_l(DAY_1, c), nl_langinfo(DAY_1)) == 0 &&
           strcmp(nl_langinfo_l(D_FMT, c), nl_langinfo(D_FMT)) == 0 &&
           strcmp(nl_langinfo_l(YESEXPR, c), nl_langinfo(YESEXPR)) == 0);
    freelocale(c);
    return 0;
}
