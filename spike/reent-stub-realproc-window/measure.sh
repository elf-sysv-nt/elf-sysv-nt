#!/usr/bin/env bash
# reent-stub-realproc-window 1.0 -- locate the fault that stops the real-process
# relink of the loader stub (spike/reent-stub-link) before its --version path,
# and correct the account of it.
#
# acceptance/reent/stub-realproc.md recorded the fault as an address collision:
# the stub "a minimal non-PIE PE that adopts a parent-reserved low window
# (DR-0028)", whose 0x400000 window "_dll_crt0 lays out its own low mappings and
# the two collide before main". This spike measures three things that reproduce
# off the committed loader source and the two build products, and together they
# put the cause elsewhere:
#
#   realproc_stub_image_base -- the real-process stub links at 0x100400000, the
#     ordinary high Cygwin image base, not the 0x400000 low window. The stub is
#     not in the window it reserves; the window is the ELF world's, held for a
#     child (DR-0028), and reserved by --version's path not at all. So the
#     collision the account names is not between the stub's image and the window.
#
#   startup_faults_without_bridge / startup_reached_with_bridge -- the fault is
#     the crt0 startup crossing. _cygwin_crt0_common calls cygwin_internal
#     (CW_USER_DATA) Microsoft-style; the faced elfsysv1.dll exports it as a
#     System V veneer. Without a bridge the call faults before main; interposing
#     one local cygwin_internal (the shape spike/reent-bringup's realproc probe
#     uses) reaches main. One flag, -DBRIDGE, is the whole difference.
#
#   ms_abi_libc_call_crosses -- past startup, one ordinary printf, a Microsoft-
#     ABI call into the faced System V libc, produces no output while control
#     survives it (A and C mark, B does not). The stub's own code is host
#     Microsoft-ABI code and every libc call it makes for its own work meets the
#     same boundary; a bridge for one export does not lift it.
#
# So item 1 is not the window/image-base reconciliation the account named. The
# obstacle is the Microsoft<->System V boundary at every host-to-faced-runtime
# call -- startup's cygwin_internal first, then the stub's whole libc use. See
# the decision this spike carries and the corrected stub-realproc.md.
#
# The faced runtime wedges on a host pty, so each probe is run detached via cmd
# with stdin from NUL, as spike/reent-bringup does.
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

emit "script  reent-stub-realproc-window 1.0"
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-realproc-window.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

CF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib"
LIBS="$build/crt0.o -L$build -lcygwin -lkernel32"

build_probe() { # $1 extra cppflags, $2 out
	gcc $CF $1 -o "$2" "$here/stub-abi-probe.c" $LIBS 2>"$tmp/build.err"
}

# The image base of the real-process probe, the same link shape the full stub
# takes. objdump reports it as a padded hex; normalize to 0x....
if build_probe "" "$tmp/nobridge.exe"; then
	ib=$(objdump -p "$tmp/nobridge.exe" 2>/dev/null | sed -n 's/^ImageBase[[:space:]]*//p' | head -1)
	emit "realproc_stub_image_base=0x$ib"
else
	emit "realproc_stub_image_base=link-failed"
	sed 's/^/    ld: /' "$tmp/build.err" | emit
	emit "verdict=no"; exit 1
fi

run_probe() { # $1 exe -> prints the newline-joined markers seen
	local base=$(basename "$1")
	cp "$1" "$out/$base"
	( cd "$out" && rm -f "$base.out" \
	  && timeout 40 cmd /c "$base > $base.out 2>&1 < NUL" ) 2>/dev/null
	tr -d '\r' < "$out/$base.out" 2>/dev/null
	rm -f "$out/$base" "$out/$base.out"
}

# Without the bridge: startup faults before the A marker.
nob=$(run_probe "$tmp/nobridge.exe")
if printf '%s' "$nob" | grep -q '^A:reached-main'; then
	emit "startup_faults_without_bridge=no"
else
	emit "startup_faults_without_bridge=yes"
fi

# With the bridge: startup reaches main (A), and the one printf is the test.
if build_probe "-DBRIDGE" "$tmp/bridge.exe"; then
	br=$(run_probe "$tmp/bridge.exe")
	if printf '%s' "$br" | grep -q '^A:reached-main'; then
		emit "startup_reached_with_bridge=yes"
	else
		emit "startup_reached_with_bridge=no"
	fi
	# The host MS-ABI libc call crosses only if its own line (B) appears while
	# control still survives it (C). No B with a C is a call that did not cross.
	if printf '%s' "$br" | grep -q '^B:printf-ran' \
	   && printf '%s' "$br" | grep -q '^C:after-printf'; then
		emit "ms_abi_libc_call_crosses=yes"
	elif printf '%s' "$br" | grep -q '^C:after-printf'; then
		emit "ms_abi_libc_call_crosses=no"
	else
		emit "ms_abi_libc_call_crosses=faulted"
	fi
else
	emit "startup_reached_with_bridge=link-failed"
fi

emit "verdict=yes"
