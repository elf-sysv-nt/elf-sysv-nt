# gdb for the triple

WP-60. A debugger configured for `x86_64-elfsysvnt-linux-gnu`, consuming the
`r_debug` rendezvous WP-39 laid down, through the same `solib-svr4` code every
SVr4 system feeds.

    build-gdb -P <prefix>
    t/accept.sh -P <prefix>

Like binutils, there is no port. Every configuration gdb matches on is
written `x86_64-*-linux-*`, so the canonical triple lands on the ordinary
GNU/Linux target and everything WP-39 certified byte-for-byte -- `r_debug`,
the `link_map` chain, `r_brk` -- is exactly what this build walks. The
version is pinned at 13.2 rather than newest because gdb 14 raised its build
floor past the host's GCC 7.4; the pin carries that reasoning.

The host supplies neither GMP nor MPFR, so `build-gdb` builds both static
from the trees the gcc bootstrap fetched, into `$work/hostlibs`, once.
Python and guile scripting are configured off: the host has no python to
link against, and nothing in the done-when wants either.

## What the acceptance measures, and what it must defer

The done-when names a live session: break, step, locals, in an ELF program
running under the stub, and a C++ throw unwound across a shared-library
boundary. That needs a process to attach to, and the loader cannot yet run
one -- the same boundary WP-15's `accept2.sh` stopped at, recorded the same
way. `t/accept.sh` measures the debugger's whole side of it: configured for
the triple and not the host, the GNU/Linux osabi compiled in, a breakpoint
placed in a cross-built `main`, its locals read from the DWARF, and the
shared library carrying the eventual throw opened and searched. When a stub
process can be attached, the run becomes the test.

The Windows-side view stays broken by design: a debugger attached from
outside sees a PE stub and anonymous executable regions. That is Wine's
situation inverted, and owning the debug channel the way Wine does is the
remedy the roadmap names -- this package is the tool that channel will feed.
