# Real-process stub link -- reent bring-up rung, item 1 (measured)

`acceptance/reent/README.md` item 1 asks that the loader's PE host stub be
relinked in the real-process shape -- `-nostdlib` against the WP-26 `crt0.o`
and `-lcygwin`, so `_dll_crt0` brings the reent up the sanctioned way -- without
regressing the WP-41 exec-* certifications the plain-PE stub passes today.

Two spikes measure how far that carries and where it stops, and the answers
reproduce on this tree (2026-09-01):

  - `realproc_stub_links=yes` (`spike/reent-stub-link`). The stub's whole
    translation-unit set links in the real-process shape, once `-lgcc` supplies
    the compiler builtins (`__chkstk_ms`) that `-nostdlib` drops. So the *link*
    half of item 1 holds.

  - `realproc_stub_reaches_version=no` (`spike/reent-stub-link`). The linked
    stub, run standalone, faults during startup before it reaches even its
    `--version` path.

  - `spike/reent-stub-realproc-window` locates that fault. The stub links at
    `0x100400000`, the ordinary high Cygwin image base, not the `0x400000` low
    window it reserves, so the fault is not the image meeting that window. It is
    the crt0 startup crossing: `_cygwin_crt0_common` calls
    `cygwin_internal(CW_USER_DATA)` Microsoft-style, the faced `elfsysv1.dll`
    exports it as a System V veneer, and the Microsoft-style call into the
    System V body faults before `main`. Interposing one local `cygwin_internal`
    (`-DBRIDGE`) reaches `main`. Past startup, one ordinary `printf` -- a
    Microsoft-ABI call into the faced System V libc -- produces no output while
    control survives it, so the boundary stands at every host-to-faced-runtime
    call, not only `cygwin_internal`.

So item 1 is not the window/image-base reconciliation an earlier reading of the
`reent-stub-link` fault named. The obstacle is the Microsoft <-> System V ABI
boundary between a host PE stub and the faced runtime: startup's
`cygwin_internal` crosses it, and so does the stub's own libc use. The startup
half has a demonstrated fix (interpose the crossing); the remaining half is a
bounded choice about how the stub reaches libc -- host-safe calls only for its
own work, or the faced libc through the System V crossing the ELF world already
uses. The decision record carries that reframing.

The plain-PE stub the exec-* certifications drive is untouched -- it links
against the unfaced Cygwin and speaks Microsoft to a Microsoft runtime, so the
boundary does not arise for it. This rung builds the real-process link as a
separate measurement, not a replacement. Item 3 (a reent-consuming ELF body
across the crossing) stays deferred behind this and the WP-53 `libc.so.6`
veneer.

## The bounded choice, measured

`spike/reent-stub-libc-crossing` closes the empirical question the "bounded
choice" above rested on. Past the bridged startup, the real-process probe
reaches the faced libc twice through an explicit `sysv_abi` thunk resolved from
`elfsysv1.dll`'s export directory -- the same shape the `cygwin_internal` bridge
uses -- and both cross: `strlen("abcd")` returns `4`
(`sysv_thunk_reentfree_call_crosses=yes`) and `puts` emits its line
(`sysv_thunk_stdio_call_crosses=yes`, a reent-consuming body over the reent
`_dll_crt0` brought up). So the boundary `reent-stub-realproc-window` found is
the ABI *direction* -- Microsoft-style into a System V body -- not reent
bring-up: routing the stub's own libc use through `sysv_abi` thunks is a viable
route, reent-consuming calls included. The choice between that and host-safe
calls stays a design decision, but it is no longer gated on whether the second
route works. This is a probe, not the loader crossing; item 3 and the
`to-green.tsv` signal are unchanged.
