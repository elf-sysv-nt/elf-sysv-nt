#!/usr/bin/env bash
#
# WP-56 certification for the exec-kind classifier: build exec_kind.c and its
# unit over the fixture table, warnings as errors, and run it. The undefined-
# behaviour sanitizer is used when the toolchain can link it and skipped with
# a note when it cannot -- the primary root's Cygwin gcc ships no libubsan --
# so the check runs on either root. The classifier is a pure decision, so the
# unit is the whole bar; the dynamic driver that consumes the verdict carries
# its own end-to-end certification when it lands.
#
# Usage:
#   exec-kind.sh [-q]
#
# Exit: 0 passed, 1 a build or check failed.

set -eu

here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..
quiet=0
[ "${1:-}" = "-q" ] && quiet=1

cc=${CC:-gcc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

say() { [ "$quiet" -eq 1 ] || printf '%s\n' "$*"; }

flags="-std=c11 -Wall -Wextra -Werror -O1 -g"

# Use UBSan only if this toolchain can actually link its runtime.
san="-fsanitize=undefined -fno-sanitize-recover=all"
printf 'int main(void){return 0;}\n' > "$work/probe.c"
if "$cc" $san "$work/probe.c" -o "$work/probe" 2>/dev/null; then
flags="$flags $san"
say "exec-kind: ubsan on"
else
say "exec-kind: ubsan unavailable on this toolchain, skipped"
fi

say "exec-kind: build"
# shellcheck disable=SC2086
"$cc" $flags \
"$exec_dir/exec_kind.c" \
"$here/exec_kind_unit.c" \
-o "$work/exec_kind_unit"

say "exec-kind: run"
"$work/exec_kind_unit"

say "exec-kind: ok"
