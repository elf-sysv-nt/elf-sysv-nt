# Proposal — exercise the faced DLL as a process's sole runtime

Status: draft
Author: Philip Dye
Date: 2026-08-31
Analysed against: e862a13 on `march`

Drafted at the operator's request. WP-27's crossing certification proves the
faced DLL by `LoadLibrary`-ing `a/build/wp27-face/elfsysv1.dll` from
`crossing.c`, which is itself a Cygwin program and so already holds
`cygwin1.dll`. That puts two Cygwin runtimes in one process. It works -- ten of
ten on a quiet machine -- but it is neither the shape production runs nor the
most robust shape to test, and this proposes the crossing exercise the DLL the
way it is actually used: as the sole Cygwin runtime of a fresh process.

## Why the current shape is not the tested-for shape

In production nothing loads `elfsysv1.dll` beside `cygwin1.dll`. A Linux program
runs on the faced runtime as its one and only Cygwin, launched through the stub;
there is no host runtime in the address space to share the per-process Cygwin
state with. The crossing's two-runtime configuration exists only because the
harness happens to be a Cygwin program, and it exercises a coexistence the
shipped product never asks for.

That coexistence is also the fragile configuration. Measured this session, the
faced DLL loads reliably in every shape on an unloaded machine, but under heavy
concurrent load the two-runtime harness is the one that flakes first, returning
the same `LoadLibrary` error codes -- 126, 998 -- a real defect would. A native
loader that holds no `cygwin1.dll`, loading the faced DLL as the sole runtime,
survived the same conditions better and matches production besides. Testing the
shape the product does not ship, in the configuration most prone to false
failure, is two costs for no coverage the sole-runtime shape lacks.

The project already has the tool for this. The standing practice on this machine
is to start a second Cygwin from `cmd` rather than from a Cygwin shell, so the
new runtime comes up in a process whose parent is not itself Cygwin. That is
exactly the launch a sole-runtime crossing wants.

## The change

Load the faced DLL from a process that holds no other Cygwin runtime, launched
fresh:

- The harness that loads `elfsysv1.dll` and calls its exports is built native
  (mingw), not against `cygwin1.dll`, so the faced DLL is the only Cygwin runtime
  in the process. A native caller can still resolve and call the System V exports
  and check the callee-saved set, which is all `crossing.c` needs.
- It is launched from `cmd`, per the standing practice, so its parent is not a
  Cygwin process and the runtime starts clean.
- The existing WP-22 and WP-23 reruns, which certify the crossing instruments at
  stand-in width, stay as they are; only the against-the-real-DLL half moves to
  the sole-runtime harness.

`hostload.c`, which proves `DllMain` and the PE TLS callback fire from the host
loader, wants the same treatment for the same reason and can share the launcher.

## What this settles and what it leaves open

It settles that the crossing is certified in the configuration the product ships
-- one runtime, fresh process -- rather than the incidental two-runtime one, and
that the certification stops depending on the harness happening to be, or not to
be, a Cygwin program. It removes the shape most prone to a load-induced false
failure, which is what made the crossing read as stuck this session.

It does not change the verdict the crossing already reaches on a quiet machine:
the two-runtime run passing ten of ten is not wrong, only measured in a
configuration the product does not use. Moving to the sole-runtime harness is
meant to make the pass faithful and robust, not to overturn it. It does not
touch the DLL, which needs no change -- it already relocates and loads correctly
as either a sole or a second runtime.

## Not verified

That a native mingw caller can drive every check `crossing.c` makes against the
System V exports without a Cygwin runtime of its own -- the register and
callee-saved checks should port unchanged, but the exports that reach into
Cygwin services may want the runtime up first, and which checks need a live
runtime versus a bare call is the detail the reimplementation settles.

Whether launching from `cmd` is sufficient on its own or whether the harness must
also avoid inheriting a Cygwin environment block; the standing practice suggests
`cmd` suffices, and the reimplementation confirms it.
