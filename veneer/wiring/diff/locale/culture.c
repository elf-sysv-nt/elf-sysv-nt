/* locale slice: setlocale and localeconv -- the startup default, the
   C and POSIX round-trips, the refusal of a made-up locale, and the
   C locale's numeric and monetary conventions read back whole. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <locale.h>

static const char *nz(const char *s) { return s && *s ? s : "(empty)"; }

int main(void)
{
    char *cur, *set;
    struct lconv *lc;

    /* a program starts in the C locale */
    cur = setlocale(LC_ALL, NULL);
    printf("start %s\n", nz(cur));

    /* C and POSIX both install and answer by their own name */
    set = setlocale(LC_ALL, "C");
    printf("set-c %s\n", nz(set));
    set = setlocale(LC_ALL, "POSIX");
    printf("set-posix %s\n", nz(set));
    set = setlocale(LC_ALL, "C");
    printf("set-c-again %s\n", nz(set));

    /* a made-up locale is refused and the current one survives */
    set = setlocale(LC_ALL, "no-such-locale");
    printf("set-bad %d\n", set == NULL);
    printf("kept %s\n", nz(setlocale(LC_ALL, NULL)));

    /* one category at a time answers per-category */
    set = setlocale(LC_NUMERIC, NULL);
    printf("numeric %s\n", nz(set));
    set = setlocale(LC_TIME, "C");
    printf("time %s\n", nz(set));

    /* the C locale's lconv, field by field */
    lc = localeconv();
    printf("decimal [%s]\n", lc->decimal_point);
    printf("thousands [%s]\n", lc->thousands_sep);
    printf("grouping %d\n", lc->grouping[0]);
    printf("currency [%s][%s]\n", lc->int_curr_symbol, lc->currency_symbol);
    printf("mon-point [%s][%s]\n", lc->mon_decimal_point, lc->mon_thousands_sep);
    printf("signs [%s][%s]\n", lc->positive_sign, lc->negative_sign);
    printf("frac %d %d %d\n",
           lc->frac_digits == CHAR_MAX,
           lc->int_frac_digits == CHAR_MAX,
           lc->p_cs_precedes == CHAR_MAX);
    return 0;
}
