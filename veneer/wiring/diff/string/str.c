/* string slice: the str* family's observable behaviour. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[64];
    const char *h = "one haystack, two haystacks";

    printf("strlen %d %d\n", (int)strlen(""), (int)strlen("haystack"));
    printf("strnlen %d %d\n", (int)strnlen("haystack", 3),
           (int)strnlen("hay", 4));

    strcpy(buf, "left");
    strcat(buf, "-right");
    printf("strcat %s\n", buf);
    strncpy(buf, "abcdef", 3);
    buf[3] = 0;
    printf("strncpy %s\n", buf);
    printf("stpcpy %d\n", (int)(stpcpy(buf, "hop") - buf));

    printf("strcmp %d %d %d\n", strcmp("a", "a") == 0,
           strcmp("a", "b") < 0, strcmp("b", "a") > 0);
    printf("strncmp %d\n", strncmp("abcX", "abcY", 3) == 0);
    printf("strcasecmp %d %d\n", strcasecmp("HAY", "hay") == 0,
           strncasecmp("HAYx", "hayY", 3) == 0);
    printf("strverscmp %d %d\n", strverscmp("a2", "a10") < 0,
           strverscmp("a10", "a2") > 0);

    printf("strchr %d\n", (int)(strchr(h, 'y') - h));
    printf("strrchr %d\n", (int)(strrchr(h, 'y') - h));
    printf("strchrnul %d\n", (int)(strchrnul(h, 'z') - h));
    printf("strstr %d\n", (int)(strstr(h, "two") - h));
    printf("strcasestr %d\n", (int)(strcasestr(h, "TWO") - h));
    printf("strspn %d %d\n", (int)strspn("aabbc", "ab"),
           (int)strcspn("aabbc", "c"));
    printf("strpbrk %d\n", (int)(strpbrk(h, "kt") - h));
    return 0;
}
