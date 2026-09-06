/*
 * hello.c -- the program the bar runs.  printf rather than write, because the
 * point is that glibc's own startup, stdio and exit path reach the kernel
 * through the gate: the static build pulls in the TLS setup, the IRELATIVE
 * resolution of the string functions, malloc for the stdio buffer, and the
 * atexit flush, which is the whole of what a C program needs before main.
 */
#include <stdio.h>

int main(void)
{
	printf("hello\n");
	return 0;
}
