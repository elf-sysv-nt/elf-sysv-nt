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
# Two more describe where checkouts sit, for the session tools:
#
#   ELFSYSVNT_MAIN     the shared checkout, the one that owns .git and the
#                      integration lock. Equal to ROOT in the main checkout;
#                      asked of git from a session worktree, wherever that
#                      worktree lives. Never derived from the path: the old
#                      ${here%%/a/wt/*} returned the worktree's own path when
#                      the separator was absent, and every caller then took
#                      the session worktree for the shared one.
#   ELFSYSVNT_WT_ROOT  where session worktrees are made, outside every
#                      checkout: <parent of MAIN>/.worktrees/<name of MAIN>.
#
# A value already in the environment wins over the default, so a second machine
# exports what it has and runs the harness unmodified. Nothing here fails when
# a root is absent -- that is each caller's judgement, since a missing toolchain
# is fatal to a build and merely a SKIP to a registry.

: "${ELFSYSVNT_ROOT:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
: "${ELFSYSVNT_PREFIX:=/c/-/x-elfsysvnt}"
: "${ELFSYSVNT_EL8:=/c/-/el8}"

# --git-common-dir prints a relative .git from the main checkout's top level,
# hence the cd into it rather than a string join.
if [ -z "${ELFSYSVNT_MAIN:-}" ]; then
	ELFSYSVNT_MAIN=$(cd "$ELFSYSVNT_ROOT" \
		&& cd "$(git rev-parse --git-common-dir 2>/dev/null || echo .git)" 2>/dev/null \
		&& cd .. && pwd) || ELFSYSVNT_MAIN=$ELFSYSVNT_ROOT
	[ -n "$ELFSYSVNT_MAIN" ] || ELFSYSVNT_MAIN=$ELFSYSVNT_ROOT
fi
: "${ELFSYSVNT_WT_ROOT:=$(dirname "$ELFSYSVNT_MAIN")/.worktrees/$(basename "$ELFSYSVNT_MAIN")}"

export ELFSYSVNT_ROOT ELFSYSVNT_PREFIX ELFSYSVNT_EL8 ELFSYSVNT_MAIN ELFSYSVNT_WT_ROOT

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
