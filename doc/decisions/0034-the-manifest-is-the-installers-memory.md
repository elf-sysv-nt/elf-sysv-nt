# DR-0034 — the installer's only memory is a manifest under the root

Status: accepted
Date: 2026-08-30
Deciding: taken in implementation of WP-63, within AGENTS.md's stated
idempotency policy
Proposal: none; the policy is AGENTS.md's, this records the mechanism

## What was decided

`elf-install` keeps one record of what it manages: a manifest of relative
paths at `etc/elfsysvnt/manifest` under the root it installed into. Orphan
removal — the half of idempotency that re-copying never buys — is computed
as the old manifest minus the new one, and nothing else is consulted. The
tool holds no state of its own anywhere else: not in the repository, not
in the home directory, not in a registry of installations.

Two consequences were accepted with that. First, the manifest travels with
the root; copy the root and the record of what is managed inside it comes
along, which is the behavior a scratch-root test and a real deployment
both want. Second, the manifest is data found on disk, not something the
tool remembers, so it is treated as hostile on every read: entries that
are absolute, empty, or climb through `.` or `..` are refused with a
warning, because each accepted entry may be handed to `rm` under the root.
A tool that trusted its own manifest would be one stale file away from
deleting outside the tree it owns.

## Why not the alternatives

State beside the tool was rejected because it desynchronizes: an
installation whose root was moved, restored from backup, or duplicated for
a test no longer matches the tool's ledger, and the orphan pass then
deletes the wrong things or nothing. State inside the files themselves — a
marker comment, an owned-by header — was rejected because binaries cannot
carry one and because it turns every read into a parse. The manifest is
the same shape rpm's own database takes for the same reason, reduced to
the one question this tool asks: what did I manage last time?

## Where it binds

Everything the installer places goes through the manifest: payload files,
the seeded configuration, the release marker, the rebuilt cache. A path
not in the manifest is not the installer's to remove, however plausible it
looks. Retiring the installer's whole footprint is therefore a run with an
empty payload, not a special uninstall mode.
