# WIP -- the acceptance verdict distinguishes a filled stub from a failing one

WP-56, the DR-0052 follow-up. The ctype-table fill gave `__ctype_b_loc` and its
two case-map kin a synthesized, certified body, but the acceptance harness still
reads `classification.tsv` -- which correctly still says bucket-4 stub, because
the export surface still does not carry the name -- and so counts the filled
symbol against bzip2 as if nothing stood behind it.

This increment teaches the harness the distinction: a bucket-4 stub named in the
wiring layer's filled manifest is reported `filled`, not `stub`, and does not
hold a package short of `ready`. The manifest is generated beside the body it
describes, so it cannot drift from the `.symver` bindings it lists.

Removed when the work lands.
