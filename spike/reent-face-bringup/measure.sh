#!/bin/bash
# reent-face-bringup (WIP skeleton) -- item 3 of the reent-tls-bringup rung.
#
# Question: does a reent-consuming libc body, reached through the WP-53
# libc.so.6 veneer resolving into a built elfsysv1.dll face, set the caller's
# reent (strtol overflow -> LONG_MAX, errno ERANGE) across the veneer->face
# resolution? See README.md.
#
# This skeleton stages the measurement: it checks for the three scratch
# artifacts item 3 rests on and reports the first one absent rather than
# asserting a finding. It is intentionally not in test/spike-regen.tsv until the
# three build and the crossing runs -- an unrun registered spike is INCOMPLETE,
# not a pass (see the reproducible-spike contract).
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=/c/-/repo/elf-sysv-nt

echo "script  reent-face-bringup 0.1-wip"

wp26=$repo/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin/new-cygwin1.dll
face=$repo/a/build/wp27-face
veneer=$repo/veneer/libc

need() { # label path
if [ -e "$2" ]; then echo "prereq $1 present"; return 0; fi
echo "prereq $1 ABSENT ($2)"; return 1
}

missing=0
need wp26-winsup-dll "$wp26"       || missing=1
need wp27-face-tree  "$face"       || missing=1
need wp53-veneer-src "$veneer/build-libc" || missing=1

if [ "$missing" != 0 ]; then
echo "verdict=staged  (a prerequisite artifact is absent; build it, then this"
echo "                skeleton grows the veneer->face crossing measurement)"
exit 0
fi

echo "verdict=staged  (prerequisites present; crossing measurement not yet written)"
exit 0
