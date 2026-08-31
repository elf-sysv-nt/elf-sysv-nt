# DR-XXXX — main returns through exit, not _exit

## Context

crt1 and Scrt1 ended a program by handing main's return value to `_exit`,
with a comment noting that `__libc_start_main` was the eventual door once
WP-53 existed. The first string-slice differential (WP-56) made the cost
observable: a cross-built binary run on the el8 reference image with stdout
redirected printed nothing at all. glibc's startup returns from main into
`exit`, which walks the atexit chain and flushes stdio; `_exit` does
neither, and a full pipe buffer died with the process.

## Decision

The startup files call `exit` with main's return value. `exit` is glibc's
own — above the floor, el8's file, unchanged — so the atexit chain and the
stdio flush are the reference behaviour by construction. `_exit` remains
what `exit` eventually reaches, on the platform as on Linux.

The csu spike scaffold (`t/exit-spike2.S`) aliases `exit` to its `_exit`,
because the spike links no stdio and owns no atexit chain; the alias keeps
WP-14's exit criterion — a static image links and the spike 2 stub runs
it — intact without pretending the scaffold is a runtime.

## Consequences

Every program's buffered output now survives redirection, which is the
difference between the per-slice differential judging string functions and
it judging the crt. Full adoption of `__libc_start_main` in the startup
files stays open; when it lands, the call to `exit` moves into it rather
than disappearing.
