# Milestones

The first four milestones are spikes, and none produces shippable code. That is
deliberate. `elf-technical-breakdown.md` ends with a list of claims that were
recalled rather than measured, four of them carry weight, and building on an
unmeasured claim is how a program discovers in year two that it chose wrong in
month one.

Each has a directory under `spike/` and one question it answers yes or no. The
verdict is the deliverable. Reaching one is a successful outcome even when the
answer is unwelcome, and especially then.

In dependency order, which is also cost order.

| # | Spike | Question | Gates |
|---|---|---|---|
| 1 | `spike/fs-base-persistence/` | Does Windows preserve a user-written FS base across a context switch? | The TLS layer, and the toolchain target through it |
| 2 | `spike/map-and-jump/` | Can a PE stub map a static ELF and jump to it? | Image mapping and the initial process image |
| 3 | `spike/abi-crossing/` | Can one entry point be System V-faced over an MS-ABI core, through a signal? | `elfsysv1.dll`, and the `-mno-red-zone` policy |
| 4 | `spike/versioned-libc/` | Does el8's `elfdeps` read a vendor-shaped `Requires` off a synthesized `libc.so.6`? | Nothing downstream, which is the point |

Spike 1 is an afternoon and decides a layer. Spike 3 is the expensive one, and
a no there sends the program to the veneer-thunk fallback, which is a different
program. Spike 4 gates nothing technically; it measures whether the whole
edifice repairs what it was built to repair, and it should run before anything
large is funded.

## After the spikes

Unscheduled, because three of the four answers can reshape it.

The target triple wants deciding before the first package is built. The
toolchain follows, which is routine cross-toolchain work. Then the loader, where
musl's `dynlink.c` is the working model and the verdef and verneed matcher is
the part musl leaves out. `elfsysv1.dll` and the libc veneer come after the
loader can run something. The `r_debug` rendezvous can start as soon as there is
a loader to announce objects, and it should, because the alternative is
debugging a world Windows tools cannot see.
