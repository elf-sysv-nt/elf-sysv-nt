/* wchar slice: the wide string operators this slice carries — the
   copy/concatenate family with wcpcpy's end-pointer contract, the
   span and search family over one haystack, wcstok walking three
   fields, wcsdup surviving a round trip, and the wmem block movers
   including an overlapping wmemmove. */
#define _GNU_SOURCE
#include <wchar.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    wchar_t a[32], b[32];
    wchar_t *p, *save, *dup;
    const wchar_t *hay = L"needle in haystack";

    p = wcpcpy(a, L"abc");
    printf("wcpcpy end %d\n", p == a + 3 && *p == L'\0');
    p = wcpncpy(b, L"abc", 5);
    printf("wcpncpy pad end %d\n", p == b + 3 && b[4] == L'\0');
    wcsncpy(b, L"xyzzy", 3);
    b[3] = L'\0';
    printf("wcsncpy prefix %d\n", wcscmp(b, L"xyz") == 0);
    wcscat(a, L"def");
    printf("wcscat %d\n", wcscmp(a, L"abcdef") == 0);
    wcsncat(a, L"ghijk", 2);
    printf("wcsncat %d\n", wcscmp(a, L"abcdefgh") == 0);

    printf("wcsstr hit %d\n", wcsstr(hay, L"hay") == hay + 10);
    printf("wcsstr miss %d\n", wcsstr(hay, L"rope") == NULL);
    printf("wcspbrk %d\n", wcspbrk(hay, L"xyz") == hay + 13);
    printf("wcscspn %d\n", (int)wcscspn(hay, L" "));
    printf("wcsspn %d\n", (int)wcsspn(hay, L"eden"));

    wcscpy(a, L"one:two:three");
    p = wcstok(a, L":", &save);
    printf("tok one %d\n", p && wcscmp(p, L"one") == 0);
    p = wcstok(NULL, L":", &save);
    printf("tok two %d\n", p && wcscmp(p, L"two") == 0);
    p = wcstok(NULL, L":", &save);
    printf("tok three %d\n", p && wcscmp(p, L"three") == 0);
    printf("tok done %d\n", wcstok(NULL, L":", &save) == NULL);

    dup = wcsdup(L"copied");
    printf("wcsdup %d\n", dup && wcscmp(dup, L"copied") == 0);
    free(dup);

    wcscpy(a, L"0123456789");
    wmemcpy(b, a, 11);
    printf("wmemcpy %d\n", wcscmp(b, L"0123456789") == 0);
    p = wmempcpy(b, L"ab", 2);
    printf("wmempcpy end %d\n", p == b + 2);
    wmemmove(a + 2, a, 4);
    a[7] = L'\0';
    printf("wmemmove overlap %d\n", wcscmp(a, L"0101236") == 0);
    return 0;
}
