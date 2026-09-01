/*
 * WP-56 reent-tls-bringup, item 1: the real-process stub's own formatted
 * output, freestanding.
 *
 * The stub's diagnostics -- refuse(), say(), the usage text, the dry-run
 * report -- are printf-family calls today. A plain Microsoft-ABI call into the
 * faced System V libc returns without crossing
 * (spike/reent-stub-realproc-window: ms_abi_libc_call_crosses=no), so a
 * real-process stub cannot format through the faced libc, and crossing a
 * Microsoft-ABI va_list into the faced runtime's System V vfprintf would marry
 * two register-save-area layouts that do not agree. DR-0066 drew the line
 * where realproc-str.c already draws it: the formatting is the stub's own
 * work, so it is done host-side with no call into any libc, and only the
 * finished bytes cross -- through the sysv_abi puts thunk realproc-cross.c
 * already carries (rp_puts). This is that host-side formatter.
 *
 * It is pure over its inputs -- no call into any libc, no faced runtime -- so
 * it is certified natively (t/run.sh unit stage) against the platform snprintf
 * it stands in for. Scope is the conversions loader/exec/stub.c actually
 * prints: %s, %c, %%, and the integer conversions %d/%i/%u/%x with the length
 * modifiers l and ll, which cover its %u, %lu, and the PRIx64 / PRIu64 width
 * the uint64_t addresses and sizes print at. No flags, no field width, no
 * precision -- the stub prints none, and a "0x" prefix it writes as literal
 * text, not as a %#x flag.
 */
#ifdef ELFSYSV_REALPROC

#include <stddef.h>
#include <stdarg.h>
#include "realproc.h"

/* A bounded output cursor: write up to lim bytes, but keep counting past it so
 * the return is the length a large-enough buffer would have held, as snprintf
 * promises. */
struct rp_sink {
	char  *buf;
	size_t lim;   /* bytes the buffer can hold, NUL included */
	size_t n;     /* bytes that would have been written, NUL excluded */
};

static void rp_emit(struct rp_sink *s, char c)
{
	if (s->n + 1 < s->lim)
		s->buf[s->n] = c;
	s->n++;
}

/* Emit an unsigned value in the given base (10 or 16, lowercase). */
static void rp_emit_u(struct rp_sink *s, unsigned long long v, unsigned base)
{
	char tmp[20];   /* 2^64 is 20 decimal digits; hex is shorter */
	int i = 0;
	if (v == 0) {
		rp_emit(s, '0');
		return;
	}
	while (v) {
		unsigned d = (unsigned)(v % base);
		tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
		v /= base;
	}
	while (i)
		rp_emit(s, tmp[--i]);
}

/* Emit a signed value in base 10, with a leading '-' for negatives. */
static void rp_emit_d(struct rp_sink *s, long long v)
{
	unsigned long long m;
	if (v < 0) {
		rp_emit(s, '-');
		m = (unsigned long long)(-(v + 1)) + 1ULL;   /* -LLONG_MIN safe */
	} else {
		m = (unsigned long long)v;
	}
	rp_emit_u(s, m, 10);
}

/* Length modifier the conversion carries. */
enum rp_len { RP_LEN_INT, RP_LEN_L, RP_LEN_LL };

int rp_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct rp_sink s;
	const char *p = fmt;
	s.buf = buf;
	s.lim = size;
	s.n = 0;

	while (*p) {
		enum rp_len len;
		if (*p != '%') {
			rp_emit(&s, *p++);
			continue;
		}
		p++;                    /* the '%' */
		len = RP_LEN_INT;
		while (*p == 'l') {     /* l, then ll */
			len = (len == RP_LEN_INT) ? RP_LEN_L : RP_LEN_LL;
			p++;
		}
		switch (*p) {
		case '%':
			rp_emit(&s, '%');
			break;
		case 'c':
			rp_emit(&s, (char)va_arg(ap, int));
			break;
		case 's': {
			const char *a = va_arg(ap, const char *);
			if (!a)
				a = "(null)";
			while (*a)
				rp_emit(&s, *a++);
			break;
		}
		case 'd':
		case 'i': {
			long long v = (len == RP_LEN_LL) ? va_arg(ap, long long)
			            : (len == RP_LEN_L)  ? va_arg(ap, long)
			                                 : va_arg(ap, int);
			rp_emit_d(&s, v);
			break;
		}
		case 'u':
		case 'x': {
			unsigned base = (*p == 'x') ? 16 : 10;
			unsigned long long v =
				(len == RP_LEN_LL) ? va_arg(ap, unsigned long long)
			      : (len == RP_LEN_L)  ? va_arg(ap, unsigned long)
			                           : va_arg(ap, unsigned int);
			rp_emit_u(&s, v, base);
			break;
		}
		default:
			/* An unhandled conversion is emitted verbatim, '%' and all,
			 * so a format the stub was not scoped for is visible rather
			 * than silently swallowing its argument's worth of output. */
			rp_emit(&s, '%');
			if (*p)
				rp_emit(&s, *p);
			break;
		}
		if (*p)
			p++;
	}

	if (s.lim)
		s.buf[s.n < s.lim ? s.n : s.lim - 1] = '\0';
	return (int)s.n;
}

int rp_snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = rp_vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

#endif /* ELFSYSV_REALPROC */
