# WIP — running the dynamic image's DT_INIT chain before entry (WP-56)

The dynamic crossing (`dyn_exec_link`, DR-0058) leaves the main image's GOT and
PLT resolved against the runtime but does not run the image's constructors. Both
`dyn_exec.h` and this package's README name that as the next step: "Running the
image's DT_INIT chain between the link and the entry is the step after this
one." This increment is that step.

`dyn_init.{c,h}` runs the main image's DT_PREINIT_ARRAY, DT_INIT, and
DT_INIT_ARRAY in the order the ABI fixes — the same order WP-38's `dl_run_init`
runs for the dl graph — over the exec package's own pair (`elf_parsed` +
`elf_mapping`), which is the shape the crossing already holds. The stub calls it
after `dyn_exec_link` and before `elf_enter`.
