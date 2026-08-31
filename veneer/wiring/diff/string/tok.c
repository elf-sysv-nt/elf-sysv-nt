/* string slice: tokenizing and the errno/signal text lookups.
 *
 * Only string.h; the errnums here are written as bare values (EINVAL 22,
 * ENOENT 2) from when the sysroot lacked <linux/errno.h>.  err.c spells
 * them symbolically now that the kernel headers are laid in.
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
