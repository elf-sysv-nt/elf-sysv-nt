# The export inventory (WP-20)

The outward surface of `elfsysv1.dll`, one row per export, generated from
Cygwin's `cygwin.din` rather than hand-kept. WP-21 wraps every function here as
a down-call, and WP-51 maps every name into the veneer, so both read this one
file; two copies of an export list drift, and the drift shows up as a symbol
that links at build time and is gone at run time. Generating it from the source
and testing that the generation reproduces is what keeps the one list honest.

## Source of record

`winsup/cygwin/cygwin.din` at Cygwin **3.6.10**, `newlib-cygwin` commit
`b11613e47`. That the runtime is based on 3.6.10 and not on the pinned 3.0.7
verification root is DR-0007; this inventory is the first artifact that commits
to it, and every count below is a property of that ref. A different ref is a
different surface and wants a new generation, not an edit to this file.

## The files

`extract-exports.sh` reads the `.din` and writes the rows. `cygwin-exports.tsv`
is what it produced against the ref above, committed. `t/reproduce.sh` pins the
ref, reruns the extraction, and diffs it against the committed file; a
mismatch, or a checkout moved off the ref, fails it.

## The rows

Tab-separated, no header, in the `.din`'s own order:

    name   kind          sigfe                              alias
           data | func   SIGFE | NOSIGFE | SIGFE_MAYBE      target | -
                         | none | -

`sigfe` is `-` for a data export and the signal-frame class for a function:
`SIGFE` wraps the call in a signal frame, `NOSIGFE` does not, `SIGFE_MAYBE`
decides at run time. `none` is a function the `.din` leaves unannotated; there
is exactly one today, `glob_pattern_p`, and it is carried as its own value
rather than folded into `NOSIGFE`, because what an unannotated export means is
WP-21's call to make and not this extractor's to assume. `alias` names the
target when a row is an alias (`sys_errlist = _sys_errlist`), and the `.din`
writes the `=` attached or spaced, both of which normalise here.

## The counts, at `b11613e47`

    total          1767
    data             38
    func           1729
      SIGFE        1001
      NOSIGFE       726
      SIGFE_MAYBE     1
      unannotated     1
    aliases         101

`extract-exports.sh --terse` prints exactly these, which is the form to quote.

## Regenerating and testing

    ./extract-exports.sh -o cygwin-exports.tsv     # regenerate
    ./t/reproduce.sh                               # certify it reproduces

The extractor takes `--din` for a `cygwin.din` elsewhere and `-o -` for stdout;
`--help` lists the rest. It fails rather than drops a line it cannot classify,
so a format change in a future `.din` surfaces as an error at generation rather
than as a missing export at run time.
