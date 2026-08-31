/* signal slice: sigaltstack -- install, query, SS_ONSTACK observed
   from inside an SA_ONSTACK handler, and the refusals for a bad
   flag word and an undersized stack. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

static volatile sig_atomic_t onstack;

static void on_usr1(int sig)
{
    stack_t cur;
    (void)sig;
    sigaltstack(NULL, &cur);
    onstack = (cur.ss_flags & SS_ONSTACK) != 0;
}

int main(void)
{
    stack_t st, q;
    struct sigaction sa;
    int rc;

    st.ss_sp = malloc(SIGSTKSZ);
    st.ss_size = SIGSTKSZ;
    st.ss_flags = 0;
    rc = sigaltstack(&st, NULL);
    printf("install %d\n", rc == 0);

    rc = sigaltstack(NULL, &q);
    printf("query %d %d %d\n", rc == 0, q.ss_sp == st.ss_sp,
           q.ss_size == (size_t)SIGSTKSZ);
    printf("idle %d\n", (q.ss_flags & SS_ONSTACK) == 0);

    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_usr1;
    sa.sa_flags = SA_ONSTACK;
    sigaction(SIGUSR1, &sa, NULL);
    raise(SIGUSR1);
    printf("onstack %d\n", onstack == 1);

    errno = 0;
    st.ss_flags = 12345;
    rc = sigaltstack(&st, NULL);
    printf("bad-flags %d %d\n", rc == -1, errno == EINVAL);

    errno = 0;
    st.ss_flags = 0;
    st.ss_size = MINSIGSTKSZ - 1;
    rc = sigaltstack(&st, NULL);
    printf("too-small %d %d\n", rc == -1, errno == ENOMEM);

    st.ss_flags = SS_DISABLE;
    st.ss_size = SIGSTKSZ;
    rc = sigaltstack(&st, NULL);
    sigaltstack(NULL, &q);
    printf("disable %d %d\n", rc == 0, (q.ss_flags & SS_DISABLE) != 0);
    return 0;
}
