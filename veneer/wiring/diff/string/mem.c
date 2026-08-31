/* string slice: the mem* family's observable behaviour. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

int main(void)
{
    char a[24], b[16];

    memset(a, 'x', 15);
    a[15] = 0;
    printf("memset %s\n", a);

    memcpy(b, "0123456789abcde", 16);
    printf("memcpy %s\n", b);

    memmove(b + 2, b, 13);
    b[15] = 0;
    printf("memmove %s\n", b);

    memcpy(b, "0123456789abcde", 16);
    printf("memchr %d\n", (int)((char *)memchr(b, '7', 16) - b));
    printf("memchr-miss %d\n", memchr(b, 'z', 16) == NULL);
    printf("memcmp %d %d %d\n", memcmp("abc", "abc", 3) == 0,
           memcmp("abc", "abd", 3) < 0, memcmp("abd", "abc", 3) > 0);
    memcpy(a, "needle in haystack", 19);
    printf("memmem %d\n", (int)((char *)memmem(a, 19, "in", 2) - a));
    printf("rawmemchr %d\n", (int)((char *)rawmemchr(a, 'h') - a));
    printf("memrchr %d\n", (int)((char *)memrchr(a, 'a', 18) - a));
    return 0;
}
