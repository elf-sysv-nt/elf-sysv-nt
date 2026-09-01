/* WP-41: the low window.
 *
 * A non-PIE ELF image is linked at a fixed address -- 0x400000 for everything
 * the x86-64 toolchains emit -- and there is exactly one place it can be
 * mapped. Windows satisfies an allocation that names no base out of the lowest
 * free region, so every allocation a process makes before the ELF world is
 * reserved is an allocation made out of where the ELF world has to live.
 * Spike 2 measured that on 2026-08-29: a 4 MB span at 0x400000 was refused
 * twenty times in twenty from inside a running Cygwin process, and the same
 * image at 0x10000000 was accepted.
 *
 * So the stub reserves the window first, before it does anything else, and
 * holds it until the loader is ready to place an image in it. This header is
 * that reservation. It is the only part of the loader that has to run early;
 * everything else may run whenever it likes.
 *
 * Handing the window over is the awkward part, and DR-0028 records why it is
 * shaped this way. A Windows reservation cannot be partially released -- the
 * unit of MEM_RELEASE is the whole allocation -- so the window cannot be
 * carved. elf_window_yield therefore releases it and calls the placement
 * function while the address space is bare, then re-reserves whatever the
 * placement did not take. Nothing may allocate between the release and the
 * placement, which is a contract the caller keeps rather than something this
 * code can enforce.
 */
#ifndef ELFSYSV_LOADER_EXEC_RESERVE_H
#define ELFSYSV_LOADER_EXEC_RESERVE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The default window: from the address every non-PIE image is linked at up to
 * the gigabyte boundary, which covers the image, its brk, and the region a
 * loader would choose for an ET_DYN it wants near the executable. Above that
 * the ELF world is content to be placed wherever there is room. */
#define ELF_WINDOW_BASE  UINT64_C(0x00400000)
#define ELF_WINDOW_SIZE  UINT64_C(0x3FC00000)

typedef enum {
	win_ok = 0,
	win_err_arg,       /* a precondition on the arguments was violated */
	win_err_refused,   /* the host would not reserve the window */
	win_err_release,   /* the host would not release it */
	win_err_place      /* the placement function reported failure */
} win_err;

typedef struct {
	uint64_t base;
	uint64_t size;
	int      held;     /* nonzero while the reservation stands */
} elf_window;

/* A free sub-span of the window, described in the child's address space: the
 * parent reserves each of these and leaves everything between them alone. */
typedef struct {
	uint64_t base;
	uint64_t size;
} elf_span;

/* One region of the child's window as VirtualQueryEx reports it, reduced to
 * the one fact the planner needs: whose it is. */
enum { elf_region_free = 0, elf_region_reserved, elf_region_committed };
typedef struct {
	uint64_t base;
	uint64_t size;
	int      state;
} elf_region;

/* Plan the parent's reservation of [base,base+size) over a child whose window
 * already carries regs[0..nreg). A cygwin-linked child holds its own low
 * reservation before any user code runs (spike reent-stub-realproc-window-
 * occupant), so the whole-window reservation is refused where it starts on
 * that region. DR-0068 has the parent recognize the child's reservation rather
 * than reserve over it; this decides, as pure arithmetic, which free sub-spans
 * the parent must still reserve. It writes them into gaps[0..maxgaps) and
 * returns the count, or -1 if a committed region overlaps the window -- an
 * occupant that is not the child's own bare reservation and cannot be
 * reconciled -- or if the gaps do not fit. Base and size are taken already
 * granularity-aligned by the caller. */
int elf_window_plan(uint64_t base, uint64_t size,
                    const elf_region *regs, int nreg,
                    elf_span *gaps, int maxgaps);

/* The placement-time companion to elf_window_plan, and like it a pure
 * decision. Given the window's regions as VirtualQuery reports them, this
 * names the reserved allocations the caller must MEM_RELEASE to bare the span
 * for the placer: it writes each reserved region's base within the window
 * into rel[0..maxrel) and returns the count, or -1 on a committed occupant, a
 * reservation overrunning the window, or an overflow. A window carved by
 * DR-0068 into the child's own low region plus the parent's reservations is
 * several such bases; a single-reservation window is one. */
int elf_window_release_plan(uint64_t base, uint64_t size,
                            const elf_region *regs, int nreg,
                            uint64_t *rel, int maxrel);

/* Called with the window released and the address space bare. Returns 0 if it
 * placed something and nonzero if it did not; on success it reports the span
 * it actually took through took_lo/took_hi so the surrounding space can be
 * re-reserved. It must not allocate before it has taken what it wants. */
typedef int (*win_place_fn)(void *ctx, uint64_t base, uint64_t size,
                            uint64_t *took_lo, uint64_t *took_hi);

/* Reserve size bytes at base. base and size are rounded outward to the host
 * allocation granularity. Idempotent in the sense that matters: a second call
 * on a window already held returns win_ok without touching the host. */
win_err elf_window_reserve(elf_window *w, uint64_t base, uint64_t size);

/* The same reservation, made in another process. This is the one that is
 * actually used: the measurement in t/when-2026-08-30.txt found that no hook
 * inside an image runs early enough, because the kernel places the initial
 * thread's stack and cygwin1.dll lays out its own mappings before the image's
 * first instruction, and both take the lowest free region. So the parent
 * creates the stub suspended and reserves the window into it from outside,
 * where nothing of the child's has run yet.
 *
 * proc is a process handle with PROCESS_VM_OPERATION, normally one just
 * returned by CreateProcess with CREATE_SUSPENDED. The window is described in
 * the child's address space, not this one, so w is bookkeeping the parent
 * keeps rather than a reservation the parent holds; elf_window_release must
 * not be called on it. DR-0028.
 *
 * The stub must be linked with a stack reserve small enough that the kernel
 * does not place the child's initial stack in the window. The measurement's
 * fifth case is exactly that failure: with the default two-megabyte reserve
 * this call returns win_err_refused every time. */
win_err elf_window_reserve_in(void *proc, elf_window *w,
                              uint64_t base, uint64_t size);

/* The stack reserve the stub is linked with, so the constant and the reason
 * for it sit together. One megabyte is placed below the window by the hosts
 * measured; the default two is placed in it. */
#define ELF_STUB_STACK_RESERVE 0x100000

/* Release the window, call place while it is bare, and re-reserve what place
 * did not take. On win_ok the window remains held over the untaken parts, and
 * w->base/w->size continue to describe the whole window rather than the
 * remainder. On failure the window is left released and held is zero, which is
 * recoverable only by the caller giving up on the low addresses. */
win_err elf_window_yield(elf_window *w, win_place_fn place, void *ctx);

/* Adopt a window this process did not make. The stub's window was reserved
 * into it by its parent, so there is nothing for the stub to reserve and
 * everything for it to confirm: this checks that one or more reservations
 * actually cover base..base+size with no gap and no committed occupant
 * (DR-0068 lets the window be several reservations) and records it if they do. A stub that was started
 * without a parent to arm it -- by hand, or by a test -- gets win_err_refused
 * and can decide whether it wants to try reserving for itself. */
win_err elf_window_adopt(elf_window *w, uint64_t base, uint64_t size);

/* Release the window and zero it. Safe on a zeroed or already released one. */
void elf_window_release(elf_window *w);

/* The process-wide window the stub arms before anything else runs, and which
 * the loader later yields. A stub that never armed it finds held zero. */
elf_window *elf_window_low(void);

/* Reserve the default window into elf_window_low(). This is what the early
 * hook calls; it takes no arguments and reports through the window itself so
 * it can be called from a context with nowhere to return an error to. */
void elf_window_arm(void);

/* A stable name for a code, for test output. */
const char *win_err_name(win_err code);

#ifdef __cplusplus
}
#endif

#endif /* ELFSYSV_LOADER_EXEC_RESERVE_H */
