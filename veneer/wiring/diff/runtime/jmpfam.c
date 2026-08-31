/* runtime slice: the setjmp/longjmp value protocol. setjmp answers 0
   on the direct return and longjmp's value on the jumped one, with a
   longjmp of 0 delivered as 1; a volatile local counts the hops so
   the jumps are observed to actually happen; the _setjmp twin obeys
   the same protocol; and a counted setjmp loop runs exactly three
   passes. */
#include <setjmp.h>
#include <stdio.h>

static jmp_buf env;

static void jump(int v)
{
    longjmp(env, v);
}

int main(void)
{
    volatile int hops = 0;
    volatile int phase = 0;
    volatile int pass = 0;
    jmp_buf uenv, lenv;
    int r, u, lv;

    r = setjmp(env);
    printf("setjmp returned %d, hop %d\n", r, hops);
    if (r == 0) {
        hops = hops + 1;
        jump(5);
    }
    if (r == 5) {
        hops = hops + 1;
        jump(0);        /* comes back as 1 */
    }

    u = _setjmp(uenv);
    printf("_setjmp returned %d, phase %d\n", u, phase);
    if (u == 0) {
        phase = 1;
        _longjmp(uenv, 42);
    }

    lv = setjmp(lenv);
    pass = pass + 1;
    if (lv < 2)
        longjmp(lenv, lv + 1);
    printf("loop: last value %d after %d passes\n", lv, pass);
    return 0;
}
