/*
 * below-line-glue.c -- a fixture, not a build input.  It is shaped like a
 * host-glue file: it names NT primitives freely, because it is the band the
 * core stands on, and it says so in its head.  substrate-line: below
 * check-substrate-line reads that declaration and walks past the file; the
 * same text without it is leaky-core.c's first three leaks.
 */
#include <windows.h>

HANDLE glue_open_console(void)
{
	return GetStdHandle(STD_OUTPUT_HANDLE);
}

long glue_query(HANDLE h, void *buf, unsigned long len)
{
	return NtQueryInformationFile(h, 0, buf, len, 5);
}
