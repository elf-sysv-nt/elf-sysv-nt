/* wchar slice: the wide stream protocol — swprintf/swscanf rounding
   a record trip, open_wmemstream collecting fwprintf and fputws and
   fputwc into a sized buffer, and a tmpfile written wide then read
   back through fgetws, fgetwc and a pushed-back ungetwc, with fwide
   reporting the orientation the first wide operation set. */
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    wchar_t buf[64], line[64];
    wchar_t *mem;
    size_t memlen;
    FILE *f;
    int n, v;

    n = swprintf(buf, 64, L"%d/%ls", 42, L"answer");
    printf("swprintf %d\n", n);
    printf("swprintf text %d\n", wcscmp(buf, L"42/answer") == 0);
    n = swscanf(buf, L"%d/%ls", &v, line);
    printf("swscanf %d %d %d\n", n, v, wcscmp(line, L"answer") == 0);

    f = open_wmemstream(&mem, &memlen);
    printf("wmemstream open %d\n", f != NULL);
    fwprintf(f, L"n=%d ", 7);
    fputws(L"tail", f);
    fputwc(L'!', f);
    fclose(f);
    printf("wmemstream len %d\n", (int)memlen);
    printf("wmemstream text %d\n", wcscmp(mem, L"n=7 tail!") == 0);
    free(mem);

    f = tmpfile();
    printf("tmpfile %d\n", f != NULL);
    printf("unset orientation %d\n", fwide(f, 0));
    fputws(L"first line\n", f);
    fputws(L"second\n", f);
    printf("wide orientation %d\n", fwide(f, 0) > 0);
    rewind(f);
    printf("fgetws %d\n",
           fgetws(line, 64, f) == line && wcscmp(line, L"first line\n") == 0);
    printf("fgetwc %d\n", fgetwc(f) == L's');
    printf("ungetwc %d\n", ungetwc(L'S', f) == L'S');
    printf("reread %d\n", fgetwc(f) == L'S');
    printf("rest %d\n",
           fgetws(line, 64, f) == line && wcscmp(line, L"econd\n") == 0);
    printf("eof %d\n", fgetwc(f) == WEOF && feof(f) != 0);
    fclose(f);
    return 0;
}
