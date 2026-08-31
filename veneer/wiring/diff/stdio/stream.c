/* stdio slice: an anonymous stream through the positioning calls. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE *f = tmpfile();
    char buf[32];
    fpos_t pos;
    int c;

    if (!f) { puts("tmpfile failed"); return 1; }

    printf("fwrite %d\n", (int)fwrite("abcdefgh", 1, 8, f));
    printf("putw %d\n", putw(0x01020304, f));
    printf("ftell %ld\n", ftell(f));

    rewind(f);
    c = (int)fread(buf, 1, 4, f);
    printf("fread %d %.4s\n", c, buf);
    fgetpos(f, &pos);
    c = fgetc(f);
    printf("fgetc %c%c\n", c, fgetc(f));
    fsetpos(f, &pos);
    printf("again %c\n", fgetc(f));

    c = fseek(f, -2, SEEK_END);
    printf("fseek %d %ld\n", c, (long)ftello(f));
    c = fgetc(f);
    printf("ungetc %d %d\n", c, (ungetc(c, f), fgetc(f)));

    printf("getw %d\n", (fseek(f, 8, SEEK_SET), getw(f)));
    printf("flags %d %d\n", feof(f), ferror(f));
    while (fgetc(f) != EOF)
        ;
    printf("ateof %d %d\n", feof(f), ferror(f));
    clearerr(f);
    printf("cleared %d\n", feof(f));
    printf("fclose %d\n", fclose(f));
    return 0;
}
