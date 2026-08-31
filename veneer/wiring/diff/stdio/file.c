/* stdio slice: named files through open, reopen, rename, remove. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char a[64], b[64], buf[32];
    FILE *f;
    int r;

    snprintf(a, sizeof a, "/tmp/esn-stdio-%d.a", (int)getpid());
    snprintf(b, sizeof b, "/tmp/esn-stdio-%d.b", (int)getpid());

    f = fopen(a, "w");
    if (!f) { puts("fopen failed"); return 1; }
    setvbuf(f, NULL, _IOFBF, 4096);
    printf("fputs %d\n", fputs("payload\n", f) >= 0);
    printf("fclose %d\n", fclose(f));

    printf("rename %d\n", rename(a, b));
    printf("gone %d\n", fopen(a, "r") == NULL);

    f = fopen(b, "r");
    printf("fgets %s", fgets(buf, sizeof buf, f));
    f = freopen(b, "a+", f);
    printf("freopen %d\n", f != NULL);
    fputs("more\n", f);
    fflush(f);
    rewind(f);
    while (fgets(buf, sizeof buf, f))
        printf("line %s", buf);
    printf("fclose2 %d\n", fclose(f));

    r = remove(b);
    printf("remove %d %d\n", r, remove(b) == -1);
    return 0;
}
