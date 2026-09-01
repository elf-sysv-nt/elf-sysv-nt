# WIP: jmp_buf shim needs a frameless face, not a call-style C wrapper

Working notes for the increment following the buffer-identity finding
(commit 0848626). Scaffold only; the decision record and any code land in
follow-up commits on this branch.

Connects the buffer-identity finding to DR-0041 (the setjmp family takes a
frameless face at the sv2ms seam): the same "must not be wrapped in an
out-of-line call" rule applies one layer over, at the wiring shim's
glibc-to-Cygwin jmp_buf translation.
