/* syslog slice: the mask gates emission — a priority outside the
   mask produces nothing at all on the LOG_PERROR path, LOG_UPTO
   admits everything at or above the named severity, the facility
   bits in the priority argument play no part in the masking, and
   widening the mask back readmits what was dropped. */
#define _GNU_SOURCE
#include <syslog.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    dup2(STDOUT_FILENO, STDERR_FILENO);

    openlog("filtered", LOG_PERROR, LOG_USER);

    setlogmask(LOG_UPTO(LOG_WARNING));
    syslog(LOG_ERR, "err passes");
    syslog(LOG_WARNING, "warning passes");
    syslog(LOG_NOTICE, "notice dropped");
    syslog(LOG_INFO, "info dropped");
    syslog(LOG_DEBUG, "debug dropped");
    printf("after upto-warning burst\n");

    setlogmask(LOG_MASK(LOG_INFO));
    syslog(LOG_INFO | LOG_LOCAL3, "facility bits ignored by the mask");
    syslog(LOG_ERR, "err dropped by an info-only mask");
    printf("after info-only burst\n");

    setlogmask(LOG_UPTO(LOG_DEBUG));
    syslog(LOG_DEBUG, "debug readmitted");
    printf("after widening\n");

    closelog();
    return 0;
}
