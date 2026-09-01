WIP: WP-56 next increment -- live crossing for the jmp_buf frameless face.

Adds veneer/wiring/t/live-jmpbuf.c, live-jmpbuf-start.S, live-jmpbuf.sh,
proving the five jmp_buf-translating rows (setjmp/_setjmp/longjmp/
_longjmp/siglongjmp) round-trip for real against elfsysv1.dll, the way
live-math.c and live-runtime.c already proved the thunk rows in their
slices. This is the specific next step the README's "Live crossing"
section names as unattempted.
