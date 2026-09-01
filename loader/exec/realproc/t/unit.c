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

/* rp_snprintf stands in for the platform snprintf: same bytes, same return.
 * ckf holds one formatting to the platform's; CKF runs the identical format
 * and arguments through both so the case cannot drift between them. */
static void ckf(const char *got, const char *want, int gn, int wn,
                const char *what)
{
	if (strcmp(got, want) != 0 || gn != wn) {
		printf("FAIL %s: got \"%s\"(%d) want \"%s\"(%d)\n",
		       what, got, gn, want, wn);
		fails++;
	}
}

#define CKF(what, ...) do {                                      \
	char a_[128], b_[128];                                   \
	int ra_ = rp_snprintf(a_, sizeof a_, __VA_ARGS__);       \
	int rb_ = snprintf(b_, sizeof b_, __VA_ARGS__);          \
	ckf(a_, b_, ra_, rb_, (what));                           \
} while (0)

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

/* rp_snprintf against the platform, over the conversions the stub prints. */
CKF("literal", "no percents here");
CKF("percent", "100%% done");
CKF("s", "%s", "elfsysv-stub");
CKF("s two", "%s: %s", "path", "message");
CKF("c", "%c", 'Q');
CKF("d", "%d", -5);
CKF("d zero", "%d", 0);
CKF("d min", "%d", -2147483647 - 1);
CKF("u", "%u", 4096u);
CKF("u max", "%u", 4294967295u);
CKF("lu", "%lu", 0x800000UL);
CKF("llu", "%llu", 12345678901234ULL);
CKF("x", "%x", 0xdeadbeefu);
CKF("lx", "%lx", 0x100400000UL);
CKF("llx", "%llx", 0xffffffffffffffffULL);
/* The two shapes the dry-run and diagnostics actually emit: a literal "0x"
 * before a uint64_t at PRIx64, and a uint64_t at PRIu64. */
CKF("prix64 shape", "stub_window_base=0x%llx", (unsigned long long)0x100000000ULL);
CKF("priu64 shape", "stub_argc=%llu", (unsigned long long)3ULL);
CKF("mixed", "%s at 0x%lx: %s", "elf_map", 0x1234UL, "bad");

/* A NULL %s prints "(null)" without touching the libc, and size 0 formats
 * nothing but still returns the length, as snprintf promises. */
{
	char a[16];
	ck(rp_snprintf(a, sizeof a, "%s", (char *)0) == 6
	   && strcmp(a, "(null)") == 0, "snprintf null");
	ck(rp_snprintf(0, 0, "%s", "abcd") == 4, "snprintf size 0 return");
}
/* Truncation: fill exactly size-1 bytes, NUL-terminate, return the full
 * length -- matching the platform on the same overflowing format. */
{
	static const char src[] = "abcdefghij";
	const char *volatile big = src;   /* hide the length from -Wformat-truncation */
	char a[8], b[8];
	int ra = rp_snprintf(a, sizeof a, "%s", big);
	int rb = snprintf(b, sizeof b, "%s", big);
	ckf(a, b, ra, rb, "snprintf truncation");
}

if (fails) { printf("unit: %d FAILED\n", fails); return 1; }
printf("unit: all primitives OK\n");
return 0;
}
