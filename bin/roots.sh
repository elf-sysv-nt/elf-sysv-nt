#!/usr/bin/env bash
#
# The three roots every script in this tree resolves against, in one place.
# Source it; do not run it.
#
#	. "$(dirname "$0")/../bin/roots.sh"
#
# Until now each script wrote /c/-/x-elfsysvnt and /c/-/el8 out longhand, which
# pinned the whole harness to one machine's layout and put that layout in a
# repository heading for publication. The values below are what those literals
# were, so nothing about a run on this host changes: a spike regenerates the
# same transcript, byte for byte, and that is the point of taking the defaults
# rather than inventing new ones.
#
#   ELFSYSVNT_ROOT     this checkout. Derived, never guessed.
#   ELFSYSVNT_PREFIX   the cross toolchain's install prefix; $PREFIX/bin is
#                      what goes on PATH.
#   ELFSYSVNT_EL8      the el8 scratch root. Disposable by design: nothing
#                      under it is backed up, and every input is refetchable
#                      from a pinned hash or rebuildable from something
#                      tracked.
#
# A value already in the environment wins over the default, so a second machine
# exports what it has and runs the harness unmodified. Nothing here fails when
# a root is absent -- that is each caller's judgement, since a missing toolchain
# is fatal to a build and merely a SKIP to a registry.

: "${ELFSYSVNT_ROOT:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
: "${ELFSYSVNT_PREFIX:=/c/-/x-elfsysvnt}"
: "${ELFSYSVNT_EL8:=/c/-/el8}"

export ELFSYSVNT_ROOT ELFSYSVNT_PREFIX ELFSYSVNT_EL8

# Expand the three names, and only those, in a string read from a registry.
# Deliberately not `eval`: a manifest field is data, and a stray backtick in
# one should be a path that does not exist, not a command that runs.
elfsysvnt_expand() {
	local s=$1
	s=${s//\$\{ELFSYSVNT_ROOT\}/$ELFSYSVNT_ROOT}
	s=${s//\$ELFSYSVNT_ROOT/$ELFSYSVNT_ROOT}
	s=${s//\$\{ELFSYSVNT_PREFIX\}/$ELFSYSVNT_PREFIX}
	s=${s//\$ELFSYSVNT_PREFIX/$ELFSYSVNT_PREFIX}
	s=${s//\$\{ELFSYSVNT_EL8\}/$ELFSYSVNT_EL8}
	s=${s//\$ELFSYSVNT_EL8/$ELFSYSVNT_EL8}
	printf '%s' "$s"
}
