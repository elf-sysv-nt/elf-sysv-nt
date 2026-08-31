/* locale slice: the narrow classifications in the C locale -- each
   class counted over 0..255 as unsigned char, the case maps as
   round-trips, isascii/toascii, and EOF through every classifier. */
#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>

static int count(int (*f)(int))
{
    int c, n = 0;
    for (c = 0; c < 256; c++)
        if (f(c))
            n++;
    return n;
}

int main(void)
{
    int c, low, up, bad;

    printf("alnum %d\n", count(isalnum));
    printf("alpha %d\n", count(isalpha));
    printf("blank %d\n", count(isblank));
    printf("cntrl %d\n", count(iscntrl));
    printf("digit %d\n", count(isdigit));
    printf("graph %d\n", count(isgraph));
    printf("lower %d\n", count(islower));
    printf("print %d\n", count(isprint));
    printf("punct %d\n", count(ispunct));
    printf("space %d\n", count(isspace));
    printf("upper %d\n", count(isupper));
    printf("xdigit %d\n", count(isxdigit));

    /* the case maps round-trip across the alphabet and leave the
       rest alone */
    low = up = bad = 0;
    for (c = 0; c < 256; c++) {
        if (isupper(c) && tolower(c) != c + 32)
            bad++;
        if (islower(c) && toupper(c) != c - 32)
            bad++;
        if (!isupper(c) && tolower(c) != c)
            low++;
        if (!islower(c) && toupper(c) != c)
            up++;
    }
    printf("case-maps %d %d %d\n", bad, low, up);
    printf("case-eof %d %d\n", tolower(EOF) == EOF, toupper(EOF) == EOF);

    /* isascii takes the whole int range's edges; toascii masks */
    printf("ascii %d %d %d %d\n",
           isascii(0), isascii(127), isascii(128), isascii(255));
    printf("toascii %d %d %d\n",
           toascii(0x41), toascii(0xC1), toascii(0x141));

    /* every classifier accepts EOF and refuses it */
    printf("eof %d\n",
           !isalnum(EOF) && !isalpha(EOF) && !isblank(EOF) &&
           !iscntrl(EOF) && !isdigit(EOF) && !isgraph(EOF) &&
           !islower(EOF) && !isprint(EOF) && !ispunct(EOF) &&
           !isspace(EOF) && !isupper(EOF) && !isxdigit(EOF));
    return 0;
}
