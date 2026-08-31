/* terminal slice: the tc* control surface refusing a pipe -- every
   call answers ENOTTY on a descriptor that is not a terminal, and
   EBADF once the descriptor is gone, so the whole family is
   observable without a controlling terminal. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>

int main(void)
{
    struct termios t;
    int p[2];
    int r;

    if (pipe(p) != 0) { printf("pipe fail\n"); return 1; }

    memset(&t, 0, sizeof t);
    errno = 0; r = tcgetattr(p[0], &t);
    printf("getattr %d %d\n", r, errno == ENOTTY);
    errno = 0; r = tcsetattr(p[0], TCSANOW, &t);
    printf("setattr %d %d\n", r, errno == ENOTTY);
    errno = 0; r = tcdrain(p[1]);
    printf("drain %d %d\n", r, errno == ENOTTY);
    errno = 0; r = tcflush(p[0], TCIFLUSH);
    printf("flush %d %d\n", r, errno == ENOTTY);
    errno = 0; r = tcflow(p[1], TCOON);
    printf("flow %d %d\n", r, errno == ENOTTY);
    errno = 0; r = tcsendbreak(p[1], 0);
    printf("sendbreak %d %d\n", r, errno == ENOTTY);
    errno = 0; r = (tcgetsid(p[0]) == (pid_t)-1);
    printf("getsid %d %d\n", r, errno == ENOTTY);

    close(p[0]); close(p[1]);
    errno = 0; r = tcgetattr(p[0], &t);
    printf("gone %d %d\n", r, errno == EBADF);
    return 0;
}
