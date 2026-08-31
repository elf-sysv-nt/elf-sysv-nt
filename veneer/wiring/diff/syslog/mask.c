/* syslog slice: the setlogmask contract — the initial mask admits
   every priority, LOG_MASK and LOG_UPTO build the documented bit
   patterns, every call returns the mask it replaced, and an argument
   of zero reads the mask back without changing it. */
#define _GNU_SOURCE
#include <syslog.h>
#include <stdio.h>

int main(void)
{
    int prev;

    prev = setlogmask(0);
    printf("initial mask %#x\n", prev);
    printf("mask emerg %#x\n", LOG_MASK(LOG_EMERG));
    printf("mask debug %#x\n", LOG_MASK(LOG_DEBUG));
    printf("upto warning %#x\n", LOG_UPTO(LOG_WARNING));
    printf("upto debug %#x\n", LOG_UPTO(LOG_DEBUG));

    prev = setlogmask(LOG_MASK(LOG_ERR));
    printf("first swap returned %#x\n", prev);
    prev = setlogmask(LOG_UPTO(LOG_INFO));
    printf("second swap returned %#x\n", prev);
    printf("read-back %#x\n", setlogmask(0));
    printf("read-back changed nothing %d\n",
           setlogmask(0) == LOG_UPTO(LOG_INFO));

    prev = setlogmask(LOG_UPTO(LOG_DEBUG));
    printf("restore returned %#x\n", prev);
    return 0;
}
