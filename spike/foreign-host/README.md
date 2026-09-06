# The foreign-host bundle

Every transcript under `spike/` so far is one host, one Windows build, one
AMD part. This directory turns the native probes into something a second
machine can run without the project's toolchain, and turns what comes back
into transcripts beside the host's own. The first such machine is the
client's Citrix developer desktop, which the operator asked about on
2026-09-05 (`arena-on-version-floor`); the same bundle serves any host.

The rule the bundle is built around is the one the operator set the same
day: **only source code is shipped.** The zip carries C sources, two
`.cmd` scripts, a PowerShell script and a README. `make-bundle.sh` refuses
to write a zip that contains an executable, and the client compiles with a
mingw-w64 gcc unpacked in their profile.

## The pieces

- `manifest.tsv` names the spikes that travel: the sources to copy, the
  libraries on the link line, the arguments `run.cmd` passes (sized for a
  managed desktop, not this host's defaults), and whether that spike's
  `measure.sh` can render a transcript from the brought-back output.
- `make-bundle.sh` assembles the zip under `$ELFSYSVNT_ROOT/a/bundle/`
  from the manifest: `src/<spike>/` per row, a generated `build.cmd` (one
  gcc line per probe, `-I` on the spike's directory, into `bin\`), a
  generated `run.cmd` (a header naming the host, then one `out\<spike>.txt`
  per probe, with `.err` beside it, then the WHP policy probe), the
  policy probe itself, and `README-client.txt` as the zip's `README.txt`.
- `README-client.txt` is what the client reads: what the probes do and do
  not do, what a security product might notice, and the three commands.
- `collect.sh <out-dir>` renders the transcripts. For each collectable
  spike it runs `measure.sh -i out/<spike>.txt`, passing the arguments the
  probe recorded on its `# args:` line as the renderer's options, and
  writes `spike/<spike>/results-host-<label>-<date>.txt`; the others it
  copies to `raw-host-<label>-<date>.txt`. The label defaults to the
  host's name from the header.

## Collect mode

Ten `measure.sh` scripts gained `-i FILE, --input=FILE` on 2026-09-06:
arena, peb-tls-bitmap, ntreadfile-uncommitted, futex-timeout,
redzone-sync-fault, whp, whp-clone-host, whp-gpa-map, whp-pagetable-fork
and whp-partition-cost. With `--input` the script neither builds nor runs;
it reads the file as the probe's output and takes the header facts (host,
Windows build, compiler, probe version) from the `# key: value` lines
`run.cmd` wrote at the top. Without it nothing changed: every transcript
those scripts had already produced regenerates as before, which
`test/t3-regen.sh` confirmed the same day.

The transcripts a foreign host produces are differentials in the sense of
the spike contract: named `results-host-<label>-<date>.txt`, outside the
`results-<date>.txt` glob the regeneration runner diffs, so a rerun here is
still judged against this host's transcript and the foreign one sits
beside it as evidence about another.

## What a run on the Citrix desktop would tell us

Not the version floor unless that desktop is on it (the header says); a
second kernel build, probably an Intel part, and a managed policy surface.
The WHP probes will most likely report the platform absent, since a VDA
rarely has nested virtualisation, and that is the answer 0012 § 8's "both
substrates offered" needs recorded, not a failure. `arena` on a second
build is the first portability data point the placeholder design has.

## Tested

The bundle was assembled, unpacked, built with the cross gcc through
`cmd.exe`, run, and collected on this host on 2026-09-06: fourteen probes
built, fourteen outputs came back, ten rendered to transcripts whose
findings matched the host's own, four copied. The futex-timeout finding
differed under the load of a census running at the time, which its README
already warns of.
