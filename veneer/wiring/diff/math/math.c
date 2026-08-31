#define _GNU_SOURCE
/* math slice: the classification family (isnan, isinf, finite, and
   the deprecated leading-underscore spellings el8 still exports) with
   their f/l twins, copysign, frexp, ldexp, modf and scalbn with
   theirs -- the thirty-four rows this slice wires.  Every case uses a
   value with an exact binary representation so both sides print the
   same bits, not just the same rounded decimal. */
#include <math.h>
#include <stdio.h>

int main(void)
{
    /* classification: an ordinary value, infinity, nan, on each width */
    printf("isnan(nan) %d\n", isnan(0.0 / 0.0));
    printf("isnan(1.0) %d\n", isnan(1.0));
    printf("isnanf(nanf) %d\n", isnanf(0.0f / 0.0f));
    printf("isnanl(1.0l) %d\n", isnanl(1.0L));

    printf("isinf(inf) %d\n", isinf(1.0 / 0.0));
    printf("isinf(-inf) %d\n", isinf(-1.0 / 0.0));
    printf("isinf(1.0) %d\n", isinf(1.0));
    printf("isinff(inff) %d\n", isinff(1.0f / 0.0f));
    printf("isinfl(1.0l) %d\n", isinfl(1.0L));

    printf("finite(1.0) %d\n", finite(1.0));
    printf("finite(inf) %d\n", finite(1.0 / 0.0));
    printf("finitef(1.0f) %d\n", finitef(1.0f));
    printf("finitel(1.0l) %d\n", finitel(1.0L));

    /* copysign: magnitude from the first argument, sign from the second */
    printf("copysign(3,-1) %.1f\n", copysign(3.0, -1.0));
    printf("copysignf(3,-1) %.1f\n", (double) copysignf(3.0f, -1.0f));
    printf("copysignl(3,-1) %.1f\n", (double) copysignl(3.0L, -1.0L));

    /* frexp: 12.0 = 0.75 * 2^4, exact in binary */
    int e;
    double m = frexp(12.0, &e);
    printf("frexp(12) %.2f %d\n", m, e);
    int ef;
    float mf = frexpf(12.0f, &ef);
    printf("frexpf(12) %.2f %d\n", (double) mf, ef);
    int el;
    long double ml = frexpl(12.0L, &el);
    printf("frexpl(12) %.2f %d\n", (double) ml, el);

    /* ldexp: the inverse direction, also exact */
    printf("ldexp(0.75,4) %.1f\n", ldexp(0.75, 4));
    printf("ldexpf(0.75,4) %.1f\n", (double) ldexpf(0.75f, 4));
    printf("ldexpl(0.75,4) %.1f\n", (double) ldexpl(0.75L, 4));

    /* modf: 3.5 splits exactly into 3.0 and 0.5 */
    double ip;
    double fp = modf(3.5, &ip);
    printf("modf(3.5) %.1f %.1f\n", ip, fp);
    float ipf;
    float fpf = modff(3.5f, &ipf);
    printf("modff(3.5) %.1f %.1f\n", (double) ipf, (double) fpf);
    long double ipl;
    long double fpl = modfl(3.5L, &ipl);
    printf("modfl(3.5) %.1f %.1f\n", (double) ipl, (double) fpl);

    /* scalbn: 0.75 * 2^4, the same exact case as ldexp */
    printf("scalbn(0.75,4) %.1f\n", scalbn(0.75, 4));
    printf("scalbnf(0.75,4) %.1f\n", (double) scalbnf(0.75f, 4));
    printf("scalbnl(0.75,4) %.1f\n", (double) scalbnl(0.75L, 4));

    return 0;
}
