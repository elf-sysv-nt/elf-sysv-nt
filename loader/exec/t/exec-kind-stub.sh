#!/usr/bin/env bash
#
# WP-56: the stub classifies before it enters.
#
# WP-41's stub entered every parsed image at e_entry, which is right for a
# static executable and wrong for a dynamic one -- a dynamic image's _start
# runs before its GOT is relocated, so entered that way it faults on its
# first library call. exec_kind_of() (WP-56) is the decision that tells the
# two apart, and this certifies that the stub now consults it: a static
# image keeps WP-41's direct-entry path, a dynamic image is recognized as
# owed the crossing rather than entered raw, and an image this route does
# not run is refused. DR-0058 fixes where the branch sits.
#
# The check reads the stub's dry-run report (-n), which does everything but
# the entry and prints stub_exec_kind, over three cross-built specimens whose
# kinds are known: a static no-interp ET_EXEC, a dynamic interp-bearing image,
# and a bare shared object the route does not run.
#
# Usage:  exec-kind-stub.sh [-k] [-q]
# Exit:   0 all passed, 1 a build or check failed, 2 usage.
set -u

# (implementation follows)
