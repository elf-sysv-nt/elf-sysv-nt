/*
 * WP-56 item 1 unit stage: the freestanding string and parsing primitives are
 * pure over their inputs, so they are certified here natively -- no faced
 * runtime, no crossing -- against known results and against the platform libc
 * they stand in for. Built with -DELFSYSV_REALPROC so realproc-str.c compiles.
 */
#ifndef ELFSYSV_REALPROC
#define ELFSYSV_REALPROC
#endif
#include "../realproc.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;

static void ck(int cond, const char *what)
{
if (!cond) { printf("FAIL %s\n", what); fails++; }
}

/* strcmp/strncmp return a sign, not a magnitude; compare signs to the libc. */
static int sgn(int x) { return (x > 0) - (x < 0); }

int main(void)
{
ck(sgn(rp_strcmp("", "")) == sgn(strcmp("", "")), "strcmp empty");
ck(sgn(rp_strcmp("abc", "abc")) == 0, "strcmp equal");
ck(sgn(rp_strcmp("abc", "abd")) == sgn(strcmp("abc", "abd")), "strcmp lt");
ck(sgn(rp_strcmp("abd", "abc")) == sgn(strcmp("abd", "abc")), "strcmp gt");
ck(sgn(rp_strcmp("ab", "abc")) == sgn(strcmp("ab", "abc")), "strcmp prefix");
ck(sgn(rp_strcmp("--version", "--version")) == 0, "strcmp option");

ck(rp_strncmp("--stack=8", "--stack=", 8) == 0, "strncmp option prefix");
ck(rp_strncmp("--stackX", "--stack=", 8) != 0, "strncmp option differ");
ck(rp_strncmp("", "", 4) == 0, "strncmp empty");

ck(rp_strlen("") == 0, "strlen empty");
ck(rp_strlen("elfsysv-stub 1.0") == strlen("elfsysv-stub 1.0"), "strlen release");

char *e;
ck(rp_strtoull("0", &e, 0) == 0 && *e == 0, "strtoull 0");
ck(rp_strtoull("0x800000", &e, 0) == 0x800000ULL && *e == 0, "strtoull hex auto");
ck(rp_strtoull("0x10000", &e, 16) == 0x10000ULL, "strtoull hex explicit");
ck(rp_strtoull("010", &e, 0) == 8ULL, "strtoull octal auto");
ck(rp_strtoull("4096", &e, 0) == 4096ULL, "strtoull decimal");
ck(rp_strtoull("0xdeadBEEF", &e, 0) == 0xdeadbeefULL, "strtoull hex mixed case");
ck(rp_strtoull("16k", &e, 0) == 16ULL && *e == 'k', "strtoull stops at nondigit");
ck(rp_strtoull("  0x20 ", &e, 0) == 0x20ULL && *e == ' ', "strtoull skips ws");
/* Match the platform strtoull on the forms the stub actually passes. */
ck(rp_strtoull("0x800000", 0, 0) == strtoull("0x800000", 0, 0), "strtoull vs libc");

if (fails) { printf("unit: %d FAILED\n", fails); return 1; }
printf("unit: all primitives OK\n");
return 0;
}
