/*
 * WP-56 reent-tls-bringup, item 1: the real-process stub's own string and
 * parsing work, freestanding.
 *
 * A plain Microsoft-ABI call into the faced System V libc returns without
 * crossing (spike/reent-stub-realproc-window: ms_abi_libc_call_crosses=no), so
 * a real-process stub cannot do its own argument work through the faced libc.
 * These do it with no call into any libc at all -- pure over their inputs, so
 * they carry no ABI crossing and are certified natively (t/run.sh unit stage),
 * independent of the faced runtime.
 *
 * Scope is the stub's own use, not a general libc: rp_strtoull covers the
 * bases and forms loader/exec/stub.c parses its options with, no locale and no
 * errno.
 */
#ifdef ELFSYSV_REALPROC

#include <stddef.h>
#include "realproc.h"

int rp_strcmp(const char *a, const char *b)
{
while (*a && *a == *b) { a++; b++; }
return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int rp_strncmp(const char *a, const char *b, size_t n)
{
while (n && *a && *a == *b) { a++; b++; n--; }
if (!n)
return 0;
return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

size_t rp_strlen(const char *s)
{
const char *p = s;
while (*p) p++;
return (size_t)(p - s);
}

static int rp_digit(int c, int base)
{
int d;
if (c >= '0' && c <= '9') d = c - '0';
else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
else return -1;
return d < base ? d : -1;
}

unsigned long long rp_strtoull(const char *s, char **end, int base)
{
const char *p = s;
unsigned long long v = 0;
int d;
while (*p == ' ' || *p == '\t') p++;
if (base == 0) {
if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
else if (p[0] == '0') { base = 8; p++; }
else base = 10;
} else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
p += 2;
}
while ((d = rp_digit((unsigned char)*p, base)) >= 0) {
v = v * (unsigned long long)base + (unsigned long long)d;
p++;
}
if (end)
*end = (char *)p;
return v;
}

#endif /* ELFSYSV_REALPROC */
