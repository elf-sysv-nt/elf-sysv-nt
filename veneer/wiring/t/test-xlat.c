/* Spot checks on the generated translation core: one known divergence
 * per direction, the aliasing resolutions, and the pass-through rule.
 * Values are read from WP-55's committed tables, not from any header. */
#include <assert.h>
#include "../xlat-core.gen.h"

int main(void)
{
    /* the shared low range is identity */
    assert(__esn_errno_up(13) == 13);       /* EACCES */
    assert(__esn_errno_down(13) == 13);

    /* a known divergence crosses both ways: EADDRINUSE 98 / 112 */
    assert(__esn_errno_up(112) == 98);
    assert(__esn_errno_down(98) == 112);

    /* linux aliases that cygwin keeps apart: both come up as 35,
     * and 35 goes down as EDEADLK's 45; ENOTSUP resolves to 95 */
    assert(__esn_errno_up(45) == 35);       /* EDEADLK */
    assert(__esn_errno_up(56) == 35);       /* EDEADLOCK */
    assert(__esn_errno_down(35) == 45);
    assert(__esn_errno_down(95) == 95);     /* EOPNOTSUPP over ENOTSUP */
    assert(__esn_errno_up(134) == 95);      /* ENOTSUP */

    /* signals: SIGBUS is 7 upstairs and 10 downstairs */
    assert(__esn_signal_up(10) == 7);
    assert(__esn_signal_down(7) == 10);
    assert(__esn_signal_up(6) == 6);        /* SIGABRT */

    /* values no row claims pass through unchanged */
    assert(__esn_errno_up(0) == 0);
    assert(__esn_errno_up(-1) == -1);
    assert(__esn_errno_up(9999) == 9999);
    assert(__esn_signal_down(0) == 0);

    return 0;
}
