# Installation (WP-63)

The installers before this one each place a layer inside the toolchain
tree: `build-csu` seeds the sysroot, `install-veneer` puts the libc face
beside it, `kernel-headers` lays the uapi set below. None of them touches
the machine the platform will actually run on. `elf-install` does. Given a
payload list of built files, it puts them under a root, seeds the loader's
search configuration, writes the marker `config.guess` falls back to where
no compiler exists (the obligation WP-11 left open), and rebuilds the
ldconfig cache with WP-62's `elf-ldconfig`.

What separates it from a copy loop is memory. The tool records every path
it manages in `etc/elfsysvnt/manifest` under the root, and that manifest
is its only memory; DR-0034 explains why it has no other. On each run the
old manifest is read first, the new state is placed, and whatever the last
run managed that this one does not is removed — then pruned, so a
directory emptied by a retirement does not linger. Because the manifest is
also an input someone could have edited, its entries are checked before
they are believed: an absolute path, or one that climbs through `..`,
is refused with a warning rather than handed to `rm` under the root.

## Idempotency, concretely

AGENTS.md states the policy; this is what it means here. Derived
configuration — `etc/ld.so.conf`, the drop-in under `ld.so.conf.d`, the
release marker, the cache — is reseeded from the pristine templates in
`templates/` on every run, with the managed settings (the library
directories, the revision) reapplied on top. Nothing is ever edited in
place. A hand edit, or a value injected by a broken run of something else,
lasts exactly until the next run. Every file lands whole: staged beside
its destination under a `.elf-install-tmp` name, then renamed. An
interrupted run therefore strands only staging files, and reaping those is
the first thing the next run does, before it reads anything.

Two runs with the same inputs leave a byte-identical tree. Two runs with
changed inputs leave only the new state. Both claims are tests in `t/`,
not sentences.

## Endpoint protection, a deployment step

The loader maps anonymous executable memory into every process it starts.
That is malware-shaped, permanently — DR-0000 and AGENTS.md both carry the
warning — so enterprise endpoint protection will quarantine or kill the
loader unless told otherwise, and there is no defect to fix. Deployment
therefore includes an exclusions step: run `elf-install --exclusions` with
the same root, payload and libdir arguments as the install, and hand the
printed paths to the endpoint-protection console before first use. The
list is derived from the same inputs as the install so it cannot drift
from what actually landed.

## Usage

    elf-install -R /some/root -p payload.list -r 1.0
    elf-install -R /some/root -p payload.list -x   # print exclusions

The payload list is one entry per line, `MODE SRC DEST`, whitespace
separated, `DEST` relative to the root; `#` comments and blank lines are
ignored, and paths in it carry no spaces. Options follow docopt shape and
every setting is also reachable by environment variable
(`ELF_INSTALL_ROOT`, `ELF_INSTALL_PAYLOAD`, `ELF_INSTALL_LDCONFIG`), with
precedence option, environment, default.

## Not verified

That the cache rebuild is right at `root=/`. The test points
`elf-ldconfig` at root-prefixed directories, which at a live root are the
same paths the conf names; no run on a live root has been performed, since
no machine deploys this yet.

That reaping by `find` is fast enough on a full root. On a scratch root it
is instant; a root carrying el8's userland has not been swept.
