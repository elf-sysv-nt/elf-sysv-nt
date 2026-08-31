/* misc slice: wordexp under a pinned environment, dirname over the
   POSIX examples, and the random-byte pair as invariants. The variables
   wordexp expands are set here and IFS is cleared so no inherited
   environment decides the fields; command substitution is refused by
   flag and a bad character by value. getentropy and getrandom fill
   what they are asked for -- lengths, return codes and the oversized
   getentropy refusal, never bytes. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <wordexp.h>
#include <libgen.h>
#include <sys/random.h>
#include <unistd.h>

int main(void)
{
    wordexp_t we;
    int rc;
    unsigned i;

    unsetenv("IFS");
    setenv("V1", "one two", 1);
    setenv("V2", "three", 1);

    rc = wordexp("$V1 ${V2}x lit", &we, 0);
    printf("expand %d %zu\n", rc, we.we_wordc);
    for (i = 0; i < we.we_wordc; i++)
        printf("word %s\n", we.we_wordv[i]);

    rc = wordexp("tail", &we, WRDE_APPEND);
    printf("append %d %zu %s\n", rc, we.we_wordc,
           we.we_wordv[we.we_wordc - 1]);
    wordfree(&we);

    rc = wordexp("$(echo hi)", &we, WRDE_NOCMD);
    printf("nocmd %d\n", rc == WRDE_CMDSUB);

    rc = wordexp("a|b", &we, 0);
    printf("badchar %d\n", rc == WRDE_BADCHAR);

    {
        static const char *in[] = { "/usr/lib", "/usr/", "usr", "/",
                                    ".", ".." };
        char buf[32];
        for (i = 0; i < sizeof in / sizeof in[0]; i++) {
            strcpy(buf, in[i]);
            printf("dirname %s %s\n", in[i], dirname(buf));
        }
    }

    {
        unsigned char buf[16];
        char big[300];
        ssize_t n;

        memset(buf, 0, sizeof buf);
        rc = getentropy(buf, sizeof buf);
        printf("entropy %d\n", rc);

        errno = 0;
        rc = getentropy(big, sizeof big);
        printf("entropybig %d %d\n", rc, errno == EIO);

        n = getrandom(buf, sizeof buf, 0);
        printf("random %d\n", n == (ssize_t)sizeof buf);

        n = getrandom(buf, sizeof buf, GRND_NONBLOCK);
        printf("randomnb %d\n", n == (ssize_t)sizeof buf);
    }

    return 0;
}
