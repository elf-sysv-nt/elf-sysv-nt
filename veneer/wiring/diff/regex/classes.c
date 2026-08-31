/* regex slice: bracket expressions and case — ranges and negation,
   the named classes in the C locale, REG_ICASE folding both the
   literal and the bracket forms, and intervals counting repetitions
   in both syntaxes. */
#define _GNU_SOURCE
#include <regex.h>
#include <stdio.h>

static void one(const char *tag, const char *pat, int cf,
                const char *s)
{
    regex_t re;
    regmatch_t m;
    int rc = regcomp(&re, pat, cf);
    printf("%s comp %d\n", tag, rc);
    if (rc != 0)
        return;
    rc = regexec(&re, s, 1, &m, 0);
    if (rc == 0)
        printf("%s hit %d:%d\n", tag, (int)m.rm_so, (int)m.rm_eo);
    else
        printf("%s miss %d\n", tag, rc == REG_NOMATCH);
    regfree(&re);
}

int main(void)
{
    one("range", "[a-f]*g", 0, "xdeadbeefgx");
    one("negate", "[^0-9]*", 0, "abc123");
    one("alpha", "[[:alpha:]]*", 0, "word99");
    one("digit", "[[:digit:]]*x", 0, "no42x");
    one("space", "a[[:space:]]*b", 0, "a \t b!");
    one("punct", "[[:punct:]]*", 0, ";,!rest");
    one("upper", "[[:upper:]]*", 0, "ABCdef");
    one("xdigit", "0x[[:xdigit:]]*", 0, "at 0xFF00 sits");
    one("litdot", "a[.]b", 0, "a.b");
    one("dotany", "a.b", 0, "axb");

    one("icase lit", "hello", REG_ICASE, "say HeLLo there");
    one("icase brk", "[a-c]*d", REG_ICASE, "xAbCd");
    one("case miss", "HELLO", 0, "hello");

    one("bre iv", "a\\{2,3\\}", 0, "xaaaa");
    one("ere iv", "a{2,3}", REG_EXTENDED, "xaaaa");
    one("ere ivx", "(ab){2}", REG_EXTENDED, "zababab");
    one("bre ivmiss", "a\\{4,\\}", 0, "aaa");
    return 0;
}
