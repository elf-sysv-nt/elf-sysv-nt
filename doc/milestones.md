# Milestones

The milestones open with spikes, and none produces shippable code. That is
deliberate. `elf-technical-breakdown.md` ends with a list of claims that were
recalled rather than measured, four of them carry weight, and building on an
unmeasured claim is how a program discovers in year two that it chose wrong in
month one. Five were planned. Four of them gate something, the fifth prices a
naming decision that has since been taken without it, and the work added three
more as it went: a sixth that settled what `%fs` could not provide, a seventh
that priced whether the red zone can survive delivery, and an eighth that WP-12
turned up. Three more had run without a row until the design-gaps review
numbered them: a ninth that read el8's own binary shape, a tenth that caught the
linker emitting `%fs` code unasked, and an eleventh that built Cygwin from
source. All eleven have run, and one of them took the recommended path off the
table, which is what a spike is for.

Each has a directory under `spike/`, and most answer one question — yes or no
for all but the fifth, a count for that one. Two carry a second characterization
beside the first: `abi-crossing` measures the fault beneath a System V frame as
well as the signal crossing, and `map-and-jump` measures span-claim overlap as
well as the map-and-jump itself, so `test/spike-regen.tsv`, the authoritative
index, counts eighteen measurements across the sixteen directories. Both second
findings are recorded in their spike's entry below. The verdict is the
deliverable. Reaching one is a successful outcome even when the answer is
unwelcome, and especially then.

In dependency order, which is also cost order.

| # | Spike | Question | Gates |
|---|---|---|---|
| 1 | `spike/fs-base-persistence/` | Does Windows preserve a user-written FS base across a context switch? | The TLS layer, and the toolchain target through it. Run 2026-08-29: no. |
| 2 | `spike/map-and-jump/` | Can a PE stub map a static ELF and jump to it? | Image mapping and the initial process image. Run 2026-08-29: yes, with a constraint on when the span is claimed. |
| 3 | `veneer:spike/abi-crossing/` | Can one entry point be System V-faced over an MS-ABI core, through a signal? | `elfsysv1.dll`, and the `-mno-red-zone` policy. Run 2026-08-29: yes, and the red zone is lost to our own layer rather than to the host. |
| 4 | `spike/versioned-libc/` | Does el8's `elfdeps` read a vendor-shaped `Requires` off a synthesized `libc.so.6`? | Nothing downstream, which is the point. Run 2026-08-29: yes, byte for byte. |
| 5 | `spike/triple-fidelity/` | How many packages in the el8 set mishandle a nonstandard vendor field? | Nothing. It priced DR-0001 rather than gating it. Run 2026-08-29: one, `flac`. |
| 6 | `spike/gs-thread-pointer/` | Does a thread pointer reached through `%gs` survive the switch that `%fs` did not? | The TLS model. Run 2026-08-29: yes, four carriers measured; DR-0003 took C3. |
| 7 | `spike/redzone-delivery/` | Can delivery reserve the red zone before it builds the handler frame? | Whether `-mno-red-zone` can be retired. Run 2026-08-29: yes, a reserved delivery holds it and the far side survives; the cost is WP-43's to price. |
| 8 | `spike/fs-base-fault/` | What does an access through a zeroed `%fs` base do, and can a handler resume from it? | Whether a load-time TLS rewriter for vendor binaries may be a heuristic. Run 2026-08-29: it faults and a handler resumes, over the data-movement forms and not the arithmetic ones. |
| 9 | `spike/vendor-image-shape/` | What shape are el8's own binaries — OSABI, ABI-tag, `PT_LOAD` alignment, SONAME? | WP-10's four compiled-in target values. Run 2026-08-29: measured against 41 el8 ELF files. |
| 10 | `spike/ld-tls-relaxation/` | Does the linker emit `%fs`-relative code on its own? | The binutils TLS-relaxation policy in WP-12. Run 2026-08-29: yes, so WP-12 refuses those relocations rather than rewriting them. |
| 11 | `veneer:spike/cygwin-from-source/` | Can this machine build `cygwin1.dll`, and does a reserving delivery hold the red zone? | WP-26's from-source build and the red-zone reservation. Run 2026-08-29: both recorded, the prerequisites and the reservation captured. |
| 12 | `spike/demand-census/` | How many el8 packages need a symbol the classification can only stub? | WP-56's slice order and its named acceptance package. Infrastructure landed 2026-08-31; the run over the 4855-name worklist is in progress. |
| 13 | `veneer:spike/reent-bringup/` | Which host shape makes a reent-consuming libc body work across the face? | WP-56's `reent-tls-bringup` road-to-green row. Run 2026-09-01: the real-process shape (crt0/`_dll_crt0`) carries it — a body sets `errno` through the caller's reent — while the cygload shape's bring-up call hangs; DR records the certification path. |
| 14 | `veneer:spike/reent-stub-link/` | Does relinking the loader stub in the real-process shape make it start? | WP-56's `reent-tls-bringup` road-to-green row, item 1. Run 2026-09-01: it links (once `-lgcc` supplies the builtins `-nostdlib` drops) but the standalone stub faults before entry; row 19 locates that fault as the crt0 `cygwin_internal` ABI crossing, not the window collision first supposed, and a decision record reframes item 1 as crossing the Microsoft↔System V ABI boundary rather than reconciling a window. |
| 15 | `veneer:spike/reent-veneer-runtime/` | Can the WP-53 `libc.so.6` veneer stand as the crossing's reent-bearing runtime? | WP-56's `reent-tls-bringup` road-to-green row, item 2. Run 2026-09-01: the veneer builds and carries the whole reent surface (the `errno@@GLIBC_PRIVATE` TLS carrier and `strtol` at its el8 node), but every FUNC/IFUNC body is a single-byte `ret` — the `elfsysv1.dll` forward each entry reaches is data in `libc-forward.tsv`, not emitted code — so it resolves the crossing at link time but consults no reent at run time; item 2 is generating the forwarding bodies, not merely building the veneer. |
| 16 | `veneer:spike/reent-veneer-body/` | What must a real forwarding body reach, and can a link-time forward reach it? | WP-56's `reent-tls-bringup` road-to-green row, item 2. Run 2026-09-01: every forward-map target (1047 `forward-same`/`forward-alias` names) is a real `elfsysv1.dll` export, so a body has a real destination; but a link-time forward (`jmp strtol@PLT` under the veneer's own `.symver`) self-references — the linker binds it to the veneer's own definition, not the PE export — so item 2's bodies are runtime-resolving thunks against the WP-27 crossing, not a link flag. |
| 17 | `veneer:spike/reent-veneer-thunk/` | What is the link-time shape of the runtime-resolving thunk that replaces the `ret` stub? | WP-56's `reent-tls-bringup` road-to-green row, item 2, the companion to row 16. Run 2026-09-01: a thunk that names its target `"strtol"` as `.rodata` and resolves it at run time through one hidden per-veneer resolver links a versioned ET_DYN where `strtol@@GLIBC_2.2.5` is DEFINED with a real 40-byte body, no undefined `.dynsym` entry or relocation names `strtol`, and the resolver stays out of `.dynsym` — so unlike the naive forward there is no ELF dependency on the faced name for the linker to self-bind. That fixes the codegen contract `generate.py` must emit; whether so-shaped a thunk reaches the face across the loader is item 3, deferred behind the built face and the WP-53 veneer. |
| 18 | `veneer:spike/reent-veneer-face-exports/` | Does every FUNC forward thunk key on a name the face actually exports, across the whole set? | WP-56's `reent-tls-bringup` road-to-green row, item 2, the standing guard for row 16. Run 2026-09-01: all 973 unique `forward-same`/`forward-alias` FUNC targets the built veneer emits are names in the committed `runtime/face/face.tsv` export table, so the run-time resolver finds a face export for each key. Row 16 checked this against the built `elfsysv1.dll` and so SKIPs in the regen harness; this restates it over the FUNC thunk set and the committed face table, needing only the cross toolchain, so it runs as a continuous guard. The name axis of item 2 is complete; reaching the face across the loader stays item 3. |
| 19 | `veneer:spike/reent-stub-realproc-window/` | Where does the real-process relink of the loader stub fault, and is it the window collision row 14 first named? | WP-56's `reent-tls-bringup` road-to-green row, item 1. Run 2026-09-01: no window collision — the stub links at `0x100400000`, not the `0x400000` window it reserves. The fault is the crt0 startup crossing: `_cygwin_crt0_common` calls `cygwin_internal` Microsoft-style into the faced runtime's System V veneer, and faults before `main`. Interposing one local `cygwin_internal` (`-DBRIDGE`) reaches `main`; past it, one ordinary `printf` into the faced libc produces no output while control survives it, so the boundary stands at every host-to-faced-runtime call, not only startup's. So item 1 is crossing the Microsoft↔System V ABI boundary, not reconciling a window; a decision record carries the reframing and row 14's first account is superseded. |
| 20 | `veneer:spike/reent-stub-libc-crossing/` | Does a host→faced libc call cross when reached through a System V thunk rather than an ordinary Microsoft-ABI call? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the companion to row 19. Run 2026-09-01: it does. Past the bridged crt0 startup, the real-process probe reaches the faced libc through an explicit `sysv_abi` thunk and both cross — `strlen("abcd")` returns 4 (reent-free) and `puts` emits its line (reent-consuming, over the reent `_dll_crt0` brought up). So the boundary row 19 found is the ABI direction, not reent bring-up: routing the stub's own libc use through `sysv_abi` thunks is a viable route for item 1's open half, reent-consuming calls included. This is a probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 21 | `veneer:spike/reent-stub-realproc-version/` | Do the two fixes rows 19 and 20 proved in isolation compose at the exact path `reent-stub-link` found faulting — the real-process stub's `--version`? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the capstone of rows 19 and 20. Run 2026-09-01: they do. One probe models `stub.c`'s `--version` (`printf("%s\n", RELEASE)`, a reent-consuming stdio body) in three build variants: without the startup bridge control never reaches the version path (`startup_faults_without_bridge=yes`, reproducing `reent-stub-link`); with the bridge but a Microsoft-style print the line does not cross (`version_print_plain_crosses=no`, reproducing row 19); with the bridge and a `sysv_abi`-thunked print the line crosses and control survives (`version_print_thunked_crosses=yes`). So a real-process stub reaches and completes its `--version` once both measured fixes are applied — the empirical green light for relinking `loader/exec/stub.c` without regressing the WP-41 exec-* certifications. A probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 22 | `veneer:spike/reent-face-bringup/` | Does a reent-consuming libc body set the caller's reent (strtol overflow → `LONG_MAX`, `errno` `ERANGE`) across the WP-53 veneer's runtime-resolving thunk into a built `elfsysv1.dll` face? | WP-56's `reent-tls-bringup` road-to-green row, item 3 — the run items 1 and 2 defer to it. Scaffolded 2026-09-01 as a WIP skeleton: the measurement rests on three scratch artifacts (the WP-26 winsup DLL, the WP-27 face, the WP-53 `libc.so.6` veneer), so `measure.sh` reports the first prerequisite absent rather than asserting a finding, and the spike is not yet in `test/spike-regen.tsv`. It registers, with a recorded transcript, once the three build and the veneer→face crossing runs. |
| 23 | `veneer:spike/reent-stub-faceload/` | Does a real process of the faced runtime reach `elfsysv1.dll`'s base without the `error 1114` cygheap wedge the cygload stub hits? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the face-base half the `reent-face-bringup` live run (row 22) halts on. Run 2026-09-01: it does. A real-process host (`-nostdlib`, WP-26 `crt0.o`, `-lcygwin`; the rows 19–21 link and startup bridge) reaches `main`, and its `--runtime` `LoadLibraryA` of the faced `elfsysv1.dll` returns the runtime's base — the host's own already-loaded module, matching `GetModuleHandleA` — so the call bumps a refcount rather than re-reserving the cygheap, and no `1114` arises. That is the base `--runtime` publishes through `AT_BASE`; the wedge is the cygload shape's, not the load's. The remaining step is the full relink of `loader/exec/stub.c` into that shape, not the base-reachability. A probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 24 | `veneer:spike/reent-stub-stderr-crossing/` | The stub's stderr diagnostics (`say`, `refuse`, `usage`, the unknown-option and no-argument messages) write to `stderr`, not the `stdout` the landed `rp_puts` thunk (row 20) carries; what stderr crossing do they reroute through? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the stderr twin of row 20. Run 2026-09-01: the faced `elfsysv1.dll` exports no `stderr` `FILE*` (`stderr_file_export_present=no`), so an `fputs`-to-stderr twin has nothing to name and the crossing must take an fd-2 body; both that exist cross the faced runtime — `write(2,s,n)` raw (`sysv_thunk_write_fd2_crosses=yes`) and `dprintf(2,"%s",s)` variadic (`sysv_thunk_dprintf_fd2_crosses=yes`). `write` is the route: non-variadic, no `FILE*`, formatting host-side first as the `--dry-run` `report()` does; `dprintf` crossing is recorded but declined on the host-side-formatting line DR-0066 draws, not because it faults. So the deferred diagnostics reroute through a `sysv_abi` `write` thunk — the implementing next step. A probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 25 | `veneer:spike/reent-stub-path/` | The real-process stub reads the image with `CreateFileA`, which resolves a Windows-form path, but the loader is handed a Cygwin POSIX path (`-r /bin/echo.exe`); how is that path made openable host-side without a faced-libc call (`SLURP-REROUTE.md`'s open question)? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the path half the image-read reroute (row of `SLURP-REROUTE.md`) left open. Run 2026-09-01: the parent passes the Windows path. On the loader's own input `/bin/echo.exe`, the parent's host `cygwin1.dll` conversion (`cygwin_conv_path`) resolves the mount and `CreateFileA` opens it (`parent_cygwin_conv_opens=yes`), while the stub's only host-safe conversion, `GetFullPathNameA`, reads it as drive-relative -- `C:\bin\echo.exe` -- and does not (`stub_getfullpath_opens=no`). The conversion the stub could make host-safe cannot resolve a mount, and the one that resolves the mount is a `cygwin1.dll` call, host-safe only in the parent (`route=parent-passes-windows-path`). So the front end (`loader/exec/dispatch.c`, a normal Cygwin process) converts the resolved image path and hands the real-process stub a Windows-form operand; wiring that conversion is item 1's next implementing step. Native, no faced DLL; a probe, not the loader crossing. Item 3 and the to-green signal are unchanged. |
| 26 | `veneer:spike/reent-stub-realproc-run/` | Do the item-1 fixes rows 19–24 proved on miniature probes compose on the actual `loader/exec/stub.c` — does the real stub link and run in the real-process shape? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the composition of the probe findings on the real source. Run 2026-09-01: it does. With the realproc seam on (`-DELFSYSV_REALPROC`), the whole `stub.c` translation-unit set links `-nostdlib` against the WP-26 `crt0.o` and `-lcygwin` (`realproc_stub_links=yes`); run detached, it reaches `--version` and emits its `RELEASE` line across the faced runtime (`realproc_stub_reaches_version=yes`, `rp_puts` past the crt0 startup bridge); and a deeper path — the low-window check reached only after startup and option handling — emits its diagnostic across fd 2 (`realproc_stub_diag_crosses=yes`, `rp_eputs`), so real stub logic runs and both crossings carry, not only the early `--version` exit. The plain-PE control still reaches `--version` (`plain_stub_reaches_version=yes`), so the relink is a shape added beside the WP-41 one. The `--runtime` face-base half stays with row 23: the stub loads `--runtime` only after the low window is held, which the parent front end reserves, so the real stub's own faceload is a front-end-driven run (`reent-face-bringup`'s live-run), the next step. Item 3 and the to-green signal are unchanged. |
| 27 | `veneer:spike/reent-stub-realproc-faceload/` | Driven through the WP-41 front end so the parent reserves the low window into the suspended child, does the real-process stub receive the handover the plain-PE stub does — the step toward its `--runtime` faceload? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the front-end-driven run row 26 named as its next step. Run 2026-09-01: it does not, and the transcript locates why. The real-process stub, the front end, the veneer, and a reent-consuming specimen all build; the plain-PE control receives the low-window handover and runs past it (`plain_stub_gets_window=yes`), so the DR-0028 handover and the harness both work. The real-process stub is refused it (`realproc_stub_gets_window=no`): the front end's `VirtualAllocEx` of the `0x400000` window into the suspended child fails `win_err_refused`, because the child is linked against `cygwin1.dll`, which the Windows loader maps into the suspended child and which already holds the low region before any user code runs. So the run halts at the window handover, before the faceload (`reent_faceload_run=blocked`, `verdict=staged`). This relocates item 1's last step: not the faceload — its base reachability is clear in the sanctioned shape (row 23) — but reconciling the low-window handover with a cygwin-linked child, the child that holds the low region being the same runtime the window is reserved for. A probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 28 | `veneer:spike/reent-stub-realproc-window-occupant/` | What does the cygwin-linked child hold at the low window at suspend, the region that refuses the DR-0028 handover row 27 found — inferred there, not measured? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the measurement row 27 left as inference. Run 2026-09-01: walking each stub spawned `CREATE_SUSPENDED`, before any user code, the plain-PE control's low window is one `MEM_FREE` region and its handover succeeds (`plain_reserve_in=ok`), while the cygwin-linked child already holds a private `MEM_RESERVE` region over the low ~2 MB at `0x400000` (`occupant=private-reserved`, `covers-window-base`), so the parent's whole-window `VirtualAllocEx` is refused with `err=487` (`ERROR_INVALID_ADDRESS`). It is not the child's image (that links high, row 19) and it predates user code, so it is the runtime's own low reservation — made for the same low region the window claims. The collision is only the low ~2 MB; from `0x600000` the window is free, so the handover fails because it *starts* on the child's reservation, not because the window is occupied. Item 1's last step is thus identification, not eviction: recognize the child's low reservation (adopt it, or reserve the window above it) rather than reserve over it. A probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 29 | `veneer:spike/reent-stub-realproc-window-reconcile/` | Does `elf_window_reserve_in`'s DR-0068/0069 reconcile fallback actually reserve the low window against a real cygwin-linked child — the reserve verb row 28's constraint pointed at, driven live rather than in the unit fixtures? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the live reserve verb. Run 2026-09-01: it does not (`verdict=blocked-by-committed-occupant`). The plain-PE control reserves the whole window in one call (`plain_reserve_in=win_ok`, `window_covered=yes`); the cygwin-linked child refuses the raw whole-window call (row 28) and refuses the reconcile fallback too (`realproc_reserve_in=win_err_refused`, `window_covered=no`). The walk shows why: the child's low window is `low_window_occupant=reserved+committed`, not the bare `MEM_RESERVE` DR-0068's planner models — a private reservation plus committed pages below the free tail, the runtime's own, present before any user code. `elf_window_plan` refuses any committed occupant by design, so the fallback returns `win_err_refused`. This re-aims item 1: how the loader should treat a committed low occupant that is the child's own runtime allocation (adopt it, reserve up to it, reserve around it) is a design step with more than one live candidate, parked for the operator rather than guessed. A probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 30 | `veneer:spike/reent-realproc-low-window/` | Rows 27–29 measured the parent-to-suspended-cygwin-child window handover refused, and the faced-runtime-hosting decision answered by making the crossing a real process of the faced runtime instead. That decision names one turning question: can such a process, on its own `_dll_crt0` main thread, map the fixed low ELF window through its own `mmap`, where the handover was refused? | WP-56's `reent-tls-bringup` road-to-green row, item 1, the measurement the resolved shape turns on. Run 2026-09-01: it can (`verdict=cleared`). Built in the sanctioned real-process shape (`-nostdlib`, WP-26 `crt0.o`, `-lcygwin`, rows 19–21), the faced `mmap` returns a live anonymous page on the main thread (`realproc_mmap_anon=live`, no bare-native-thread `cygtls` crash), places `MAP_FIXED` at a free low address (`realproc_mmap_fixed_free=ok`), and — the finding — places `MAP_FIXED` at `ELF_WINDOW_BASE` `0x400000` for bzip2's span (`realproc_mmap_fixed_window=ok`). The walk shows why the handover failed there but this does not: in a sole-runtime process the low window `[0x400000,0x600000)` reads `low_window_occupant=free`, not the `reserved+committed` occupant the suspended child held (row 29). The occupant the reconcile fought was an artifact of the foreign-parent/suspended-child arrangement, not of a cygwin runtime as such. So the parent window handover (DR-0028) and its child reconcile (DR-0068/0069) are set aside on this path, and item 1's finishing code step — moving `accept.sh`'s crossing onto a real process of `elfsysv1.dll` — rests on a measured constraint. A probe, not the loader crossing; item 3 and the to-green signal are unchanged. |
| 31 | `spike/vendor-hardened-build/` | WP-56 parked on a non-PIE bzip2 that wants `0x400000`, and the three candidates scored against that park each assumed the `ET_EXEC` was el8's shape. Is it? | The candidate scoring, and DR-0072. Run 2026-09-02: it is not (`verdict=the-non-pie-image-is-the-harness-artifact`). Red Hat ships an `ET_DYN` with zero-based segments; the spec builds through `$RPM_OPT_FLAGS` and `%{__global_ldflags}`, whose macros pull `redhat-hardened-cc1` and `redhat-hardened-ld`, and those two specs files inject `-fPIE` and `-pie` — each link read out of `redhat-rpm-config-131-1.el8` rather than recalled. The same source cross-built through `acceptance/packages.tsv`'s naked `make` line gives `EXEC` at `0x400000`; under the el8-effective flags it gives `DYN`, zero-based and granule-separable under DR-0061. So the image that could not be placed was the harness's, and building it the vendor's way upholds DR-0000 rather than crossing it. The flag substitution (specs files for the flags they inject) is S2 in `doc/design/substitutions.md`. |
| 32 | `spike/rtld-lift-survey/` | `AGENTS.md` now asks what Linux and GNU already have before code is written, and the loader is where that was never asked. The standing answer rested on `doc/history/elf-technical-breakdown.md` recording glibc's resolver as GPL, which it is not. Could FreeBSD's `rtld-elf` have been lifted, and what would a lift carry? | The reuse convention, and WP-44's oracle. Run 2026-09-02: it was liftable on licence all along (`verdict=liftable-on-licence-adaptation-is-the-question`). `rtld.c` and `rtld.h` both carry `SPDX-License-Identifier: BSD-2-Clause`, which an LGPLv3-or-later tree takes outright; every versioning entry point is present — `find_symdef`, `symlook_obj`, `symlook_obj1_gnu`, `symlook_obj1_sysv`, `matched_symbol`, `rtld_verify_versions` — with verdef and verneed both parsed; and the path is 320 lines out of `rtld.c`'s 6411 against 9 BSD kernel includes. Three hundred lines is the same order as what WP-36 wrote from Drepper, so the choice was closer than the record made it look, and the record made it look further away because it had the licence wrong. The verdict stops at liftable: whether to lift now is a decision this does not take, and the likelier value here is as a differential oracle for WP-44 rather than as source to adopt. |
| 33 | `veneer:spike/empty-version-node/` | Proposal 0008 §2 asserted, as a correctness constraint, that a version node emptied by the withdrawal fails a consumer's verneed. The whole node-retention rule rested on it and it was never measured. Does an empty node still satisfy a verneed? | The retention rule, and DR-0083. Run 2026-09-03: it does (`verdict=an-empty-node-is-still-defined`). The version script declares the node, so withdrawing every member leaves its `.gnu.version_d` entry in place — the generator already printed "nodes with no symbols (still emitted)" and nobody read it against the rule. The second finding is the decisive one: a verneed entry exists because the consumer references a symbol at that node, so relinking against the withdrawn provider is refused on the symbol rather than on the version, and retaining some other member never helped. The rule cost 58 of 111 claimed names, twenty of them exported with no body, and is withdrawn. At scale the rebuilt `libc.so.6` carries 24 empty nodes of 29, all 29 still defined, the ladder identical to the vendor's, and elfdeps' thirty provides unchanged. |
| 34 | `veneer:spike/errno-funnel/` | DR-0091 recommends a deposit-site errno translation and records the premise it rests on as unverified: that Cygwin's errno stores funnel through a countable set of sites, with `set_errno` the expected funnel and winsup unaudited for bypasses. Where does Cygwin write errno, and is that a countable set? | DR-0091's open question, and the deposit mechanism the implementing package would choose. Run 2026-09-03 against `cygwin-3.6.10-7-ga9925920a`: the funnel is not whole and the shape is ruled out (`verdict=deposit-site-translation-is-ruled-out`). Seven deposit forms, five of which bypass `set_errno` — chiefly `__seterrno` and its `_from_win_error` / `_from_nt_status` siblings, which assign in `errno.cc` directly, plus the `save_errno` destructor, plain assignments, `set_sig_errno` through a pointer cached in `_cygtls`, and the generated signal-return stub storing through that same pointer in assembly; newlib, linked into the same DLL, assigns it too (`outside_winsup=newlib-writes-the-same-slot`). The slot is `_cygtls.local_clib._errno` — `__DYNAMIC_REENT__`, not `_Thread_local` — with `_impure_ptr->_errno` and `_cygtls.errno_addr` as second and third keepers. The decisive finding is the read-back (`readback=cygwin-reads-its-own-errno-and-compares-against-cygwin-constants`): thirty-six sites and two switches branch on the slot against Cygwin constants the compiler baked in — `disk_file.cc` on `ENOSYS`, `spawn.cc` rewriting `ENOMEM` to `E2BIG`, `mount.cc` on `EMFILE`, `signal.cc` on `EINTR` — so a Linux number deposited there is compared against Cygwin numbering and takes the wrong branch. Translating the reads back down cannot save it: DR-0088 measured the el8 caller reading the slot through a cached `__attribute_const__` pointer, so the contents must already be Linux's, and one location holds one numbering. What survives is DR-0088's E1 in its plainest form — the floor's `errno` constants built as el8's, so nothing translates anywhere. A source audit, not a run; DR-0091's shape recommendation is what it answers, and the record's decision stands. |
| 35 | `spike/nt-clone-fork/` | Proposal 0011 rests `fork` on cloning an NT process's address space. Interix proved the primitive and its source was never public, so nothing here has run it. Does it work on this Windows 11 build, and does the clone carry a live thread? | Proposal 0011's open question 2, and the process model the rest of that design stands on. Run 2026-09-04: yes, through one candidate of two (`verdict=clone-works-via-rtlclone`). `NtCreateProcessEx` with a null section does clone the address space — a private page written before the clone reads back in the child at the same address, inheritable handles arrive at the same values, and a `ViewShare` section view is coherent both ways — but no thread can be created in that clone: `NtCreateThreadEx` and `RtlCreateUserThread` both return `STATUS_PROCESS_IS_TERMINATING` against a clone whose `ExitStatus` is `STATUS_PENDING`, while the identical call runs a thread in the calling process. The obvious explanation was the parent's `csrss` registration, and it is wrong: relaunched from an ntdll-only native-subsystem parent, the same clone refuses its thread with the same status. What carries is `RtlCloneUserProcess`, the fork-shaped wrapper over `NtCreateUserProcess`, which returns `STATUS_PROCESS_CLONED` in a child that reaches its own code on a live thread and passes the address-space, handle and shared-view checks in that shape. The raw `NtCreateUserProcess` call with `PROCESS_CREATE_FLAGS_INHERIT_FROM_PARENT` is refused with `STATUS_INVALID_PARAMETER`, so the wrapper is the interface rather than a convenience over one. Measuring the second candidate is what turned the verdict; the first alone would have recorded a design-ending no. |
| 36 | `spike/native-host/` | Proposal 0011's `lk-host` is a process whose only module is `ntdll` — no `kernel32`, so no `csrss` registration, so cloneable. Can such a process be created here, and can it do the kernel's work? | Proposal 0011's open question 3, and the host process every later phase runs inside. Run 2026-09-04: yes (`verdict=native-host-viable-afd-complete`). The subsystem field decides and the import table does not: two images with identical ntdll-only imports map two modules when linked `--subsystem native` and four when linked console, the console pair being `KERNEL32.DLL` and `KERNELBASE.dll`. The native image is created by `NtCreateUserProcess` and runs; `cmd /c` refuses it, which costs nothing, since the supervisor creates `lk-host` and no shell is in that path. Inside it, `RtlWaitOnAddress` carries a wake between threads under a timeout, an ALPC port is created and connected, and AFD works end to end — endpoint, `IOCTL_AFD_BIND` to a port read back through `IOCTL_AFD_GET_SOCK_NAME`, listen, connect, eight bytes across, and `IOCTL_AFD_POLL` reporting not-ready before data, read-ready after the peer sends, and write-ready on the connected endpoint. Two layout faults hid behind one `STATUS_INVALID_PARAMETER`, both found by reading Wine, ReactOS and `wepoll` as specification: the EA name is the literal `AfdOpenPacketXX` at fifteen characters, and this build's `IOCTL_AFD_CONNECT` wants `AFD_CONNECT_JOIN_INFO` rather than the older `AFD_CONNECT_INFO`. The loader list re-walked after all of it still holds two modules. Open question 8 is reported and not settled: the poll is level-triggered, the same bit returning while data sits unread, so an edge needs the caller's own last state — criterion 9 is where that is decided. The injection half of the question is answered for Defender only, this machine carrying no third-party EDR. |
| 37 | `spike/arena/` | Proposal 0011 § 4.4 has the kernel own the user range as a placeholder reservation and replace pieces of it with section views, committing lazily. Every part of that is a reading of documentation. Does the mechanism behave that way here? | Proposal 0011 § 4.4, and through it the `p_vaddr ≡ p_offset (mod 64 KB)` congruence rule the design imposes on ELF segments. Run 2026-09-04: yes, and at a finer granularity than the design assumed (`verdict=arena-holds-at-4k`). One `NtAllocateVirtualMemoryEx` placeholder was accepted at 1 GB, 64 GB, 1 TB, 8 TB and 64 TB; 127 TB is refused with `STATUS_NO_MEMORY`, so the literal claim on the whole user range fails and the practical one — half of it, in a single reservation — holds. Inside that 64 TB placeholder a `MEM_PRESERVE_PLACEHOLDER` split carves a piece measured exactly one granule wide, a pagefile-backed section replaces it, and the neighbours survive intact. The finding that moves the design is the fine-grained one: a 4 KB split and a 4 KB view replacement both succeed, including a file-backed view at a section offset of 4096, which has no 64 KB congruence to its address. 64 KB is what Microsoft documents and 4 KB is what this kernel does, and one machine's evidence is not enough to retire a rule — the spike's reading is to build against the 64 KB invariant and let the kernel probe for the finer one, which is a proposal-level call left to the operator. Lazy commit through a vectored handler works: the handler catches the access violation on an uncommitted page, commits it, returns `EXCEPTION_CONTINUE_EXECUTION`, and the faulting store re-executes, over 2048 further pages taking the same path. |
| 38 | `spike/hijack/` | Proposal 0011 delivers a signal into a thread that is not cooperating, by suspending it and rewriting its context. Does that land on this Windows 11 build, and does it respect the red zone this project already fought for? | The signal-delivery path, against DR-0030 and DR-0050. Run 2026-09-04: yes, with the kernel case deferring rather than failing (`verdict=hijack-delivers-user-defers-kernel`). A thread spinning in user mode with no syscalls and no faults was hijacked on all 2000 deliveries and returned cleanly to its loop from the saved context. The frame built 128 bytes below the interrupted `%rsp` left the red zone whole — the pattern at offsets 8 through 128 untouched, the reserved frame's nearest write at 136 — while the naive control lost offset 8, which is what proves the watcher can see. A thread inside `NtWaitForSingleObject` reports a user `%rip` at the syscall return in `ntdll`, and the rewrite does nothing until the wait completes, then fires: deferred-to-wait-return, which is the behaviour the design wants rather than a limit it has to work around. The in-kernel flag is trustworthy — over 20000 suspensions, every flag-down observation caught `%rip` outside the guarded region, no violations. The rejected alternative was measured rather than recalled: 2000 user APCs queued at the spinning thread never ran, and the same APC drained at once on an alertable control. The finding is bounded by configuration: user-mode shadow stacks are off on this build, so the `%rip` rewrite is unconstrained here and a host with CET on is not covered. |
| 39 | `spike/lxfs/` | Proposal 0011 takes WSL's on-disk metadata format rather than inventing one, so that a tree written here reads under WSL and Cygwin. Does this NTFS volume carry that metadata through NT's own interfaces? | The on-disk format decision, and verification criterion 3. Run 2026-09-04: it does, and the interop half is one-sided (`verdict=lxfs-complete-cygwin-blind`). `FileStatLxInformation` returns all six LX fields on an open handle and, the claim the proposal named as unverified, through `NtQueryInformationByName` with no file opened, agreeing field for field including `FileId`. The `$LXUID`, `$LXGID`, `$LXMOD` and `$LXDEV` extended attributes round-trip through `NtSetEaFile` and `NtQueryEaFile`, and the stat class reports what was written. An `IO_REPARSE_TAG_LX_SYMLINK` is set and read back with its UTF-8 target intact, needing no privilege. Case sensitivity was the standing worry and it did not materialise: `FileCaseSensitiveInformation` is set unelevated, `Makefile` and `makefile` coexist and both enumerate. POSIX delete works with one constraint worth carrying into the VFS: the disposition succeeds but the name leaves only when the deleting handle closes, so `unlink` is open-for-DELETE, set, close — Cygwin's own shape. The interop finding is the unwelcome one. WSL's `rocky8` agrees with everything written, modes, owners, symlink target and a 1:3 character device; Cygwin 3.6.10 resolves the symlink and nothing else, reading both files 755 owned by the Windows account and the device node as an empty regular file. Criterion 3 as written cannot pass on this host, and that is a fact about Cygwin's ACL-derived `stat` rather than about NTFS. Extended 2026-09-06 with q9 (`results-2026-09-06.txt`): a POSIX rename replaces an open target and the old handle keeps reading; a POSIX delete of a file with a live section view succeeds and the view still reads; a truncate below a live view is refused with `ERROR_USER_MAPPED_FILE` until the view is unmapped; and a directory cannot be renamed while a handle is open beneath it. The first two are Linux's semantics for free; the last two are recorded divergences. |
| 40 | `spike/whp/` | Proposal 0011 designs two substrates and open question 1 chooses between them on one number: what a syscall costs under the hypervisor. The 5 to 15 µs the proposal quotes is a recollection of published gVisor and WHP figures, not this project's measurement. What does it cost here? | Proposal 0011's open question 1, and through it whether the toolchain change and the userland rebuild are needed at all. Run 2026-09-04: the substrate is usable and the number is at the favourable end (`verdict=whp-usable`). The hypervisor is present and the optional feature is on, queried without elevation; a partition is created and set up, 4 MiB is mapped at GPA 0 with RWX, a vCPU takes all 21 registers in one call, and guest code runs at CPL 3 with `CS=0x2b`. With `EFER.SCE` on and `LSTAR` pointing at a shim, a ring-3 `syscall` reaches the host as a halt exit at the shim with the user return address in `RCX`, and `sysretq` puts the guest back at CPL 3. An unmapped guest-physical touch arrives as a memory-access exit carrying exactly the faulting GPA. The measurement: median 5.0 µs per exit round trip over 20000 exits, p99 10.2 µs, and the full ring-3 syscall round trip measures the same median, so the shim costs nothing observable. A trapping NT syscall on the same host is 679 ns, making the hypervisor path about seven times a native syscall and comfortably under open question 1's 10 µs threshold. The surprise, which shapes any real shim: `hlt` is privileged and the first ring-3 sequence triple-faulted, there being deliberately no IDT, so ring three leaves through `syscall` or through a page the partition does not back. The second condition of open question 1 — the operator accepting the hypervisor feature as a prerequisite — is not a measurement and this spike does not touch it. |
| 41 | `spike/whp-vcpu-interrupt/` | Proposal 0011's substrate interface has nine calls; eight were exercised across spikes 35–40, but `thread_interrupt` under H — forcing a running WHP vCPU out from another thread — was never run, and the "build both substrates" plan rests on the interface being genuinely two-implementable. Is H's `thread_interrupt` the equal of N's hijack? | Proposal 0011 §3, and the two-substrate bet. Run 2026-09-05: yes (`verdict=interrupt-delivers-resumable`). `WHvCancelRunVirtualProcessor` from a second host thread forces a vCPU spinning in a never-exiting ring-3 loop back to the host — 200 of 200 return `WhvRunVpExitReasonCanceled` at CPL 3 — and the vCPU re-enters and resumes where it stopped, `%rax` climbing across the exits, so it is an interrupt and not a teardown. Register injection on the cancel delivers as N's context rewrite does: `%rip` pointed at a guest stub and `%rsp` moved 128 below the interrupted pointer (the DR-0030 red-zone gap), the stub runs and stores its marker, the saved context restores and the loop resumes. The cancel-versus-run race is closed by WHP itself, not by hand: a cancel issued while the vCPU is provably not in a run latches and the next run returns canceled at once (200/200, the in-kernel-flag concern spike (d) handled on the N side), and 10000 cancels raced against the loop produce exactly 10000 canceled exits, none lost, none doubled. The cost is a normal exit — median 4.1 µs, p99 14.5 µs — on spike (f)'s ~5 µs exit and well under spike (d)'s ~18 µs NT hijack. So the last unproven interface call is proven, and H's `thread_interrupt` is the equal of N's. Bounds: a bare guest loop, not the ring-0 shim with a real `rt_sigframe`; a single vCPU, so the multi-vCPU cost spike (f) flagged is still open; one Windows build, one AMD processor. |
| 42 | `spike/whp-gpa-map/` | Substrate H's memory story is one sentence in 0011 § 4: the lazy-commit handler "still exists, one level down". Does `WHvMapGpaRange` populate or pin the host memory behind it, and what does the guest see when the host page behind a mapped GPA is reserved, decommitted or released? | H's memory design in either shape, and the topology choice in proposal 0012 (measurement 1 of the four taken 2026-09-05). Run 2026-09-05: mapping is lazy and pins nothing (`finding=map-lazy,touched-pages-resident-unlocked,reserved-maps-exits-until-committed,decommit-and-release-exit-cleanly,map-on-exit-works`). A committed, untouched gigabyte maps in 12 ms with the working set unmoved and no sampled page resident, while the hypervisor's own counter reports every page mapped; the guest's first touch backs a page at about 13 µs against 4 µs for a resident one and leaves it unlocked. Reserve-only host memory maps, and a guest touch arrives as a memory-access exit with `GpaUnmapped` clear, "mapped, host absent", distinguishable from an unmapped GPA; committing behind the exit and resuming works, 256 of 256, at about 32 µs a page. Decommit and release behind a live mapping both succeed and the next touch exits the same way; a recommitted page reads zero. A 4 KB map call is about 7 µs and a 2 MB one about 30 µs; populate advice over 64 MB costs 6 ms, after which a first touch costs what a resident page does. Mapping page by page on exits works (1024 of 1024, about 25 µs each) and is the slow way; map and populate in ranges is the lever. Extended 2026-09-06 with q8 and q9 (`results-2026-09-06.txt`, `map-lazy-at-64gb,trimmed-pages-return-transparently`): 64 GB of reserved host space maps in about a second and unmaps in about the same, 16 ms per gigabyte, working set unmoved, 16.8 million 4 KB entries counted by the hypervisor; after `EmptyWorkingSet` none of 1024 guest-written pages was resident and every one read back correct from the guest at 13 µs, with 2 GB of host pressure displacing none. |
| 43 | `spike/whp-partition-cost/` | Shape A builds a partition per fork and holds one per Linux process; shape B pools vCPUs in one partition and hands them between threads. What does a partition cost to bring up, how many may one process hold, how many vCPUs does one carry, does a vCPU run from one thread and then another, and do concurrent exits cost what one does? | The topology choice in proposal 0012 (measurements 2 and 5), and shape A's fork budget. Run 2026-09-05: one mapped partition per process (`finding=one-mapped-partition-per-process,section-shared-across-processes,created-vcpus-all-run,vcpu-hands-off-between-threads,concurrent-exits-run`). Create, size and set up is about 0.4 ms, the whole way to a first exit about 0.7 ms, delete about 0.6 ms. A process may set up several partitions but a second `WHvMapGpaRange` into a second partition is refused with `0xC0370008` whichever mapped first, and accepted once the first unmaps; a second process holding its own mapped partition is unaffected, and a section viewed in both processes maps into both partitions and is one memory. That rules out a partition per Linux process inside one kernel process (shape B1) and leaves one partition with a page-table root per process (B2), whose preconditions hold: `ProcessorCount` accepted to 2048, 240 vCPUs created and each run to a halt (the 241st refused with a VID status, Hyper-V's ceiling), a vCPU run alternately from two threads a thousand rounds each, and eight vCPUs exiting at once at 4 to 6 µs each against 3.6 µs alone, over a million exits a second aggregate. Extended 2026-09-06 with q7 (`results-2026-09-06.txt`, `vcpu-pool-isolates`): the pool as 0012 describes it, 8 vCPUs under 256 threads each carrying a private counter, 256,000 rounds with no counter ever moved by another thread's run; a round (borrow, load, run, save, return) is 13 µs uncontended, about 500,000 a second, and the aggregate holds near 260,000 a second at 32 threads per vCPU. |
| 44 | `spike/whp-pagetable-fork/` | Shape B forks by copying a process's page tables inside one partition and copying a page on the first write fault, with the host as the kernel and no guest IDT. Does a guest `#PF` reach the host as an exception exit, what does the copy cost at 64 MB and 576 MB, what does one copy-on-write fault cost end to end, is the isolation right, and what does switching a vCPU between roots cost? | Shape B's `as_clone`, criterion 4 for shape B, and the topology choice in proposal 0012 (measurement 3). Run 2026-09-05: yes on every count (`finding=page-fault-exits-to-host,guest-runs-on-4k-tables,fork-copies-tables,cow-isolates,no-tlb-reload-needed,all-cow-paths-correct,root-switch-works`). With `ExtendedVmExits.ExceptionExit` and bit 14 of the exception bitmap, a store through a read-only leaf arrives as `WHvRunVpExitReasonException` carrying the fault address in `ExceptionParameter` (the CR2 register reads zero), error code `0x3`, `%rip` still at the store. Copying the tables of a 64 MB process (37 table pages) takes about 0.1 ms and of a 576 MB one (294 pages) 0.5 to 0.75 ms, against spike 35's 5.1 ms process clone. A copy-on-write fault costs about 37 µs from register write to halt, 5 µs of it the host's walk and copy, against 12 µs for the same store without a fault; the stale translation needs no flush, 256 of 256 stores completing in one fault without the CR3 reload. Parent, child and grandchild each read their own bytes, from the host and through the guest under each root; the parent copies a page the child never touched and takes without copy a page the child already copied. Alternating a vCPU between two roots costs about 6 µs over running it on one. Extended 2026-09-06 with q7 (`results-2026-09-06.txt`, `ring3-cow-isolates`): the same stores from CPL 3 through DPL-3 selectors fault with error code `0x7`, user bit set, `%rip` at the store, one fault per page, every frame copied and isolated, 24 µs a round trip; a ring-3 store to a supervisor page faults and is refused. |
| 45 | `spike/whp-clone-host/` | Shape A forks by `RtlCloneUserProcess` of a Win32 host that holds a partition, `WinHvPlatform.dll` needing `kernel32`. Spike 35 cloned a plain process; nothing had cloned one holding the hypervisor. Does the clone run, what of Win32 works in it, can it build a partition and run a vCPU, what does the inherited handle do, and what does the clone cost with guest memory mapped? | Shape A's viability and fork floor in proposal 0012 (measurement 4). Run 2026-09-05: it runs (`finding=clone-runs,win32-works-loadlibrary-fails,child-builds-and-runs-partition,inherited-handle-hangs-first-runs-after-own,one-mapped-partition-in-child,parent-partition-survives-clones,clones-with-mapped-memory-succeed`). The child does heap, event, wait, `VirtualAlloc` and TLS work, cannot `LoadLibrary`, and builds a partition of its own to a first halt in 1.3 to 2.3 ms, twice a fresh process's. A `WHvRunVirtualProcessor` on the inherited partition handle as the child's first WHP call never returns; after the child has built its own partition the same call returns a halt, having run one iteration of the parent's guest, so the design closes the inherited handle first and never runs it. The one-mapped-partition rule of spike 43 holds in the child, the inherited mapping not counting against it. The clone costs 3 to 5 ms with 4 MB mapped and 6 to 8 ms with 256 MB touched, and the control with that memory touched but unmapped costs the same, so the hypervisor adds nothing to the clone; the child's remapping of its guest memory, which would, was not measured. |
| 46 | `spike/glibc-spawn-clone-flags/` | Proposal 0011 § 6 says `clone` "with `CLONE_VFORK` but without `CLONE_VM` is the `vfork` glibc's `posix_spawn` uses" and returns `EINVAL` for every other combination. What does the shipped el8 libc actually ask for? | Whether `posix_spawn` works at all under H, which runs that libc unmodified, and what N's `sysdeps` port patches; proposal 0012's `vfork` design. Run 2026-09-05, read out of `libc-2.28.so` from the pinned RPM with the cross `objdump`: the sentence is wrong (`finding=posix-spawn-clones-with-vm-and-vfork,vfork-is-syscall-58`). `posix_spawn` and `posix_spawnp` reach `__spawnix`, which calls `__clone` with `0x4111`, `CLONE_VM \| CLONE_VFORK \| SIGCHLD`, the child running `__spawni_child` on the parent's memory; and `__vfork` pops its return address, issues syscall 58 and pushes it back. So the design as written refuses the one `clone` shape `posix_spawn` makes, and a `vfork` without shared memory breaks the error report `__spawni_child` writes into the parent. |
| 47 | `spike/redzone-sync-fault/` | DR-0050 retired `-mno-red-zone` on the asynchronous path's evidence. Substrate N also takes synchronous faults (a lazy-commit first touch, `int3`) as NT exceptions dispatched on the faulting thread's own stack. Does that dispatch write into the 128 bytes below `%rsp`? | Proposal 0012's treatment of N's lazy commit and synchronous signals, and whether DR-0050 had to be reversed for N. Run 2026-09-05: it does not (`finding=redzone-intact-through-fault-dispatch`). A leaf that paints its red zone, stores to an uncommitted page, and has a vectored handler commit it and re-execute the store lost nothing over 1000 runs; the same through `int3` stepped over, nothing; NT placed the `EXCEPTION_RECORD` 568 bytes and the `CONTEXT` 1832 bytes below the interrupted `%rsp`, so the dispatch frame begins more than 400 bytes under the red zone. The control in which the handler itself flips one byte at `rsp-8` was caught in all 1000 runs at offset 8, so an intact result is a result. The review's argument that the synchronous path would clobber a leaf's temporaries was a reading of the ABI, and the measurement refutes it; DR-0050 stands for both paths. |
| 48 | `spike/peb-tls-bitmap/` | DR-0003 declined carrier C1 (a fixed `TlsSlots` index) because `TlsAlloc` draws from the same 64 bits and an injected DLL could take the slot. Can the process keep the slot for itself by setting its bit in the PEB's `TlsBitmap`? And where does the array end, so that the canary and the pointer guard the ABI keeps at fixed offsets from the thread pointer have somewhere to live? | DR-0101, DR-0106 and 0012 open question 1: N's thread-pointer carrier, and with it the GCC stack-protector guard offset. Run 2026-09-06: yes (`finding=three-bits-reserved-array-ends-at-the-thread-pointer`). The bitmap is the two words at `PEB+0x80` behind the `RTL_BITMAP` at `PEB+0x78`; `RtlSetBit(TlsBitmap, 63)` sets it, and seventy `TlsAlloc` calls afterwards hand out every other primary slot (61 of them) and nine expansion slots and never 63, with the bit still set; a DLL loaded after the reservation and ten more allocations change nothing. The slot still works as a carrier whatever the bitmap says: `TlsSetValue(63)` is what `%gs:0x1678` reads and a raw `%gs` write is what `TlsGetValue` returns; a new thread starts it at zero, so `tp_set` writes it before the thread runs user code, as every carrier in spike 6 needed. The array ends at the thread pointer: slot 0 is at `0x1480` and slot 63 at `0x1678`, 512 bytes apart, so `TP+8` is the first byte past it and none of the eighty indices the process holds lands there, which puts the canary and the pointer guard in the two slots *below* the thread pointer (`%gs:0x1670` and `%gs:0x1668`) rather than the two above. Reserved the same way: with bits 61 to 63 set and the earlier indices freed, seventy allocations and ten more after a second DLL load hand out none of the three, all three read back through one `%gs` load and agree with `TlsGetValue`, and all three start a new thread at zero. Bounded by what a Win32 probe can see: its CRT had already taken two slots, and a DLL injected before the process's first instruction is exactly what the substrate's start-up check refuses. |
| 49 | `spike/ntreadfile-uncommitted/` | Proposal 0012 § 4 makes every I/O go through a kernel buffer and gives the reason under N as a reading: the I/O manager probes a user buffer in kernel mode, where no vectored handler runs. Does an I/O call accept a lazily committed user buffer? | DR-0098's N-side premise. Run 2026-09-06: no (`finding=io-probe-fails-uncommitted-buffer-handler-never-runs`). `ReadFile` into reserved, `PAGE_NOACCESS` or half-decommitted memory fails with `ERROR_NOACCESS` (998) and `WriteFile` from reserved memory with `ERROR_INVALID_USER_BUFFER` (1784), on synchronous and overlapped handles alike, with the handler that would have committed the page entered zero times against one hit for a user-mode touch of the same buffer; the committed control reads 64 KB both ways. Zero-copy I/O under N is closed by the kernel, not by policy. One incidental lesson: at `-O1` the compiler moved the non-volatile store of the handler's bounds past the volatile touch, and the probe died until the pointers were declared `*volatile`. |
| 50 | `spike/futex-timeout/` | `FUTEX_WAIT` with a timeout is `RtlWaitOnAddress`, which wakes on the clock interrupt; 0012 § 9 says the kernel raises the resolution at start. How late is a timed wait before and after, and does it ever wake early? | 0012 § 9 and phase 4's futex timeout loop. Run 2026-09-06 (`finding=default-tick-lands-100us-wait-a-tick-late,raised-tick-sub-millisecond,early-wakes-observed`). At the 15.6 ms clock as found, a 100 µs wait returns 15.8 ms late and a 1 ms wait 14.9 ms late; after `NtSetTimerResolution` to the 0.5 ms the kernel offers, 0.4 ms late at the median (1.3 ms p99) and under 0.1 ms for the 1 ms wait; `NtDelayExecution` matches, so the clock is the limit. Some waits returned before their deadline (27 of 200 at 1 ms), which Linux never does: the futex path re-reads the clock and waits again before reporting `ETIMEDOUT`. The power cost of the raised resolution is not measured. |
| 51 | `spike/syscall-fs-census/` | 0012 § 8 says the userland N cannot take as shipped is a list: packages whose text carries a raw `syscall` outside glibc, plus the Go runtime. How long is the list, over every x86_64 package in Rocky 8.10? | DR-0102's rebuild scope and phase 4's worklist. Census launched 2026-09-06 (`run-census.sh`, detached, resumable, hours over 3780 packages); byte hits confirmed by disassembly with the cross objdump. The transcript lands when the run completes. On the 473-package smoke run: podman with 120 confirmed instructions and a Go runtime, glibc's own 745, and bash and coreutils with byte pairs that disassembly does not confirm. |
| 52 | `spike/foreign-host/` | Every transcript is one host. What does a second machine, the client's Citrix desktop first, need in order to run the native probes and send back something the project can read, given that only source code may be shipped? | The operator's `arena-on-version-floor` question of 2026-09-05. Built 2026-09-06: a manifest of fourteen probes, `make-bundle.sh` writing a zip of sources plus a generated `build.cmd` and `run.cmd` (refusing a zip with any executable in it), collect mode (`-i FILE`) in ten `measure.sh` scripts, and `collect.sh` rendering `results-host-<label>-<date>.txt` beside the host's own. Exercised end to end on this host through `cmd.exe`: fourteen built, ten rendered with findings matching the host's, four copied raw. No foreign transcript yet; the first will be the desktop's. |

Spike 1 was an afternoon and it decided a layer, against us. Spike 2 came back
yes and moved a question from whether to when. Spike 3 was the expensive one,
and a no there would have sent the program to the veneer-thunk fallback, which
is a different program; it came back yes, and left behind a measurement that
moves the red-zone question from the host onto our own layer. Spike 4 gated
nothing technically; it measured whether the whole edifice repairs what it was
built to repair, which is the question worth answering before anything large is
funded, and it came back yes. Spike 5 gated nothing in the end either: the
triple was decided on 2026-08-29 without waiting for it, and the count it
produced the same day is the size of the patch set that decision commits to.
One package, well inside the threshold DR-0001 set in advance. Spike 6 was the
follow-on spike 1 forced: with `%fs` gone something had to carry the thread
pointer, so it measured four `%gs` carriers against spike 1's own cases, found
three that hold, and let DR-0003 choose on ownership rather than on persistence.
Spike 7 is the follow-on spike 3 forced, and it ran on 2026-08-29: spike 3 caught
our own delivery destroying the red zone, and spike 7 asked whether delivery can
be made to reserve it. It can. A frame built 128 below the interrupted `%rsp`
left the red zone whole across every delivery, the handler still ran and
returned, and a value carried only in the red zone came back intact on the fa
side. That does not retire the `-mno-red-zone` flag on its own -- the flag stands
as policy either way, and what a reserved delivery costs in Cygwin's real
`sigdelayed` is unpriced -- but it establishes that an ELF-faithful repair at the
delivery site is available for WP-43 to weigh against the flag rather than
foreclosed.

## Spike 1, the thread pointe

Run 2026-08-29, and the answer is no. `WRFSBASE` is available on this host and
a base written with it addresses `%fs:0` correctly, so the failure is not at
the instruction. It is at the scheduler: anything that takes the thread off a
processor returns it with the base at zero, and the probe's cases that block,
yield, migrate, take a signal or get hijacked all fail on their first or second
check.

The case that settles what it costs makes no system call at all. It spins on
`RDFSBASE` while a burner sits on every processor, and it still loses the base,
in tens of milliseconds. A base cleared on the way back from a call could have
been re-established at the call site; a base cleared by preemption cannot,
because preemption has no call site. So the two obvious repairs are both closed
and this reaches the toolchain layer rather than merely adding work to it, as
the milestone said it would.

`spike/fs-base-persistence/results-2026-08-29.txt` is the transcript and that
spike's README reads it. What replaces `%fs` was reserved to the operator by
`AGENTS.md` and is now settled: a follow-on spike, `spike/gs-thread-pointer/`,
measured four `%gs` carriers the same day against these same cases, and DR-0003
took carrier C3 — a runtime-owned thread pointer kept below the stack base in
Cygwin's `_my_tls` shape and reached through `%gs`. Where the `%fs` base came
back zero on the first check of every descheduling case, the `%gs` carriers
returned their pointer across 17.6 billion checks with none, at about a sixth of
emulated TLS's per-access cost. That spike's README reads its own transcript.

## Spike 2, mapping and jumping

Run 2026-08-29, and the answer is yes. A PE stub reserved the span a static
`ET_EXEC` asks for, committed and protected one region per `PT_LOAD`, built
the stack the psABI describes, and jumped to `e_entry`; the image ran at its
link address, read its own segments, walked the auxv it was handed, and
returned. Three of the four mapping cases did that, both controls were
refused, and every protection Windows reported back was the one asked for --
confirmed by fault probes rather than only by `VirtualQuery`, because a spike
that reads back its own request has measured its own request.

Two findings come free with it. `.bss` costs nothing, because Windows hands
back freshly committed pages already zeroed and the word past `p_filesz` read
as zero without the stub touching it. And a link base that is page-aligned but
not granule-aligned costs address space rather than correctness: an image at
`0x8048000` reserves from `0x8040000` and spends 32 KB below itself on
nothing.

The fourth case is the finding that matters. At a `p_align` of `0x200000` --
`ld`'s default, and so what a vendor binary is expected to carry -- each
segment lands on its own 2 MB boundary and the span inflates from 24 KB to
4 MB without gaining a byte of content. At `0x400000` that span is not
available: the free run there measured between `0x200000` and `0x260000`
across runs, never the `0x405000` wanted, and the reservation was refused
twenty times in twenty. The same geometry at `0x10000000` mapped and ran, so
the arithmetic is right and the address is the problem.

What takes the low addresses is Windows' bottom-up allocator, and the spike
caught it in the act: in the case that mapped high, the stub's own stack
allocation, requested with no base, came back at `0x400000`. Anything in the
process that allocates before the image's span is claimed can take part of it,
and a Cygwin runtime allocates before `main`.

So the question moves from whether to when, and it lands on WP-41: a non-PIE
image's span has to be reserved before the runtime under the stub warms up.
Whether a PE TLS callback or an image entry point is early enough is not
measured here, and it should be measured before that package is written rathe
than discovered inside it. `spike/map-and-jump/results-2026-08-29.txt` is the
transcript and that spike's README reads it.

## Spike 3, the ABI crossing

Run 2026-08-29, and the answer is yes on the crossing and no on the red zone,
which are less related than they sound.

The crossing holds in both directions at one function's width. A System V
caller passed six integers, eight doubles and two stack arguments into a
`sysv_abi` entry point that then made five descents into Microsoft x64 --
`GetCurrentThreadId`, `VirtualQuery`, `QueryPerformanceCounter`, the runtime's
`snprintf`, `Sleep` -- and returned with every System V callee-saved registe
intact. Going the other way, a Microsoft caller reached System V code with all
eight callee-saved GPRs and all ten callee-saved XMM registers intact, which is
the direction that could leak, because `%rsi`, `%rdi` and `%xmm6` through
`%xmm15` are callee-saved to a Windows caller and volatile to a System V
callee. Windows called in four ways -- a thread start, an APC, a vectored
exception handler, a Cygwin signal handler -- and each reached System V code one
frame down. A null store in Microsoft code beneath a System V frame came back
as SIGSEGV and left by `siglongjmp` past that frame, which is the SEH claim
AGENTS.md forbids assuming, tested at the only width anyone has tested it.

The red zone is destroyed, and the finding is which layer destroys it. Windows
does not: preemption with a burner on every processor moved nothing, two
thousand suspend-and-restore hijacks moved nothing, and Windows' own exception
dispatch left the nearest 320 bytes below `%rsp` untouched, which is well
outside the 128 the psABI reserves. Cygwin's signal delivery takes the word at
`%rsp-8` on every one of two thousand deliveries and everything down to the
1024 bytes watched, because it hijacks the thread and builds the handler's call
frame at the interrupted stack pointer.

So `-mno-red-zone` throughout stands, and it is not free: gcc gives a
`sysv_abi` leaf a red zone on this target and the flag turns `-32(%rsp)` into
`subq $32, %rsp`. But the code breaking the guarantee is Cygwin's own delivery
path, which this project already intends to modify, so there is a second option
beside the flag and choosing between them is the operator's. `AGENTS.md` records
it as open.

`veneer:spike/abi-crossing/results-2026-08-29.txt` is the transcript and that spike's
README reads it, along with what a one-function measurement does not reach:
unwind data, `DllMain`, and the runtime actually rebuilt.

Re-verified 2026-08-31 on the primary root, after the environment moved
(DR-0038) and the verdict briefly read `no` there: the flip was gcc 14 eliding
the null-store specimen, not the host, and with the specimen repaired every
crossing case passes under 3.6.10 too. `results-2026-08-31.txt` is that
transcript. It also reads the red zone differently, because the Cygwin unde
it is different: 3.6.10's stock delivery leaves the reserved 128 bytes alone
where 3.0.7 took `%rsp-8`. The paragraphs above describe 3.0.7; DR-0006 was
decided on that measurement, and reading the new one against it is the
operator's. The spike README's re-verification section carries the detail.

## Spike 4, the payoff

Run 2026-08-29, and the answer is yes. This is the spike that asks whethe
building the thing repairs what it was built to repair, and the sharpest form
of that question is a string: does rpm write `libc.so.6(GLIBC_2.2.5)(64bit)`
when it is pointed at a library we made up.

It does, and not merely in the right shape. The line a synthesized `libc.so.6`
carrying one verdef node yields from el8's own `elfdeps` is byte-identical to
the line el8's `libc-2.28.so` yields from the same binary, and to the
requirement a synthesized consumer emits from its `.gnu.version_r`. The edge
closes: what the library provides is what a program asks for, spelled the same
way at both ends, which is the whole of what
`doc/history/symbol-versioning-formats.md` says PE can never do.

Synthesized again with el8 libc's whole node list -- 29 of them, `GLIBC_2.2.5`
through `GLIBC_2.28` and `GLIBC_PRIVATE` -- the provides set is identical to
the vendor's, thirty lines each. So one node proves the mechanism and the
ladder prices the veneer: a package requiring `GLIBC_2.14` is not satisfied by
a library defining only `GLIBC_2.2.5`, and `rpm`'s own binary needs three of
the nodes.

One finding is a trap worth carrying forward. A versioned provide is read off
the base verdef node, not off `DT_SONAME`; a library that gets those two
strings out of step provides under one name, is required under the other, and
says nothing about it. In a library a linker produced they are always the same
and the question never arises, which is exactly why a synthesized one will get
it wrong. Measured here, by a fixture built to get it wrong on purpose.

What the verdict does not buy is automatic generation on the build host. That
needs an rpm carrying `elfdeps` and `fileattrs`, and Cygwin's ships neither, so
the stage 0.5 admission survives -- changed from a format impossibility into an
installation gap. `spike/versioned-libc/results-2026-08-29.txt` is the
transcript and that spike's README reads it.

## Spike 5, the target triple

The triple has four fields and they are not equally load-bearing. The fields
are `cpu-vendor-kernel-os` in `config.sub`'s own naming, none of them called
`abi`; only the kernel and the libc are consulted by the machinery that would
break, and `vendor` is passed through untouched and read by almost nothing. So
the triple is `x86_64-elfsysvnt-linux-gnu`, decided on that reasoning and
recorded in DR-0001: it puts the honest name where it costs least and leaves
`linux-gnu` standing where configure actually looks. Neither of the two
load-bearing fields is thereby a lie, which DR-0005 settles and
`doc/design/target-definition.md` states: `gnu` is glibc exactly, and `linux`
is the Linux kernel ABI bounded at raw syscall dispatch, which this project
satisfies by rebuild instead.

Buildroot has shipped `x86_64-buildroot-linux-gnu` on the same grounds for ove
a decade, and crosstool-NG ships `unknown` in that slot, so neither the shape
nor the length is novel.

What the vendor costs is the open question. A package that matches `*-linux-gnu`
is unaffected; one that matches the literal `*-pc-linux-gnu` o
`*-unknown-linux-gnu` misses, silently, and takes a configure branch nobody
intended. Such packages exist. How many is the size of the patch set the
decision commits to, and guessing at it is precisely the habit these spikes
exist to break.

The script takes the `config.sub` shipped by each package in the el8 source
set, feeds it three candidates, and records the canonicalized output of each:
the masquerade `x86_64-pc-linux-gnu`, the vendor-honest
`x86_64-elfsysvnt-linux-gnu`, and the kernel-honest `x86_64-pc-elfsysvnt` as a
control. The control was put there expecting a rejection, and a preliminary run
over nine local `config.sub` vintages says otherwise: pre-2020 files do not
validate the os field at all, and the 2021 file accepts `elfsysvnt` by matching
`elf*`, the entry that exists for bare-metal ELF targets. Silent acceptance
into `config.gcc`'s bare-metal branch is a worse outcome than a refusal, which
is the argument for the vendor field restated on firmer ground. The same pass
greps the set for literal vendor matches in `configure.ac`, `configure`, and
any hand-written `case $host`. The transcript is two counts and the offending
package names.

A verdict of a handful is a patch set. A verdict in the hundreds argues for the
masquerade, and the honest name moves to `EI_OSABI`, the `.note.ABI-tag`, the
dynamic linker SONAME, and `uname`, which is arguably where it belonged anyway:
those are read at runtime, by tools, whereas the triple is a build-time label
that no shipped artifact consults. Where the line between the two sits is in
DR-0001, as a share of packages rather than as an adjective, written before the
count so that the count cannot be read to suit.

It came back a handful, and a small one. Over all 2893 source names, the
masquerade and the candidate get identical verdicts from every one of the 1193
vendored `config.sub` files, and exactly one package carries a live literal
host test: `flac`, whose `configure.ac` gates `FLAC__SYS_LINUX` on
`*-pc-linux-gnu)`. The transcript is
`spike/triple-fidelity/results-2026-08-29.txt` and that spike reads the
eighteen other matches, which are comments and test fixtures.

Path length is settled and needs no spike. On this machine the deepest
installed path carrying a triple is a libstdc++ policy header at 132 characters
POSIX, 146 as Windows sees it through the Cygwin root; the ten characters the
longer vendor adds put the worst case at 156, against a `MAX_PATH` of 260. A
build tree is deeper than an installed tree and a gcc bootstrap stacks stage
directories on top, but a hundred characters of headroom absorbs that. Measured
2026-08-20.

## Spike 6, the %gs thread pointe

Run 2026-08-29, and the answer is yes. Spike 1 took `%fs` away that afternoon
and left the thread pointer without a carrier, so this measured four: a fixed
`TlsSlots` index, the word below the stack base in Cygwin's `_my_tls` shape,
`NtTib.ArbitraryUserPointer`, and the PE TLS directory. Three held identically
across spike 1's twelve persistence cases -- 17.6 billion reads with none lost,
through 45 million real context switches -- and each read a glibc-shaped block
back correctly. Persistence did not choose between them; ownership and precedent
did, and DR-0003 took carrier C3, the word below the stack base that Cygwin
already keeps by this chain and the runtime owns. The access costs one extra
load over a global, roughly a sixth of what emulated TLS costs, which is the
comparison that matters once spike 1 has removed the native one.
`spike/gs-thread-pointer/results-2026-08-29.txt` is the transcript and that
spike's README reads it.

## Spike 7, reserving the red zone

Run 2026-08-29, and the answer is yes. Spike 3 found the red zone destroyed and
named the layer: Cygwin's own signal delivery builds the handler's frame at the
interrupted stack pointer and takes `%rsp-8` first, where Windows itself leaves
the nearest 320 bytes alone. That left `-mno-red-zone` standing as policy and one
question beneath it, whether delivery could instead reserve the 128 bytes before
it builds, the way a Linux kernel does, and let the flag come off. This spike
asked exactly that, without rebuilding `cygwin1.dll`: spike 3 already built the
thread-hijack delivery it measured against, and this put a handler frame back on
top of that hijack two ways -- at the interrupted `%rsp`, which had to clobber,
and 128 below it, which must not -- and watched the red zone under each. The
naive frame lost the word at offset 8 on every delivery, reproducing spike 3's
finding and proving the model destroys the red zone where the real path does. The
reserved frame left the 128 whole, nearest write at offset 136, on every
delivery; the handler ran and returned each time; and a value carried only in the
first red-zone word came back intact on the far side, while the same value broke
under a naive delivery. It is a model of delivery rather than the real
`sigdelayed`, in the way `spike/gs-thread-pointer/` measured a stand-in fo
`_my_tls`, and WP-43 re-measures the real path and prices what reserving costs
there. What it gates is narrow: the yes lets a delivery-site repair that honors
the red zone for compiled and hand-written code alike be weighed against the
flag, rather than foreclosed; it does not retire the flag, which stands as policy
until WP-43's record. By the discipline DR-0001 and DR-0003 followed that record
is taken against this spike's transcript rather than ahead of it.
`spike/redzone-delivery/README.md` carries the mechanism, the cases and the
reading; `results-2026-08-29.txt` is the transcript.

## Spike 8, reading a zeroed `%fs` base

Run 2026-08-29, and the answer is that it faults and a handler resumes from it.
Spike 1 measured the base and found it zero after anything that deschedules the
thread; it never measured the next instruction, and that gap turns out to
decide something larger than its size suggests.

Vendor binaries are built for real Linux and reach for TLS through `%fs`, and
no toolchain choice of ours touches a binary that arrives already linked. The
operator's direction is that load-time rewriting is the fallback for that case,
which puts the weight on finding every access site rather than on rewriting
one: in a linked executable the local-exec relocations have been consumed, so
a rewriter is reduced to scanning bytes, and code-versus-data on x86-64 has no
sound general answer. It will miss sites. What a miss costs is what this asks.

If an access through the zeroed base faults, and a vectored handler can
identify the instruction, emulate it through the C3 carrier and resume, then
the rewriter is an optimization over a sound fallback and is allowed to be a
heuristic. If it reads something instead, the rewriter has to be exhaustive,
nothing here can make it so, and the fallback narrows to binaries we built
ourselves — which is barely a fallback, since a binary we built is one we
could have compiled correctly.

It takes nine of spike 1's twelve cases unchanged, the spinning one included,
since that is the one with no call site to hook, and its own README says where
the two lists part. Ten of its twelve events lose the base and an access
afterwards raised an access violation every time; the other two are the two
descheduling cases spike 1 also recorded as surviving, and nothing read through
a zeroed base anywhere in the run.

The finding the fallback rests on is what Windows hands the handler. With the
base at zero the effective address is the offset, so the faulting address is
the TLS displacement itself and the handler never computes an address: `0x0`,
`0x40`, `-0x8` and `-0x18` were each reported as themselves. What is left is a
length and a destination register. Nine forms were decoded, emulated through
DR-0003's carrier C3, and resumed correctly, each checked both against the
value the block held and against a landing pad the probe computed beside the
access; the interrupted code got its other registers and its carry flag back
intact. The case that decides it is the one with no call site, and it held:
1,304,000 reads under a burner on every processor, every one a fault, every one
correct, and 15,662,000 more across 24 threads with none wrong.

So the rewriter may be a heuristic, and that is the verdict — over the
data-movement forms. The qualifier is the spike's second deliverable and it is
not small: a read-modify-write access, `addl $1, %fs:-0x4` and its relatives,
is refused by name rather than guessed at, because emulating it means emulating
`EFLAGS`. A missed site of that shape is a `SIGSEGV` rather than a slow
success. Closing that gap is a choice between emulating the flags and requiring
the rewriter to be exhaustive over exactly the forms it is least able to be
exhaustive about, and what it costs turns on a census nobody has run. A handled
fault costs about 2.2 microseconds against half a nanosecond rewritten — three
orders of magnitude, which is fine for a miss and would be ruinous as a policy.

`spike/fs-base-fault/README.md` carries the method, the reading, and what the
measurement does not reach; `results-2026-08-29.txt` is the transcript. The
verdict goes to `veneer:doc/design/proposals/0003-vendor-binary-tls-rewriting.md`,
which was written against it rather than ahead of it.

Nothing in phase 1 waited on this. What waits is the loader's answer to a
vendor binary, which is phase 3 at the earliest, and the rewriter is not yet
cut into a work package.

## Phase 0 of proposal 0011

Rows 35 to 40 are a block apart from everything above them. They belong to
proposal 0011, which designs a kernel at the syscall boundary rather than at
the C library's, and that proposal declares itself conditional on all six —
its own words are that every NT behaviour it leans on is a reading of
documentation, of ReactOS, of Wine, of `wepoll`, or of Cygwin's source, and
that phase 0 exists to turn each of them into a transcript. They ran on
2026-09-04, in parallel, one worktree each, and all six came back yes.

Two of them came back yes only because a second candidate was measured. Spike
35 found the address space cloning and the clone refusing to hold a thread,
which read as a design-ending no; `RtlCloneUserProcess` carries what
`NtCreateProcessEx` would not, and the ladder's rule that an empirical tier
narrows only after every surviving candidate is measured is the whole reason
that was found rather than reported. Spike 36 reached AFD's door with a
refused open packet and stopped there on its first pass; two layout faults
were behind the one status, and past them the readiness primitive the whole
`epoll` design rests on works end to end. A first pass on either would have
recorded the wrong verdict, in the expensive direction.

Three findings are worth carrying forward whatever the substrate. The arena
replaces placeholders at 4 KB on this build where 64 KB is what Microsoft
documents, which would loosen the segment congruence rule if it held
generally, and one machine is not "generally". Hijack delivery defers rather
than fails for a thread inside a syscall, which is the behaviour the design
wanted. And a tree carrying WSL's metadata reads correctly under WSL and not
under Cygwin, so verification criterion 3 as written cannot pass on this host
— a fact about Cygwin's ACL-derived `stat`, not about NTFS.

The measurement open question 1 waits on is spike 40's: a syscall under the
hypervisor costs a median 5.0 µs here against 679 ns for a trapping NT
syscall, so the substrate clears the proposal's own 10 µs threshold. That
settles the empirical half of the substrate choice and not the other half,
which is whether the operator accepts the hypervisor feature as a
prerequisite. That is a value, and it is the operator's.

The operator's answer to that half came on 2026-09-05: build both substrates,
because different customers need one or the other — a locked-down or
hypervisor-less fleet takes N, a fleet that must run el8's shipped binaries
unmodified takes H. Proposal 0011 anticipated this: § 3 puts one core behind a
nine-call substrate interface so the second substrate is an implementation, not
a rewrite. Spike 41 followed directly from the decision. Eight of the nine
calls were exercised across spikes 35 to 40; `thread_interrupt` under H was the
one left unproven, and it is the call the two-substrate bet most depends on,
since a leak there is where the core would grow N-shaped and the interface stop
being two-sided. It came back the equal of N's hijack: `WHvCancelRunVirtualProcessor`
forces a running vCPU out, the vCPU resumes intact, register injection delivers
as N's context rewrite does, and WHP closes the cancel-versus-run race itself.
The interface is two-implementable at its hardest call; building both is on a
measured footing.

## After the spikes

No package waits on a spike. All eight have run and the recommended path
survived the one that could have taken it away; the seventh gated only whethe
a delivery-site red-zone repair is available to weigh against `-mno-red-zone`,
and the eighth gated only what a load-time rewriter for vendor binaries is
allowed to be, which is phase 3's question at the earliest. Nothing starting
now depends on either.
`ROADMAP.md` inventories the work and `IMPLEMENTATION-PLAN.md` cuts it into
packages; the sketch below is the shape both of them fill in.

The target triple wanted deciding before the first package was built, and it
was, on 2026-08-29. The TLS model wanted deciding on the same footing, and it
was, the same day: spike 1 came back no that afternoon, the gs-thread-pointe
spike measured the replacements, and the operator settled DR-0003 on carrie
C3 — a runtime-owned thread pointer through `%gs`. The toolchain can now be
configured around a named thread pointer. The toolchain follows, which is
otherwise routine
cross-toolchain work. Then the loader, where musl's `dynlink.c` is the working
model and the verdef and verneed matcher is the part musl leaves out.
`elfsysv1.dll` and the libc veneer come after the loader can run something. The
`r_debug` rendezvous can start as soon as there is a loader to announce
objects, and it should, because the alternative is debugging a world Windows
tools cannot see.
