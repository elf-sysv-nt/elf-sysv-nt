/* runtime slice: the context family. Two made contexts ping-pong
   through swapcontext and the trace pins the order; makecontext's
   arguments arrive intact in the started function; falling off the
   end follows uc_link back to main; and a getcontext/setcontext
   loop reruns its region a counted three times. */
#include <ucontext.h>
#include <stdio.h>

static ucontext_t uc_main, uc_a, uc_b;
static char stack_a[64 * 1024], stack_b[64 * 1024];

static void fa(int x, int y)
{
    printf("A starts with %d %d\n", x, y);
    swapcontext(&uc_a, &uc_b);
    printf("A resumes\n");
}                               /* falls off: uc_link -> main */

static void fb(void)
{
    printf("B starts\n");
    swapcontext(&uc_b, &uc_a);
    printf("B never prints this\n");
}

int main(void)
{
    ucontext_t loop;
    volatile int n = 0;

    getcontext(&uc_a);
    uc_a.uc_stack.ss_sp = stack_a;
    uc_a.uc_stack.ss_size = sizeof stack_a;
    uc_a.uc_link = &uc_main;
    makecontext(&uc_a, (void (*)(void))fa, 2, 3, 4);

    getcontext(&uc_b);
    uc_b.uc_stack.ss_sp = stack_b;
    uc_b.uc_stack.ss_size = sizeof stack_b;
    uc_b.uc_link = &uc_main;
    makecontext(&uc_b, fb, 0);

    swapcontext(&uc_main, &uc_a);
    printf("main resumes\n");

    getcontext(&loop);
    n = n + 1;
    printf("pass %d\n", n);
    if (n < 3)
        setcontext(&loop);
    printf("loop left after %d passes\n", n);
    return 0;
}
