/* locale slice: the locale-object family -- newlocale, duplocale,
   uselocale round-trips against LC_GLOBAL_LOCALE, the _l classifiers
   through an explicit object, and freelocale leaving the thread's
   locale untouched. */
#define _GNU_SOURCE
#include <stdio.h>
#include <locale.h>
#include <ctype.h>
#include <wctype.h>

int main(void)
{
    locale_t c, dup, bad, prev, back;
    int agree, ch;

    c = newlocale(LC_ALL_MASK, "C", (locale_t)0);
    printf("new %d\n", c != (locale_t)0);
    bad = newlocale(LC_ALL_MASK, "no-such-locale", (locale_t)0);
    printf("new-bad %d\n", bad == (locale_t)0);

    /* the _l classifiers through the object agree with the plain
       ones in the C locale, narrow and wide */
    agree = 1;
    for (ch = 0; ch < 256; ch++) {
        if (!isalpha_l(ch, c) != !isalpha(ch))
            agree = 0;
        if (!isspace_l(ch, c) != !isspace(ch))
            agree = 0;
        if (tolower_l(ch, c) != tolower(ch))
            agree = 0;
        if (!iswpunct_l((wint_t)ch, c) != !iswpunct((wint_t)ch))
            agree = 0;
        if (towupper_l((wint_t)ch, c) != towupper((wint_t)ch))
            agree = 0;
    }
    printf("object-agree %d\n", agree);

    /* wctype_t names resolve per-object too */
    printf("wctype-l %d %d\n",
           wctype_l("digit", c) != (wctype_t)0,
           wctype_l("no-such-class", c) == (wctype_t)0);
    printf("wctrans-l %d\n", wctrans_l("tolower", c) != (wctrans_t)0);

    /* uselocale round-trips: the thread starts on the global locale,
       installing the object returns that, and restoring returns the
       object */
    prev = uselocale(c);
    printf("use-prev-global %d\n", prev == LC_GLOBAL_LOCALE);
    printf("use-current %d\n", uselocale((locale_t)0) == c);
    back = uselocale(LC_GLOBAL_LOCALE);
    printf("use-back %d\n", back == c);
    printf("use-restored %d\n", uselocale((locale_t)0) == LC_GLOBAL_LOCALE);

    /* duplocale gives a distinct object with the same behaviour */
    dup = duplocale(c);
    printf("dup %d %d\n", dup != (locale_t)0, dup != c);
    printf("dup-agree %d\n", isupper_l('A', dup) != 0 &&
           tolower_l('Z', dup) == 'z');
    freelocale(dup);
    freelocale(c);
    printf("done %d\n", uselocale((locale_t)0) == LC_GLOBAL_LOCALE);
    return 0;
}
