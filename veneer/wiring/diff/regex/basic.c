/* regex slice: the compile-and-match core — a literal found where it
   is, subexpressions reporting their offsets, BRE against ERE syntax
   over the same pattern text, REG_NOSUB compiling with re_nsub still
   counted, and back-references binding to what the group captured. */
#define _GNU_SOURCE
#include <regex.h>
#include <stdio.h>
#include <string.h>

static void spans(const char *tag, regmatch_t *m, int n)
{
    int i;
    printf("%s", tag);
    for (i = 0; i < n; i++)
        printf(" %d:%d", (int)m[i].rm_so, (int)m[i].rm_eo);
    printf("\n");
}

int main(void)
{
    regex_t re;
    regmatch_t m[4];

    printf("literal comp %d\n", regcomp(&re, "world", 0));
    printf("literal exec %d\n", regexec(&re, "hello world", 1, m, 0));
    spans("literal", m, 1);
    printf("literal miss %d\n",
           regexec(&re, "goodbye", 0, NULL, 0) == REG_NOMATCH);
    regfree(&re);

    printf("groups comp %d\n",
           regcomp(&re, "\\(a*\\)\\(b*\\)c", 0));
    printf("groups nsub %d\n", (int)re.re_nsub);
    printf("groups exec %d\n", regexec(&re, "xxaabbbc", 3, m, 0));
    spans("groups", m, 3);
    regfree(&re);

    /* the same characters mean different things in the two syntaxes */
    printf("bre plus comp %d\n", regcomp(&re, "a+", 0));
    printf("bre plus lit %d\n", regexec(&re, "a+", 0, NULL, 0));
    printf("bre plus rep %d\n",
           regexec(&re, "aaa", 0, NULL, 0) == REG_NOMATCH);
    regfree(&re);
    printf("ere plus comp %d\n", regcomp(&re, "a+", REG_EXTENDED));
    printf("ere plus rep %d\n", regexec(&re, "aaa", 1, m, 0));
    spans("ere plus", m, 1);
    regfree(&re);

    printf("ere alt comp %d\n",
           regcomp(&re, "(cat|dog)s?", REG_EXTENDED));
    printf("ere alt nsub %d\n", (int)re.re_nsub);
    printf("ere alt exec %d\n", regexec(&re, "hotdogs", 2, m, 0));
    spans("ere alt", m, 2);
    regfree(&re);

    printf("nosub comp %d\n",
           regcomp(&re, "\\(x\\)y", REG_NOSUB));
    printf("nosub nsub %d\n", (int)re.re_nsub);
    printf("nosub exec %d\n", regexec(&re, "wxyz", 0, NULL, 0));
    regfree(&re);

    printf("backref comp %d\n",
           regcomp(&re, "\\(ab\\)\\1", 0));
    printf("backref hit %d\n", regexec(&re, "zabab", 2, m, 0));
    spans("backref", m, 2);
    printf("backref miss %d\n",
           regexec(&re, "abba", 0, NULL, 0) == REG_NOMATCH);
    regfree(&re);
    return 0;
}
