/* sysv-ipc slice: the ftok contract — the same path and project id
   name the same key every time, only the low byte of the project id
   participates, different low bytes name different keys, and a path
   that does not exist fails with ENOENT.  The key values themselves
   are inode-derived and unprintable across runs; the facts printed
   are the relations between them. */
#define _GNU_SOURCE
#include <sys/ipc.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *path = "/etc/passwd";
    key_t a, b, c, d;

    a = ftok(path, 'a');
    b = ftok(path, 'a');
    printf("valid key %d\n", a != (key_t)-1);
    printf("stable %d\n", a == b);

    c = ftok(path, 'a' | 0x100);
    printf("low byte only %d\n", a == c);

    d = ftok(path, 'b');
    printf("proj distinguishes %d\n", a != d);

    errno = 0;
    a = ftok("/no/such/path/anywhere", 'a');
    printf("missing path %d %s\n", a == (key_t)-1, strerror(errno));

    errno = 0;
    a = ftok(path, 0);
    printf("zero proj gives %d with errno %d\n", a != (key_t)-1, errno);
    return 0;
}
