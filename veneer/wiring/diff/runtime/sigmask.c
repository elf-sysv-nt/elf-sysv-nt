/* runtime slice: what each jump saves of the signal mask. On glibc
   setjmp and _setjmp leave the mask alone, and sigsetjmp saves it
   only when asked; each section blocks SIGUSR1 in the jumped-from
   region and reads the mask back after the return. siglongjmp's
   value protocol matches its plain sibling: a 0 comes back as 1. */
#define _GNU_SOURCE
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

static int blocked(int sig)
{
    sigset_t cur;
    sigprocmask(SIG_BLOCK, NULL, &cur);
    return sigismember(&cur, sig);
}

static void clearmask(void)
{
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
}

int main(void)
{
    sigjmp_buf senv;
    jmp_buf env;
    sigset_t one;
    int r;

    sigemptyset(&one);
    sigaddset(&one, SIGUSR1);

    clearmask();
    if (sigsetjmp(senv, 1) == 0) {
        sigprocmask(SIG_BLOCK, &one, NULL);
        printf("before siglongjmp: SIGUSR1 blocked %d\n", blocked(SIGUSR1));
        siglongjmp(senv, 1);
    }
    printf("sigsetjmp(1) restored: SIGUSR1 blocked %d\n", blocked(SIGUSR1));

    clearmask();
    if (sigsetjmp(senv, 0) == 0) {
        sigprocmask(SIG_BLOCK, &one, NULL);
        siglongjmp(senv, 1);
    }
    printf("sigsetjmp(0) kept: SIGUSR1 blocked %d\n", blocked(SIGUSR1));

    clearmask();
    if (setjmp(env) == 0) {
        sigprocmask(SIG_BLOCK, &one, NULL);
        longjmp(env, 1);
    }
    printf("setjmp kept: SIGUSR1 blocked %d\n", blocked(SIGUSR1));

    clearmask();
    if (_setjmp(env) == 0) {
        sigprocmask(SIG_BLOCK, &one, NULL);
        _longjmp(env, 1);
    }
    printf("_setjmp kept: SIGUSR1 blocked %d\n", blocked(SIGUSR1));

    clearmask();
    r = sigsetjmp(senv, 1);
    if (r == 0)
        siglongjmp(senv, 0);
    printf("siglongjmp(0) came back as %d\n", r);
    return 0;
}
