#!/usr/bin/env bash
# reent-stub-path 1.0 -- how does the real-process stub open a POSIX image path?
#
# The stub's image read (realproc-file.c) is CreateFileA, which resolves a
# Windows-form path; the loader is handed a Cygwin POSIX path. SLURP-REROUTE.md
# left open how that path is converted host-side without a faced-libc call. This
# measures the two candidates on the loader's own real input (/bin/echo.exe):
# the parent's host cygwin1.dll conversion, and the stub's only host-safe
# option, Win32 GetFullPathNameA.
#
# It needs only the native (host Cygwin) toolchain -- no faced runtime, no build
# products -- so it always runs. It SKIPs (verdict yes) only if the probe input
# /bin/echo.exe is absent.
set -u

here=$(cd "$(dirname "$0")" && pwd)
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }
emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
[ -n "$dest" ] && : >"$dest"

emit "script  reent-stub-path 1.0"
emit ""
emit "host        $(hostname)"
emit "compiler    $(gcc --version 2>/dev/null | head -1)"
emit "binutils    $(ld --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

input=/bin/echo.exe
if [ ! -f "$input" ]; then
emit "skip=no $input to probe; the loader's own image input is absent"
emit "verdict=yes"
exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-path.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

if ! gcc -std=gnu11 -O1 -Wall -Wextra -o "$tmp/probe.exe" \
     "$here/probe-path.c" 2>"$tmp/build.err"; then
emit "probe_builds=no"
sed 's/^/    cc: /' "$tmp/build.err" | emit
emit "verdict=no"; exit 1
fi
emit "probe_builds=yes"

out=$("$tmp/probe.exe" "$input" 2>&1)
posix=$(printf '%s\n' "$out" | sed -n 's/^I:posix=//p' | head -1)
emit "probe_input=$posix"

# The parent's route: host cygwin1.dll conversion, then open.
pconv=$(printf '%s\n' "$out" | sed -n 's/^P:conv-win=//p' | head -1)
popens=$(printf '%s\n' "$out" | sed -n 's/^P:conv-opens=//p' | head -1)
if printf '%s\n' "$out" | grep -q '^P:conv-failed'; then
emit "parent_cygwin_conv_opens=no  (cygwin_conv_path failed)"
elif [ -n "$pconv" ]; then
emit "parent_cygwin_conv_opens=$popens  (-> $pconv)"
fi

# The stub's route: Win32 GetFullPathNameA, then open.
sfull=$(printf '%s\n' "$out" | sed -n 's/^S:full-win=//p' | head -1)
sopens=$(printf '%s\n' "$out" | sed -n 's/^S:full-opens=//p' | head -1)
if printf '%s\n' "$out" | grep -q '^S:full-failed'; then
emit "stub_getfullpath_opens=no  (GetFullPathNameA failed)"
elif [ -n "$sfull" ]; then
emit "stub_getfullpath_opens=$sopens  (-> $sfull)"
fi

if printf '%s\n' "$out" | grep -q '^Z:done'; then
emit "probe_ran_to_end=yes"
else
emit "probe_ran_to_end=no"
emit "verdict=no"; exit 1
fi

# The finding: which route makes the POSIX path openable host-side.
if [ "$popens" = "yes" ] && [ "$sopens" != "yes" ]; then
emit "route=parent-passes-windows-path"
elif [ "$popens" = "yes" ] && [ "$sopens" = "yes" ]; then
emit "route=either-opens"
elif [ "$sopens" = "yes" ]; then
emit "route=stub-getfullpath-suffices"
else
emit "route=neither-opens"
fi

emit "verdict=yes"
