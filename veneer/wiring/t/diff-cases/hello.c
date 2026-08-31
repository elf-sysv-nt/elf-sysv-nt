/* Harness self-test case: observable lines any C library must agree on. */
#include <stdio.h>
#include <string.h>

int main(void)
{
	printf("strlen=%zu\n", strlen("elf-sysv-nt"));
	printf("fmt=%08x %+d % 5.2f\n", 0xbeefu, 42, 3.14159);
	puts("done");
	return 0;
}
