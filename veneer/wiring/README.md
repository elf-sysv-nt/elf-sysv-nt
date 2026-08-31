# Wiring the bodies, in slices (WP-56)

Work in progress. The forwards become real resolutions into `elfsysv1.dll`
and the shims become translations through WP-55's tables, sliced by
subsystem, with the slice order taken from spike 12's demand ranking.

The census (spike 12) has not yet run over the full el8 set, so the
ranking that orders the slices does not exist yet. First act of this WP:
run the census to completion (a long, resumable, background job logged
under `a/build-logs/`), then cut the first slice from its
`demand-ranking.tsv`.

Status: census run in flight under a/census-work (single background job,
resumable, logged to a/build-logs/wp56-wiring-bodies.log; a duplicate run
was retired); no slices cut.
