/* WP-24: walk a System V va_list against a printf/scanf format and rebuild it
 * as a Microsoft va_list. See sv2ms.h for why this exists and cannot be a
 * forward.
 *
 * The layout target is the Microsoft x64 argument home area: a run of
 * eight-byte slots, one per scalar, read back by __builtin_ms_va_arg in the
 * core. So the walk is: for each conversion the format names, pull the
 * argument from the System V list with its true type -- which is what tells
 * gcc's __builtin_va_arg whether to take it from the integer save area or the
 * floating one -- and store its value into the next slot. An integer or
 * pointer is stored as its sixty-four-bit value; a double is stored as its
 * eight raw bytes. Both are exactly what the Microsoft core will read back.
 *
 * This file is compiled to the runtime's own ABI. The System V semantics come
 * from the __sysv_va_list type carried on ap, not from the ABI of the walker;
 * the register save area was already populated by the sysv_va_start in the
 * entry point that owns ap.
 */
#include "sv2ms.h"

#include <stdint.h>
#include <string.h>

/* One format character, narrow or wide, as an int for ASCII comparison. Every
 * character of printf/scanf conversion syntax is ASCII, so a wide format is
 * read the same way once each wchar_t is widened. */
static int fmtch(const void *fmt, int wide, size_t i)
{
	if (wide)
		return (int)((const wchar_t *)fmt)[i];
	return (int)(unsigned char)((const char *)fmt)[i];
}

static int is_flag(int c)  { return c == '-' || c == '+' || c == ' ' || c == '#' || c == '0' || c == '\''; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }

/* length modifier, decoded to a small rank: 0 int, 1 long, 2 long long,
 * -1 short/char (still an int argument), 3 long double, 4 the float forms
 * (double argument). j/z/t are long-long-width here, which is correct on
 * LP64. */
enum { LEN_INT = 0, LEN_LONG = 1, LEN_LL = 2, LEN_LDBL = 3 };

static void push_u64(unsigned long long *slots, int *n, int max, unsigned long long v)
{
	if (*n < max)
		slots[*n] = v;
	(*n)++;
}

static void push_double(unsigned long long *slots, int *n, int max, double d)
{
	unsigned long long v;
	memcpy(&v, &d, sizeof v);
	push_u64(slots, n, max, v);
}

/* The printf walk. Width and precision '*' each consume an int argument; then
 * the conversion consumes its own, whose width the length modifier sets. */
static va_list build_print(unsigned long long *slots, int max,
			   const void *fmt, int wide, __sysv_va_list ap)
{
	int n = 0;
	size_t i = 0;
	int c;

	while ((c = fmtch(fmt, wide, i)) != 0) {
		i++;
		if (c != '%')
			continue;
		if (fmtch(fmt, wide, i) == '%') { i++; continue; }

		while (is_flag(fmtch(fmt, wide, i)))
			i++;
		if (fmtch(fmt, wide, i) == '*') {
			push_u64(slots, &n, max, (uint64_t)(int64_t)__sysv_va_arg(ap, int));
			i++;
		} else {
			while (is_digit(fmtch(fmt, wide, i)))
				i++;
		}
		if (fmtch(fmt, wide, i) == '.') {
			i++;
			if (fmtch(fmt, wide, i) == '*') {
				push_u64(slots, &n, max, (uint64_t)(int64_t)__sysv_va_arg(ap, int));
				i++;
			} else {
				while (is_digit(fmtch(fmt, wide, i)))
					i++;
			}
		}

		int len = LEN_INT;
		for (;;) {
			int m = fmtch(fmt, wide, i);
			if (m == 'l') { len = (len == LEN_LONG) ? LEN_LL : LEN_LONG; i++; }
			else if (m == 'h') { i++; /* short/char: still an int argument */ }
			else if (m == 'j' || m == 'z' || m == 't' || m == 'q') { len = LEN_LL; i++; }
			else if (m == 'L') { len = LEN_LDBL; i++; }
			else break;
		}

		c = fmtch(fmt, wide, i);
		if (c != 0)
			i++;
		switch (c) {
		case 'd': case 'i':
			if (len >= LEN_LL)
				push_u64(slots, &n, max, (uint64_t)__sysv_va_arg(ap, long long));
			else if (len == LEN_LONG)
				push_u64(slots, &n, max, (uint64_t)(long long)__sysv_va_arg(ap, long));
			else
				push_u64(slots, &n, max, (uint64_t)(int64_t)__sysv_va_arg(ap, int));
			break;
		case 'u': case 'o': case 'x': case 'X': case 'b':
			if (len >= LEN_LL)
				push_u64(slots, &n, max, (uint64_t)__sysv_va_arg(ap, unsigned long long));
			else if (len == LEN_LONG)
				push_u64(slots, &n, max, (uint64_t)__sysv_va_arg(ap, unsigned long));
			else
				push_u64(slots, &n, max, (uint64_t)__sysv_va_arg(ap, unsigned int));
			break;
		case 'c':
			/* %lc takes a wint_t, still an int-width argument. */
			push_u64(slots, &n, max, (uint64_t)(unsigned)__sysv_va_arg(ap, int));
			break;
		case 'f': case 'F': case 'e': case 'E':
		case 'g': case 'G': case 'a': case 'A':
			if (len == LEN_LDBL) {
				/* long double is where the copy breaks: System V passes
				 * an eighty-bit value in sixteen bytes and Microsoft a
				 * sixty-four-bit one. Nothing here can convert the value,
				 * so this pulls the argument to keep the walk aligned and
				 * narrows it; a format that needs exact long double is
				 * outside what a value copy can carry. See the README. */
				long double ld = __sysv_va_arg(ap, long double);
				push_double(slots, &n, max, (double)ld);
			} else {
				push_double(slots, &n, max, __sysv_va_arg(ap, double));
			}
			break;
		case 's': case 'p': case 'n':
			push_u64(slots, &n, max, (uint64_t)(uintptr_t)__sysv_va_arg(ap, void *));
			break;
		default:
			/* unknown conversion: consume nothing and let the core render
			 * it verbatim, which is what a lone stray '%x'-less '%' does. */
			break;
		}
		if (n >= max)
			break;
	}
	return (va_list)(void *)slots;
}

/* The scanf walk. Every conversion that stores takes one pointer; an
 * assignment-suppressing '*' takes none, '%%' takes none, and a width is a
 * literal rather than an argument. So the layout is one pointer slot per
 * non-suppressed conversion. */
static va_list build_scan(unsigned long long *slots, int max,
			  const void *fmt, int wide, __sysv_va_list ap)
{
	int n = 0;
	size_t i = 0;
	int c;

	while ((c = fmtch(fmt, wide, i)) != 0) {
		i++;
		if (c != '%')
			continue;
		if (fmtch(fmt, wide, i) == '%') { i++; continue; }

		int suppress = 0;
		if (fmtch(fmt, wide, i) == '*') { suppress = 1; i++; }
		while (is_digit(fmtch(fmt, wide, i)))
			i++;
		while (fmtch(fmt, wide, i) == 'l' || fmtch(fmt, wide, i) == 'h' ||
		       fmtch(fmt, wide, i) == 'L' || fmtch(fmt, wide, i) == 'j' ||
		       fmtch(fmt, wide, i) == 'z' || fmtch(fmt, wide, i) == 't' ||
		       fmtch(fmt, wide, i) == 'q')
			i++;

		c = fmtch(fmt, wide, i);
		if (c == '[') {
			/* a scan set: skip to its closing ']', minding a leading ']'. */
			i++;
			if (fmtch(fmt, wide, i) == '^') i++;
			if (fmtch(fmt, wide, i) == ']') i++;
			while ((c = fmtch(fmt, wide, i)) != 0 && c != ']')
				i++;
			if (c == ']') i++;
			if (!suppress)
				push_u64(slots, &n, max, (uint64_t)(uintptr_t)__sysv_va_arg(ap, void *));
			if (n >= max) break;
			continue;
		}
		if (c != 0)
			i++;
		if (c == 0)
			break;
		/* every remaining conversion stores through one pointer. */
		if (!suppress)
			push_u64(slots, &n, max, (uint64_t)(uintptr_t)__sysv_va_arg(ap, void *));
		if (n >= max)
			break;
	}
	return (va_list)(void *)slots;
}

va_list __sv2ms_print(unsigned long long *slots, int max, const char *fmt, __sysv_va_list ap)
{
	return build_print(slots, max, fmt, 0, ap);
}

va_list __sv2ms_wprint(unsigned long long *slots, int max, const wchar_t *fmt, __sysv_va_list ap)
{
	return build_print(slots, max, fmt, 1, ap);
}

va_list __sv2ms_scan(unsigned long long *slots, int max, const char *fmt, __sysv_va_list ap)
{
	return build_scan(slots, max, fmt, 0, ap);
}

va_list __sv2ms_wscan(unsigned long long *slots, int max, const wchar_t *fmt, __sysv_va_list ap)
{
	return build_scan(slots, max, fmt, 1, ap);
}
