/* syslog slice: the connection's identity — openlog re-tags later
   messages, a second openlog after closelog replaces the tag, the
   LOG_PERROR flag rides through closelog, and vsyslog reached
   through a variadic wrapper prints exactly what the direct syslog
   call prints. (closelog resets the tag to the program name, which
   differs between the two sides' binaries, so the untagged default
   is deliberately not printed here.) */
#define _GNU_SOURCE
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

static void relay(int pri, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsyslog(pri, fmt, ap);
    va_end(ap);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    dup2(STDOUT_FILENO, STDERR_FILENO);

    openlog("first-tag", LOG_PERROR, LOG_DAEMON);
    syslog(LOG_NOTICE, "under the first tag");

    closelog();
    openlog("second-tag", LOG_PERROR, LOG_LOCAL0);
    syslog(LOG_NOTICE, "under the second tag");

    relay(LOG_NOTICE, "relayed %s %d", "through vsyslog", 9);
    syslog(LOG_NOTICE, "relayed %s %d", "through vsyslog", 9);

    closelog();
    return 0;
}
