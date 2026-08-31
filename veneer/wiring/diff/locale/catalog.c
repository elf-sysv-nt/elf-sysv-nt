/* locale slice: the message catalogs without a catalog -- catopen's
   refusal of a missing name, catgets falling back to the caller's
   default string through a failed descriptor, and catclose refusing
   it, every call sequenced before its printf. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <nl_types.h>

int main(void)
{
    nl_catd cat;
    char *msg;
    int rc;

    errno = 0;
    cat = catopen("no-such-catalog", 0);
    printf("open %d %d\n", cat == (nl_catd)-1, errno != 0);

    errno = 0;
    cat = catopen("no-such-catalog", NL_CAT_LOCALE);
    printf("open-locale %d\n", cat == (nl_catd)-1);

    /* catgets through the failed descriptor hands back the default,
       the same pointer, untouched */
    msg = catgets(cat, 1, 1, "the default line");
    printf("gets [%s]\n", msg);
    printf("gets-same %d\n", strcmp(msg, "the default line") == 0);
    msg = catgets(cat, NL_SETD, 42, "another default");
    printf("gets-setd [%s]\n", msg);

    /* closing what never opened is refused */
    rc = catclose(cat);
    printf("close %d\n", rc == -1);
    return 0;
}
