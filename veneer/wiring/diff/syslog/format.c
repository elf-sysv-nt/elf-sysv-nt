/* syslog slice: the LOG_PERROR text — with stderr routed onto stdout
   the message copy is observable: the ident prefixes each line, the
   printf-style conversions format as printf would, %m expands the
   errno set at the call, and the copy arrives as exactly one
   newline-terminated line whether or not the format supplied one. */
#define _GNU_SOURCE
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    dup2(STDOUT_FILENO, STDERR_FILENO);

    openlog("diffcase", LOG_PERROR, LOG_USER);
    syslog(LOG_INFO, "plain text");
    syslog(LOG_INFO, "number %d string %s char %c", 42, "forty-two", 'x');

    errno = ERANGE;
    syslog(LOG_INFO, "errno says %m");
    errno = 0;
    syslog(LOG_INFO, "errno says %m");

    syslog(LOG_INFO, "with newline\n");
    syslog(LOG_INFO, "without newline");

    printf("wide %s\n", "line");
    syslog(LOG_INFO, "padded |%8d| |%-8s|", 7, "left");

    closelog();
    return 0;
}
