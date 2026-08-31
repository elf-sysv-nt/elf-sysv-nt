/* regex slice: where a match may start and end — the anchors plain,
   REG_NOTBOL and REG_NOTEOL turning them off, REG_NEWLINE making them
   line-relative and stopping dot at the break, an empty pattern
   matching everywhere, and scanning a subject match by match through
   rm_eo. */
#define _GNU_SOURCE
#include <regex.h>
#include <stdio.h>

int main(void)
{
    regex_t re;
    regmatch_t m;
    const char *s;
    int rc, off;

    printf("bol comp %d\n", regcomp(&re, "^abc", 0));
    printf("bol hit %d\n", regexec(&re, "abcdef", 0, NULL, 0));
    printf("bol mid %d\n",
           regexec(&re, "xxabc", 0, NULL, 0) == REG_NOMATCH);
    printf("bol notbol %d\n",
           regexec(&re, "abc", 0, NULL, REG_NOTBOL) == REG_NOMATCH);
    regfree(&re);

    printf("eol comp %d\n", regcomp(&re, "xyz$", 0));
    printf("eol hit %d\n", regexec(&re, "wxyz", 0, NULL, 0));
    printf("eol noteol %d\n",
           regexec(&re, "wxyz", 0, NULL, REG_NOTEOL) == REG_NOMATCH);
    regfree(&re);

    printf("nl comp %d\n", regcomp(&re, "^b$", REG_NEWLINE));
    printf("nl hit %d\n", regexec(&re, "a\nb\nc", 1, &m, 0));
    printf("nl span %d:%d\n", (int)m.rm_so, (int)m.rm_eo);
    regfree(&re);
    printf("flat comp %d\n", regcomp(&re, "^b$", 0));
    printf("flat miss %d\n",
           regexec(&re, "a\nb\nc", 0, NULL, 0) == REG_NOMATCH);
    regfree(&re);

    printf("nldot comp %d\n", regcomp(&re, "a.c", REG_NEWLINE));
    printf("nldot miss %d\n",
           regexec(&re, "a\nc", 0, NULL, 0) == REG_NOMATCH);
    regfree(&re);
    printf("dotnl comp %d\n", regcomp(&re, "a.c", 0));
    printf("dotnl hit %d\n", regexec(&re, "a\nc", 0, NULL, 0));
    regfree(&re);

    printf("empty comp %d\n", regcomp(&re, "", 0));
    printf("empty hit %d\n", regexec(&re, "anything", 1, &m, 0));
    printf("empty span %d:%d\n", (int)m.rm_so, (int)m.rm_eo);
    regfree(&re);

    printf("scan comp %d\n", regcomp(&re, "[0-9][0-9]*", 0));
    s = "a1bb22ccc333d";
    off = 0;
    while (regexec(&re, s + off, 1, &m, off ? REG_NOTBOL : 0) == 0) {
        printf("scan %d:%d\n", off + (int)m.rm_so, off + (int)m.rm_eo);
        off += (int)m.rm_eo;
    }
    printf("scan done\n");
    regfree(&re);
    return 0;
}
