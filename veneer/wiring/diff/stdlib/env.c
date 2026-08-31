/* stdlib slice: the environment -- getenv, setenv, putenv, unsetenv,
   secure_getenv, clearenv. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char slot[] = "ESN_PUT=via-putenv";

int main(void)
{
    printf("absent %d\n", getenv("ESN_DIFF_VAR") == NULL);

    printf("setenv %d\n", setenv("ESN_DIFF_VAR", "one", 1));
    printf("get %s\n", getenv("ESN_DIFF_VAR"));
    printf("keep %d\n", setenv("ESN_DIFF_VAR", "two", 0));
    printf("kept %s\n", getenv("ESN_DIFF_VAR"));
    printf("clobber %d\n", setenv("ESN_DIFF_VAR", "three", 1));
    printf("clobbered %s\n", getenv("ESN_DIFF_VAR"));

    printf("putenv %d\n", putenv(slot));
    printf("put %s\n", getenv("ESN_PUT"));
    printf("secure %s\n", secure_getenv("ESN_PUT"));

    printf("unset %d\n", unsetenv("ESN_DIFF_VAR"));
    printf("gone %d\n", getenv("ESN_DIFF_VAR") == NULL);
    printf("unset2 %d\n", unsetenv("ESN_DIFF_VAR"));

    printf("badname %d\n", setenv("BAD=NAME", "x", 1) == -1);

    printf("clearenv %d\n", clearenv());
    printf("cleared %d\n", getenv("PATH") == NULL);
    return 0;
}
