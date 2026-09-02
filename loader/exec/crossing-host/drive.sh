#!/usr/bin/env bash
# drive.sh IMAGE [ARGS...] -- run an ELF image through the faced-runtime crossing
# host (WP-56, DR-0071). Characterization driver: it reports how far the
# real-process crossing carries the actual image, so the run's halt is measured,
# not inferred. The host is run from beside elfsysv1.dll (own-module resolution),
# detached through cmd with stdin from NUL (the faced runtime wedges on a pty),
# and given the image's Windows path (the host's host-safe slurp needs it).
set -u
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
face=$main/a/build/wp27-face
host=$face/elfsysv-crossing-host.exe
[ -f "$host" ] || { echo "no host; run build-host.sh first" >&2; exit 2; }

img=$1; shift
imgwin=$(cygpath -w "$img")
args="--self-window --runtime elfsysv1.dll --verbose $* $imgwin"
( cd "$face" && rm -f drive.out \
  && timeout 40 cmd /c "elfsysv-crossing-host.exe $args > drive.out 2>&1 < NUL" ) 2>/dev/null
rc=$?
tr -d '\r' < "$face/drive.out" 2>/dev/null
echo "drive_rc=$rc"
rm -f "$face/drive.out"
