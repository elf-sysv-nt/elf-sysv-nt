# reent-veneer-face-exports -- the veneer's thunk keys are all real face exports

A rung of WP-56's reent-tls-bringup road (`acceptance/to-green.tsv`, item 2 of
`acceptance/reent/README.md`). `spike/reent-veneer-thunk` pinned the link-time
shape of one runtime-resolving thunk: it names its target as a `.rodata` string
and resolves that name at run time by walking `elfsysv1.dll`'s PE export
directory (the WP-27 crossing ABI). That spike measured a single symbol,
`strtol`, and said nothing about the other thousand forwards -- nor whether the
name each thunk keys on is one the face in fact exports. A key that named no
face export would make the run-time walk return null and the thunk fault,
whatever its link-time shape held.

This spike closes that gap across the whole forward set. It reads two committed
truths rather than a run:

  - the keys: `veneer/libc/build-libc` emits `libc-forward.tsv`; every FUNC row
    classed `forward-same` or `forward-alias` carries in its target column the
    export name its thunk hands the resolver.

  - the face: `runtime/face/face.tsv` is the committed export table `gen-din.sh`
    turns into `face.din`'s `EXPORTS` -- the names in `elfsysv1.dll`'s PE export
    directory the crossing's walk searches. Building the DLL itself needs the
    heavy WP-26 winsup tree; its export *names* are this committed table, so the
    cross-check needs no native build.

## Findings, reproduced (`measure.sh`, 2026-09-01)

    func_forward_keys_extracted=973
    all_keys_are_face_exports=yes   (all 973 keys among 1767 face exports)
    exemplars_present=yes           (strtol and memcpy)

Every one of the 973 unique FUNC forward keys is a name the face exports, so the
run-time resolver finds a face export for each. `strtol` (the reent-consuming
exemplar) and `memcpy` (the bindings exemplar WP-53 certifies) are both among
the keys and both face exports.

## What this does and does not settle

It settles that no forward thunk keys on a name the face lacks: the resolution
table is complete on the name axis, for the whole set rather than one symbol. It
does not run the resolver, map the face, or cross the loader -- reaching the
face and returning a reent-consuming result across the crossing is item 3,
behind the built face DLL and the reconciled real-process stub. This is the
name-completeness half of item 2, measured off committed data so it reproduces
wherever the cross toolchain is installed.

`measure.sh` SKIPs (verdict yes, exit 0) when the cross toolchain is absent,
`build-libc`'s output being an uncommitted build product, as the crossing spikes
do. Registered in `test/spike-regen.tsv`.
