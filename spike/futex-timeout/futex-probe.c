/* futex-probe: how late does RtlWaitOnAddress wake from a timeout, and what
 * does raising the timer resolution do about it?
 *
 * FUTEX_WAIT with a timeout is RtlWaitOnAddress under N (0011 § 6) and the
 * same NT wait sits beneath H's in-process futex. Linux wakes on an hrtimer,
 * a few microseconds late. NT wakes on the clock interrupt, whose period is
 * whatever the highest-resolution requester on the machine asked for, 15.6
 * ms by default. 0012 § 9 says the kernel raises the resolution at start;
 * this measures what a timed wait costs before and after it does.
 *
 * Native, mingw. Every wait is on a word nobody wakes, so the timeout is the
 * only way out and the lateness is the whole measurement.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#define RELEASE "futex-probe 1.0"

typedef NTSTATUS (NTAPI *fn_WaitOnAddress)(volatile VOID *, PVOID, SIZE_T, PLARGE_INTEGER);
typedef NTSTATUS (NTAPI *fn_SetTimerResolution)(ULONG, BOOLEAN, PULONG);
typedef NTSTATUS (NTAPI *fn_QueryTimerResolution)(PULONG, PULONG, PULONG);
typedef NTSTATUS (NTAPI *fn_DelayExecution)(BOOLEAN, PLARGE_INTEGER);

static fn_WaitOnAddress p_wait;
static fn_SetTimerResolution p_setres;
static fn_QueryTimerResolution p_queryres;
static fn_DelayExecution p_delay;
static LARGE_INTEGER freq;

static void emit(const char *k, const char *fmt, ...)
{
	va_list ap;
	printf("%s=", k);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

static uint64_t now_ns(void)
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return (uint64_t) ((double) t.QuadPart * 1e9 / (double) freq.QuadPart);
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *) a, y = *(const uint64_t *) b;
	return x < y ? -1 : x > y ? 1 : 0;
}

/* n timed waits of `want_us`; the lateness of each in ns. */
static void measure(const char *tag, unsigned want_us, int n, int use_delay)
{
	uint64_t *late = malloc((size_t) n * sizeof *late), t0, t1;
	volatile LONG word = 0, expect = 0;
	LARGE_INTEGER to;
	int i, early = 0;
	char key[80];

	to.QuadPart = -(LONGLONG) want_us * 10;      /* relative, 100 ns units */
	for (i = 0; i < n; i++) {
		t0 = now_ns();
		if (use_delay)
			p_delay(FALSE, &to);
		else
			p_wait(&word, (PVOID) &expect, sizeof word, &to);
		t1 = now_ns();
		if (t1 - t0 < (uint64_t) want_us * 1000) { early++; late[i] = 0; }
		else late[i] = (t1 - t0) - (uint64_t) want_us * 1000;
	}
	qsort(late, (size_t) n, sizeof *late, cmp_u64);
#define K(s) (snprintf(key, sizeof key, "%s_%s", tag, s), key)
	emit(K("wanted_us"), "%u", want_us);
	emit(K("late_median_ns"), "%llu", (unsigned long long) late[n / 2]);
	emit(K("late_p99_ns"), "%llu", (unsigned long long) late[(n * 99) / 100]);
	emit(K("late_max_ns"), "%llu", (unsigned long long) late[n - 1]);
	emit(K("early"), "%d", early);
#undef K
	free(late);
}

int main(int argc, char **argv)
{
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	ULONG minr = 0, maxr = 0, curr = 0, got = 0;
	int i, n = 200;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		if (!strcmp(argv[i], "--iterations") && i + 1 < argc) { n = atoi(argv[++i]); continue; }
		fprintf(stderr, "futex-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);
	QueryPerformanceFrequency(&freq);
	p_wait = (fn_WaitOnAddress)(void *) GetProcAddress(nt, "RtlWaitOnAddress");
	p_setres = (fn_SetTimerResolution)(void *) GetProcAddress(nt, "NtSetTimerResolution");
	p_queryres = (fn_QueryTimerResolution)(void *) GetProcAddress(nt, "NtQueryTimerResolution");
	p_delay = (fn_DelayExecution)(void *) GetProcAddress(nt, "NtDelayExecution");
	emit("entrypoints", "%d", (p_wait && p_setres && p_queryres && p_delay) ? 1 : 0);
	if (!(p_wait && p_setres && p_queryres && p_delay)) return 1;

	/* q1. The clock as found: whatever the loudest process on the machine
	 * has asked for. */
	p_queryres(&maxr, &minr, &curr);
	emit("q1_resolution_max_100ns", "%lu", (unsigned long) maxr);
	emit("q1_resolution_min_100ns", "%lu", (unsigned long) minr);
	emit("q1_resolution_current_100ns", "%lu", (unsigned long) curr);

	/* q2. Timed waits at the resolution found. */
	measure("q2_default_100us", 100, n, 0);
	measure("q2_default_1ms", 1000, n, 0);
	measure("q2_default_10ms", 10000, n, 0);

	/* q3. Raise it to the finest the kernel offers, and measure again. */
	emit("q3_set_status", "0x%08lx", (unsigned long) p_setres(minr, TRUE, &got));
	emit("q3_resolution_granted_100ns", "%lu", (unsigned long) got);
	measure("q3_raised_100us", 100, n, 0);
	measure("q3_raised_1ms", 1000, n, 0);
	measure("q3_raised_10ms", 10000, n, 0);

	/* q4. NtDelayExecution beside it, the nanosleep path, at the raised
	 * resolution: is the wait object the limit, or the clock. */
	measure("q4_delay_raised_100us", 100, n, 1);
	measure("q4_delay_raised_1ms", 1000, n, 1);

	/* q5. Give it back and confirm the clock returns to what it was. */
	p_setres(0, FALSE, &got);
	p_queryres(&maxr, &minr, &curr);
	emit("q5_resolution_after_release_100ns", "%lu", (unsigned long) curr);
	return 0;
}
