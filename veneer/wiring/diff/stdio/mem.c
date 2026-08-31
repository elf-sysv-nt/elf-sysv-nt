/* stdio slice: memory streams and line readers. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char text[] = "first line\nsecond line\nthird:part\n";
    char store[16];
    char *line = NULL, *out = NULL;
    size_t cap = 0, outn = 0;
    FILE *f;
    int r;

    f = fmemopen(text, strlen(text), "r");
    r = (int)getline(&line, &cap, f);
    printf("getline %d %s", r, line);
    r = (int)getdelim(&line, &cap, ':', f);
    printf("getdelim %d %s|\n", r, line);
    while ((r = (int)getline(&line, &cap, f)) != -1)
        printf("more %d %s", r, line);
    r = feof(f);
    printf("done %d %d\n", r, fclose(f));
    free(line);

    f = fmemopen(store, sizeof store, "w");
    r = fprintf(f, "0123456789abcdefXYZ");
    fflush(f);
    printf("wclip %d %.15s\n", r, store);
    fclose(f);

    f = open_memstream(&out, &outn);
    fputs("grown", f);
    fprintf(f, "+%d", 42);
    fflush(f);
    printf("memstream %d %s\n", (int)outn, out);
    fclose(f);
    free(out);
    return 0;
}
