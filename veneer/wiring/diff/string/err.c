/* string slice: errno spellings and the strerror family, by name.
 *
 * tok.c wrote the errnums as bare Linux values while errno.h could not
 * be included; this case spells them symbolically, so the constants the
 * sysroot headers hand a package are compared against el8's, and takes
 * the aliased pairs the xlat generator singles out (EDEADLK/EDEADLOCK,
 * ENOTSUP/EOPNOTSUPP, EAGAIN/EWOULDBLOCK) through the same door.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char buf[128];

    printf("EINVAL %d\n", EINVAL);
    printf("ENOENT %d\n", ENOENT);
    printf("ENOSYS %d\n", ENOSYS);
    printf("ELOOP %d\n", ELOOP);
    printf("aliased %d %d %d\n", EDEADLK == EDEADLOCK,
           ENOTSUP == EOPNOTSUPP, EAGAIN == EWOULDBLOCK);

    printf("strerror %s\n", strerror(EINVAL));
    printf("strerror %s\n", strerror(ENOSYS));
    printf("strerror_r %s\n", strerror_r(ENOENT, buf, sizeof buf));

    errno = ERANGE;
    printf("errno %d\n", errno);
    errno = 0;
    printf("errno %d\n", errno);
    return 0;
}
