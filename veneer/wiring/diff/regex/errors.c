/* regex slice: the refusals and their texts — each bad pattern named
   by its error code, regerror's message for every code it mints, and
   the truncation contract: the returned length counts the whole
   message while a short buffer gets a terminated prefix of it. */
#define _GNU_SOURCE
#include <regex.h>
#include <stdio.h>
#include <string.h>

static void bad(const char *tag, const char *pat, int cf, int want)
{
    regex_t re;
    char msg[256];
    int rc = regcomp(&re, pat, cf);
    printf("%s code %d\n", tag, rc == want);
    regerror(rc, &re, msg, sizeof msg);
    printf("%s text %s\n", tag, msg);
    if (rc == 0)
        regfree(&re);
}

int main(void)
{
    regex_t re;
    char msg[256];
    char tiny[8];
    size_t need;

    bad("ebrack", "[abc", 0, REG_EBRACK);
    bad("eparen", "(abc", REG_EXTENDED, REG_EPAREN);
    bad("ebrace", "a\\{1", 0, REG_EBRACE);
    bad("badbr", "a\\{3,1\\}", 0, REG_BADBR);
    bad("erange", "[z-a]", 0, REG_ERANGE);
    bad("esubreg", "\\(a\\)\\2", 0, REG_ESUBREG);
    bad("badrpt", "*a", REG_EXTENDED, REG_BADRPT);
    bad("eescape", "a\\", 0, REG_EESCAPE);
    bad("ectype", "[[:whatever:]]", 0, REG_ECTYPE);

    printf("comp ok %d\n", regcomp(&re, "abc", 0));
    regerror(0, &re, msg, sizeof msg);
    printf("noerror %s\n", msg);
    regerror(REG_NOMATCH, &re, msg, sizeof msg);
    printf("nomatch %s\n", msg);

    need = regerror(REG_EBRACK, &re, NULL, 0);
    printf("size only %d\n", need > 1);
    regerror(REG_EBRACK, &re, msg, sizeof msg);
    printf("size full %d\n", need == strlen(msg) + 1);
    memset(tiny, 'x', sizeof tiny);
    regerror(REG_EBRACK, &re, tiny, sizeof tiny);
    printf("tiny term %d\n", tiny[sizeof tiny - 1] == '\0');
    printf("tiny prefix %d\n",
           strncmp(tiny, msg, sizeof tiny - 1) == 0);
    regfree(&re);
    return 0;
}
