/* locale slice: the wide classifications in the C locale -- each
   class counted over the first 256 codes, the tow* case maps, and
   the named classes and mappings through wctype/wctrans. */
#define _GNU_SOURCE
#include <stdio.h>
#include <wctype.h>

static int count(int (*f)(wint_t))
{
    wint_t c;
    int n = 0;
    for (c = 0; c < 256; c++)
        if (f(c))
            n++;
    return n;
}

int main(void)
{
    wint_t c;
    wctype_t alpha, none;
    wctrans_t upper, gone;
    int agree, bad;

    printf("walnum %d\n", count(iswalnum));
    printf("walpha %d\n", count(iswalpha));
    printf("wblank %d\n", count(iswblank));
    printf("wcntrl %d\n", count(iswcntrl));
    printf("wdigit %d\n", count(iswdigit));
    printf("wgraph %d\n", count(iswgraph));
    printf("wlower %d\n", count(iswlower));
    printf("wprint %d\n", count(iswprint));
    printf("wpunct %d\n", count(iswpunct));
    printf("wspace %d\n", count(iswspace));
    printf("wupper %d\n", count(iswupper));
    printf("wxdigit %d\n", count(iswxdigit));

    /* the wide case maps agree with the narrow story over ASCII and
       leave WEOF alone */
    bad = 0;
    for (c = 0; c < 128; c++) {
        if (iswupper(c) && towlower(c) != c + 32)
            bad++;
        if (iswlower(c) && towupper(c) != c - 32)
            bad++;
    }
    printf("wcase %d %d %d\n", bad,
           towlower(WEOF) == WEOF, towupper(WEOF) == WEOF);

    /* the named classes resolve; a made-up name does not */
    alpha = wctype("alpha");
    none = wctype("no-such-class");
    printf("wctype %d %d\n", alpha != (wctype_t)0, none == (wctype_t)0);
    agree = 1;
    for (c = 0; c < 256; c++)
        if (!iswctype(c, alpha) != !iswalpha(c))
            agree = 0;
    printf("wctype-agree %d\n", agree);

    /* the named mappings resolve; a made-up name does not */
    upper = wctrans("toupper");
    gone = wctrans("no-such-map");
    printf("wctrans %d %d\n", upper != (wctrans_t)0, gone == (wctrans_t)0);
    agree = 1;
    for (c = 0; c < 256; c++)
        if (towctrans(c, upper) != towupper(c))
            agree = 0;
    printf("wctrans-agree %d\n", agree);
    return 0;
}
