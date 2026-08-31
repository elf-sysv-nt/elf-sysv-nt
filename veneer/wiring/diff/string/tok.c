/* string slice: tokenizing and the errno/signal text lookups.
 *
 * Only string.h: the sysroot's errno.h reaches for linux/errno.h, which
 * the header set does not carry yet, so the errnums are Linux's values
 * written as themselves (EINVAL 22, ENOENT 2).  The argz family waits on
 * the same gap -- argz.h includes errno.h.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[64], *save, *tok, *p;

    strcpy(buf, " a bb  ccc ");
    for (tok = strtok_r(buf, " ", &save); tok;
         tok = strtok_r(NULL, " ", &save))
        printf("tok %s\n", tok);

    strcpy(buf, "x::y");
    p = buf;
    printf("strsep %s\n", strsep(&p, ":"));
    printf("strsep-empty %d\n", *strsep(&p, ":") == 0);
    printf("strsep %s\n", strsep(&p, ":"));

    printf("strerror %s\n", strerror(22));
    printf("strerror %s\n", strerror(2));
    printf("strsignal %s\n", strsignal(9));
    return 0;
}
