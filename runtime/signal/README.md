# signals (WP-43)

WIP. The trampoline from Cygwin's thread-hijack delivery onto an ELF-side
stack: `siginfo_t` and `ucontext_t` laid out as the psABI and the Linux headers
agree, extended FPU state saved where a consumer looks for it, and a return
path that restores all of it.

The red zone is why this package is written the way it is. DR-0006 settled that
the 128 reserved bytes are honoured at the delivery site rather than compiled
around, and left the price here.
