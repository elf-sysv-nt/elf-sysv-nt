/* terminal slice: the cf* family over a zeroed struct termios -- no
   terminal needed. Speed setters round-trip through the getters,
   cfsetspeed feeds both directions at once, cfmakeraw's edits are
   observed as flag facts, and a made-up speed is refused EINVAL. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <termios.h>

int main(void)
{
    struct termios t;
    int r;

    memset(&t, 0, sizeof t);
    r = cfsetospeed(&t, B9600);
    printf("oset %d %d\n", r, cfgetospeed(&t) == B9600);
    r = cfsetispeed(&t, B19200);
    printf("iset %d %d\n", r, cfgetispeed(&t) == B19200);

    r = cfsetspeed(&t, B38400);
    printf("both %d %d %d\n", r,
           cfgetospeed(&t) == B38400, cfgetispeed(&t) == B38400);

    /* B0 on input means follow the output speed; the setter takes it. */
    r = cfsetispeed(&t, B0);
    printf("izero %d %d\n", r, cfgetispeed(&t) == B0);

    errno = 0;
    r = cfsetospeed(&t, 010017 + 1);
    printf("badspeed %d %d\n", r, errno == EINVAL);

    memset(&t, 0, sizeof t);
    t.c_lflag = ICANON | ECHO | ISIG;
    t.c_oflag = OPOST;
    t.c_iflag = IXON | ICRNL | ISTRIP;
    t.c_cflag = CSIZE | PARENB;
    cfmakeraw(&t);
    printf("raw %d %d %d %d %d %d\n",
           (t.c_lflag & (ICANON | ECHO | ISIG)) == 0,
           (t.c_oflag & OPOST) == 0,
           (t.c_iflag & (IXON | ICRNL | ISTRIP)) == 0,
           (t.c_cflag & CSIZE) == CS8,
           (t.c_cflag & PARENB) == 0,
           t.c_cc[VMIN] == 1 && t.c_cc[VTIME] == 0);
    return 0;
}
