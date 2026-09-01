#!/usr/bin/env bash
# reent-stub-faceload 1.0 -- item 1's face-base half: does a real process of the
# faced runtime reach elfsysv1.dll's base without the 1114 cygheap wedge?
#
# The reent-face-bringup live run halts on face_base_via_runtime=no: the loader's
# --runtime LoadLibraryA of the faced DLL, made from the plain-PE cygload stub,
# wedges (error 1114, heap-at-wrong-address; DR-0060), so AT_BASE carries no face
# base and the veneer thunk null-faults. DR-0060/0066/0067 name the shape that
# clears it -- a real process of the faced runtime: -nostdlib against the WP-26
# crt0.o and -lcygwin, so the faced DLL is the process's own runtime, brought up
# and cygheap-reserved by _dll_crt0 at startup rather than by a later load.
#
# This measures, in that shape (the stub-abi-probe link and bridge), whether the
# faced base is reachable both ways --runtime needs it: as the already-loaded
# module (GetModuleHandleA) and through the literal LoadLibraryA operation --
# and whether the two agree. It reproduces off the committed probe and the two
# build products (the faced DLL, the WP-26 crt0.o), both gitignored.
#
# The faced runtime wedges on a host pty, so each probe is run detached via cmd
# with stdin from NUL, as the sibling reent-stub spikes do.
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

emit "script  reent-stub-faceload 1.0"
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-faceload.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

CF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib"
LIBS="$build/crt0.o -L$build -lcygwin -lkernel32"

build_probe() { # $1 extra cppflags, $2 out
	gcc $CF $1 -o "$2" "$here/faceload-probe.c" $LIBS 2>"$tmp/build.err"
}

run_probe() { # $1 exe -> prints the newline-joined markers seen
	local base=$(basename "$1")
	cp "$1" "$out/$base"
	( cd "$out" && rm -f "$base.out" \
	  && timeout 40 cmd /c "$base > $base.out 2>&1 < NUL" ) 2>/dev/null
	tr -d '\r' < "$out/$base.out" 2>/dev/null
	rm -f "$out/$base" "$out/$base.out"
}

# The real-process host links the same shape the full stub relink takes.
if ! build_probe "-DBRIDGE" "$tmp/bridge.exe"; then
	emit "realproc_host_links=no"
	sed 's/^/    ld: /' "$tmp/build.err" | emit
	emit "verdict=no"; exit 1
fi
emit "realproc_host_links=yes"

# Without the bridge, the crt0 startup crossing still gates the host (the
# control that the shape, not the bridge, is what reaches the faced runtime).
if build_probe "" "$tmp/nobridge.exe"; then
	nob=$(run_probe "$tmp/nobridge.exe")
	if printf '%s' "$nob" | grep -q '^A:reached-main'; then
		emit "startup_gated_without_bridge=no"
	else
		emit "startup_gated_without_bridge=yes"
	fi
fi

# With the bridge: reach main, then take the face-base measurement.
br=$(run_probe "$tmp/bridge.exe")

if printf '%s' "$br" | grep -q '^A:reached-main'; then
	emit "startup_reached_main=yes"
else
	emit "startup_reached_main=no  (real-process host faulted before main)"
	emit "verdict=no"; exit 1
fi

# The faced runtime is the process's own module in this shape.
if printf '%s' "$br" | grep -q '^H:own-runtime-base='; then
	emit "own_runtime_base_present=yes  ($(printf '%s' "$br" | sed -n 's/^H://p' | head -1))"
else
	emit "own_runtime_base_present=no"
fi

# The literal --runtime operation: LoadLibraryA the faced DLL.
if printf '%s' "$br" | grep -q '^L:loadlibrary-base='; then
	emit "faceload_via_loadlibrary=succeeds  ($(printf '%s' "$br" | sed -n 's/^L://p' | head -1))"
elif printf '%s' "$br" | grep -q '^L:loadlibrary-failed-err='; then
	emit "faceload_via_loadlibrary=wedged  ($(printf '%s' "$br" | sed -n 's/^L://p' | head -1))"
else
	emit "faceload_via_loadlibrary=faulted  (no L marker; control did not survive the load)"
fi

# Do the two ways --runtime can reach the base agree?
if printf '%s' "$br" | grep -q '^M:base-matches'; then
	emit "faceload_base_matches_own_runtime=yes"
elif printf '%s' "$br" | grep -q '^M:base-differs'; then
	emit "faceload_base_matches_own_runtime=no"
fi

# Control survived the whole measurement.
if printf '%s' "$br" | grep -q '^Z:done'; then
	emit "host_survives_faceload=yes"
else
	emit "host_survives_faceload=no"
fi

emit "verdict=yes"
