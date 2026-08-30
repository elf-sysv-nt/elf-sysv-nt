# DR-0013 — the version map's companion set spans glibc, libnsl and libxcrypt

Status: accepted  ·  ratified 2026-08-30 (DR-0036)
Date: 2026-08-30
Deciding: WP-51 implementation, a defensible call under the non-reserved
decision policy in AGENTS.md; the operator may ratify or reopen
Proposal: none; taken when WP-51 had to name where each companion's version
nodes come from

## What was decided

The version map is extracted from nine libraries — `libc.so.6` and the eight
companions the plan names — and those nine do not all come from the `glibc`
package. `glibc-2.28-251.el8_10.40` carries `libc.so.6`, `libm.so.6`,
`libpthread.so.0`, `libdl.so.2`, `librt.so.1`, `libresolv.so.2` and
`libutil.so.1`. The other two are separate packages, pinned in
`veneer/version-map/packages.tsv` at the builds that match glibc's:

- `libnsl.so.1` from `libnsl-2.28-251.el8_10.40`, glibc's own NIS compat
  library split into a package of its own on el8.
- `libcrypt.so.1` from `libxcrypt-4.1.1-6.el8`, which is not glibc at all. el8
  builds glibc `--disable-crypt` and ships the crypt interface from libxcrypt
  instead.

The map records, faithfully, whatever version nodes each vendor library
defines. For `libcrypt.so.1` that includes an `XCRYPT_2.0` node alongside its
`GLIBC_2.2.5` through `GLIBC_2.4` ones. The extractor does not filter it to the
GLIBC nodes, because the map's job is to say what the vendor library provides,
and a package requiring `libcrypt.so.1(XCRYPT_2.0)(64bit)` is a real package.

## Why it is load-bearing

WP-52 partitions this map and WP-53 and WP-54 build the ELF libraries from it,
so where a companion's nodes come from is a fact those packages inherit rather
than rediscover. Two consequences follow that would be wrong if the map assumed
one glibc source.

The crypt face of the veneer is not glibc-shaped. If WP-54's `libcrypt.so.1`
reproduced only glibc's crypt nodes, it would still fail to satisfy an el8
binary that requires `XCRYPT_2.0`, and it would do so silently — the load would
resolve the GLIBC symbols and the `XCRYPT_2.0` requirement would go unmet.
Recording the node in the map is what puts that requirement in front of WP-54
rather than behind it.

The companion partition follows el8's packaging, not a modern glibc's. Later
glibc merged `libnsl` and the crypt library back in or dropped them; el8 has not,
and the plan's WP-54 already says the partition follows el8's. This record ties
that statement to the three specific packages the map is measured from, so the
pin is a decision with a reason rather than an accident of what happened to be
fetched.

## Why extracted with our own parser rather than read from readelf

The map is produced by parsing the ELF directly — `.dynsym`, `.gnu.version`,
`.gnu.version_d`, `.dynamic` — and not by scraping `readelf` output. The reason
is the same one that makes the reproduce test meaningful: the certification
diffs the map against `readelf -V` and `readelf --dyn-syms`, and that is only a
check if the two were produced by different code. A map scraped from readelf and
then checked against readelf would certify nothing. The vendor libraries are the
authority; readelf is the independent second reading.

## What it does not decide

Whether the veneer ultimately provides `XCRYPT_2.0`. The map carries it because
the vendor defines it; whether WP-54's `libcrypt.so.1` implements a body behind
it, forwards it, or stubs it is WP-52's classification and WP-54's build, not
this record's. What is settled here is only that the node is in the map and not
filtered out of it.

Which glibc objects are companions. The map covers the nine libraries a vendor
binary's `DT_NEEDED` reaches through the plan's set. glibc also ships
`ld-linux`, the `libnss_*` modules, `libmvec`, `libanl` and others; whether any
of those joins the companion set is WP-54's scope call, and they are not
extracted here.

## What it costs to reverse

Cheap. The pins are three rows in `packages.tsv` and the manifest is nine rows
in `libraries.tsv`; adding, dropping or repinning a library is an edit to those
and a rerun of the extraction. Nothing downstream has been built against the map
yet. Reversal is a new record pointing back here, not an edit to this one.
