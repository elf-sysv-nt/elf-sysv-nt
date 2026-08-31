#!/usr/bin/env bash
# WP-27 milestone 8: the per-thread signal record, reached through the
# carrier's TCB.
#
# Standalone on purpose: the unit is a seam between the carrier and the
# signal package, and every property it owes -- resolution, establishment
# order, inheritance, independence, the fallback -- is visible with
# fabricated TCBs on harness-owned stacks.  No faced DLL is needed, so
# this runs on every merge rather than only where the build products are.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face=$here/..
sig=$face/../signal
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

bin=$(mktemp -d "${TMPDIR:-/tmp}/wp27-sigtls.XXXXXX")
trap 'rm -rf "$bin"' EXIT

gcc -std=gnu11 -O2 -g -Wall -Wextra -mno-red-zone \
    -o "$bin/sigtls" "$here/sigtls.c" "$face/sigtls.c" "$face/carrier.c" \
    "$sig/sigdisp.c" "$sig/sigframe.c" "$sig/sigenter.S" -lpthread \
  || { bad "the sigtls harness does not build"; say "verdict: no"; exit 1; }

out=$bin/sigtls.out
if timeout 60 "$bin/sigtls" > "$out" 2>&1 && grep -q '^verdict=yes$' "$out"
then
	say "ok: the per-thread record resolves through the carrier's TCB"
	grep '^checks=\|^failures=' "$out" | sed 's/^/     /'
else
	bad "the sigtls certification failed (rc $?):"
	sed 's/^/     /' "$out" 2>/dev/null || say "     (no output)"
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
