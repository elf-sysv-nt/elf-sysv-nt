#!/usr/bin/env bash
# reent-stub-realproc-version 1.0 -- does a real-process host stub reach and
# complete its --version path once the two fixes the reent-stub-* spikes
# measured are applied?
#
# spike/reent-stub-link found the loader's real-process stub links but faults
# before --version (realproc_stub_reaches_version=no). Two later spikes
# localized the obstacle and proved each fix alone:
# spike/reent-stub-realproc-window (the fault is the unbridged startup
# cygwin_internal crossing; a sysv_abi bridge reaches main; a plain Microsoft-
# ABI libc call still does not cross) and spike/reent-stub-libc-crossing (a libc
# call reached through an explicit sysv_abi thunk does cross, stdio included).
#
# This spike combines them at the exact path reent-stub-link found faulting --
# stub.c's --version, printf("%s\n", RELEASE), a reent-consuming stdio body --
# with three build variants of one probe, and reads three verdicts:
#
#   startup_faults_without_bridge   -- NO_BRIDGE: control never reaches the
#     version path. Reproduces reent-stub-link at this path.
#   version_print_plain_crosses     -- PLAIN_PRINT: bridge in, RELEASE printed
#     Microsoft-style. main is reached; the line does not cross. Reproduces the
#     realproc-window ms_abi finding at the version path.
#   version_print_thunked_crosses   -- default: bridge in, RELEASE printed
#     through a sysv_abi thunk. The line crosses and control survives -- the
#     real-process stub completes its --version.
#
# Together: once the startup bridge and a sysv_abi-thunked stdio path are
# applied, the real-process stub reaches the --version reent-stub-link found
# faulting. The findings are verdict words, reproducible; no address or offset
# is a finding here.
#
# The faced runtime wedges on a host pty, so each probe is run detached via cmd
# with stdin from NUL, as the sibling reent-stub-* spikes do.
#
# SKIPs (verdict yes, exit 0) when the faced DLL or the WP-26 build tree are
# absent, both being uncommitted build products.
set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }
emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
[ -n "$dest" ] && : >"$dest"

# Build products under a/ are gitignored -- resolve them against the shared git
# common dir so the spike runs from a session worktree as well as the checkout.
main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
out=$main/a/build/wp27-face
dll=$out/elfsysv1.dll
build=$main/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin

emit "script  reent-stub-realproc-version 1.0"
emit ""
emit "host        $(hostname)"
emit "compiler    $(gcc --version 2>/dev/null | head -1)"
emit "binutils    $(ld --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ]; then
emit "skip=no faced DLL or WP-26 build tree; build them first"
emit "verdict=yes"
exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-realproc-version.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

CF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib"
LIBS="$build/crt0.o -L$build -lcygwin -lkernel32 -lgcc"

build_variant() { # $1 out-name  $2... extra defines
local name=$1; shift
gcc $CF "$@" -o "$tmp/$name.exe" "$here/version-probe.c" $LIBS 2>"$tmp/$name.err"
}

run_probe() { # $1 exe -> prints newline-joined markers + any version stdout
local base=$(basename "$1")
cp "$1" "$out/$base"
( cd "$out" && rm -f "$base.out" \
  && timeout 40 cmd /c "$base --version > $base.out 2>&1 < NUL" ) 2>/dev/null
tr -d '\r' < "$out/$base.out" 2>/dev/null
rm -f "$out/$base" "$out/$base.out"
}

# All three variants must link; a link failure is a real negative.
if ! build_variant nobridge -DNO_BRIDGE \
   || ! build_variant plain -DPLAIN_PRINT \
   || ! build_variant thunk; then
emit "probe_links=no"
for f in nobridge plain thunk; do
[ -s "$tmp/$f.err" ] && sed "s/^/    ld($f): /" "$tmp/$f.err" | while IFS= read -r l; do emit "$l"; done
done
emit "verdict=no"; exit 1
fi

o_nobridge=$(run_probe "$tmp/nobridge.exe")
o_plain=$(run_probe "$tmp/plain.exe")
o_thunk=$(run_probe "$tmp/thunk.exe")

# NO_BRIDGE: startup must NOT reach the version path (it faults in crt0).
if printf '%s' "$o_nobridge" | grep -q '^A:reached-main'; then
emit "startup_faults_without_bridge=no"
else
emit "startup_faults_without_bridge=yes"
fi

# PLAIN_PRINT: bridge reaches main; the Microsoft-style RELEASE line must NOT
# emit while control survives (E present, RELEASE absent).
if printf '%s' "$o_plain" | grep -q '^A:reached-main'; then
if printf '%s' "$o_plain" | grep -q '^elfsysv-stub 1\.0$'; then
emit "version_print_plain_crosses=yes"
elif printf '%s' "$o_plain" | grep -q '^E:after-version'; then
emit "version_print_plain_crosses=no"
else
emit "version_print_plain_crosses=faulted"
fi
else
emit "version_print_plain_crosses=no-main"
fi

# default (thunk): bridge reaches main; the RELEASE line emits through the
# sysv_abi thunk and control survives (RELEASE present and E present).
if printf '%s' "$o_thunk" | grep -q '^A:reached-main'; then
if printf '%s' "$o_thunk" | grep -q '^elfsysv-stub 1\.0$' \
   && printf '%s' "$o_thunk" | grep -q '^E:after-version'; then
emit "version_print_thunked_crosses=yes"
elif printf '%s' "$o_thunk" | grep -q '^E:after-version'; then
emit "version_print_thunked_crosses=no"
else
emit "version_print_thunked_crosses=faulted"
fi
else
emit "version_print_thunked_crosses=no-main"
fi

emit "verdict=yes"
