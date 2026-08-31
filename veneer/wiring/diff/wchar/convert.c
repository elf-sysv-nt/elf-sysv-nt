/* wchar slice: the conversion state machine in the C locale —
   btowc/wctob agree on ASCII and refuse 0x80, mbrtowc/wcrtomb round
   a character trip with a clean state, mbrlen counts it, the string
   converters walk a whole string both ways, and the C11 pair
   (mbrtoc16/c16rtomb, mbrtoc32/c32rtomb) rounds the same trip. */
#include <wchar.h>
#include <uchar.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(void)
{
    mbstate_t st;
    wchar_t wc;
    char16_t c16;
    char32_t c32;
    char buf[MB_LEN_MAX];
    wchar_t wbuf[16];
    char nbuf[16];
    const char *src;

    setlocale(LC_ALL, "C");

    printf("btowc A %d\n", btowc('A') == L'A');
    printf("btowc EOF %d\n", btowc(EOF) == WEOF);
    printf("wctob A %d\n", wctob(L'A') == 'A');

    memset(&st, 0, sizeof st);
    printf("initial state %d\n", mbsinit(&st) != 0);
    printf("mbrtowc x %d\n", (int)mbrtowc(&wc, "x", 1, &st));
    printf("round wc %d\n", wc == L'x');
    printf("wcrtomb back %d\n", (int)wcrtomb(buf, wc, &st));
    printf("round mb %d\n", buf[0] == 'x');

    memset(&st, 0, sizeof st);
    printf("mbrlen x %d\n", (int)mbrlen("x", 1, &st));
    errno = 0;
    memset(&st, 0, sizeof st);
    printf("mbrtowc 0x80 fails %d\n",
           mbrtowc(&wc, "\x80", 1, &st) == (size_t)-1 && errno == EILSEQ);

    src = "wide";
    memset(&st, 0, sizeof st);
    printf("mbsrtowcs %d\n", (int)mbsrtowcs(wbuf, &src, 16, &st));
    printf("as wide %d\n", wcscmp(wbuf, L"wide") == 0);
    {
        const wchar_t *wsrc = wbuf;
        memset(&st, 0, sizeof st);
        printf("wcsrtombs %d\n", (int)wcsrtombs(nbuf, &wsrc, 16, &st));
        printf("as narrow %d\n", strcmp(nbuf, "wide") == 0);
    }
    src = "wide";
    memset(&st, 0, sizeof st);
    printf("mbsnrtowcs 2 %d\n", (int)mbsnrtowcs(wbuf, &src, 2, 16, &st));
    {
        const wchar_t *wsrc = L"wide";
        memset(&st, 0, sizeof st);
        printf("wcsnrtombs 2 %d\n", (int)wcsnrtombs(nbuf, &wsrc, 2, 16, &st));
    }

    memset(&st, 0, sizeof st);
    printf("mbrtoc16 y %d\n", (int)mbrtoc16(&c16, "y", 1, &st));
    printf("c16 value %d\n", c16 == u'y');
    memset(&st, 0, sizeof st);
    printf("c16rtomb back %d\n", (int)c16rtomb(buf, u'y', &st));
    printf("c16 mb %d\n", buf[0] == 'y');
    memset(&st, 0, sizeof st);
    printf("mbrtoc32 z %d\n", (int)mbrtoc32(&c32, "z", 1, &st));
    printf("c32 value %d\n", c32 == U'z');
    memset(&st, 0, sizeof st);
    printf("c32rtomb back %d\n", (int)c32rtomb(buf, U'z', &st));
    printf("c32 mb %d\n", buf[0] == 'z');
    return 0;
}
