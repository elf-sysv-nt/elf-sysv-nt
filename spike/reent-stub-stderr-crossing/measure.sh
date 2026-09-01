#!/usr/bin/env bash
# reent-stub-stderr-crossing 1.0 -- does a host->faced stderr write cross through
# a sysv_abi thunk, and by which route?
#
# spike/reent-stub-libc-crossing measured the stdout route: a sysv_abi puts
# thunk crosses, so the landed rp_puts carries the stub's --version and
# --dry-run output. The stub's diagnostics (say, refuse, usage, the unknown-
# option and no-argument messages) write to stderr, which acceptance/reent/
# RELINK.md defers as wanting "a separate stderr crossing before they reroute".
#
# The faced elfsysv1.dll exports no `stderr` FILE* (this spike confirms that:
# stderr_file_export_present), so fputs/fwrite-to-stderr is not available and
# the crossing must take an fd-2 body. Two exist and are measured:
#
#   sysv_thunk_write_fd2_crosses  -- write(2, s, n), the raw fd-2 body. No
#     FILE*, no va_list crossing the ABI boundary. If it crosses, a stderr twin
#     of rp_puts is a plain sysv_abi write thunk.
#   sysv_thunk_dprintf_fd2_crosses -- dprintf(2, "%s", s), the fd-2 formatted
#     body. Variadic: carries a Microsoft-ABI va_list into the faced runtime's
#     System V vararg reader, the disagreement DR-0066 draws a line at. If it
#     does not cross while write does, the reroute formats host-side (as rp_puts
#     does) and crosses through write.
#
# The findings are verdict words, reproducible. The faced runtime wedges on a
# host pty, so the probe is run detached via cmd with stdin from NUL, as
# spike/reent-stub-libc-crossing does. SKIPs (verdict yes, exit 0) when the
# faced DLL or the WP-26 build tree are absent, both uncommitted build products.
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

emit "script  reent-stub-stderr-crossing 1.0"
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

# The crux the fd-2 routes exist to answer: the faced DLL exports fd bodies but
# no stderr FILE*, so a FILE*-based stderr write has nothing to name.
if x86_64-elfsysvnt-linux-gnu-objdump -p "$dll" 2>/dev/null \
     | grep -Eq '[0-9]+\] stderr$'; then
	emit "stderr_file_export_present=yes"
else
	emit "stderr_file_export_present=no"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-stderr-crossing.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

CF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib"
LIBS="$build/crt0.o -L$build -lcygwin -lkernel32 -lgcc"

if ! gcc $CF -o "$tmp/probe.exe" "$here/stderr-crossing-probe.c" $LIBS 2>"$tmp/build.err"; then
	emit "probe_links=no"
	sed 's/^/    ld: /' "$tmp/build.err" | emit
	emit "verdict=no"; exit 1
fi

run_probe() { # $1 exe -> prints the newline-joined markers and tokens seen
	local base=$(basename "$1")
	cp "$1" "$out/$base"
	( cd "$out" && rm -f "$base.out" \
	  && timeout 40 cmd /c "$base > $base.out 2>&1 < NUL" ) 2>/dev/null
	tr -d '\r' < "$out/$base.out" 2>/dev/null
	rm -f "$out/$base" "$out/$base.out"
}

o=$(run_probe "$tmp/probe.exe")

# Startup must have reached main for either route to mean anything.
if ! printf '%s' "$o" | grep -q '^A:reached-main'; then
	emit "startup_reached_main=no"
	emit "sysv_thunk_write_fd2_crosses=faulted"
	emit "sysv_thunk_dprintf_fd2_crosses=faulted"
	emit "verdict=yes"; exit 0
fi
emit "startup_reached_main=yes"

# Raw fd-2 write: its token reached output and control survived (C after B).
if printf '%s' "$o" | grep -q '^W:write-crossed' \
   && printf '%s' "$o" | grep -q '^C:after-write'; then
	emit "sysv_thunk_write_fd2_crosses=yes"
elif printf '%s' "$o" | grep -q '^C:after-write'; then
	emit "sysv_thunk_write_fd2_crosses=no"
else
	emit "sysv_thunk_write_fd2_crosses=faulted"
fi

# Formatted fd-2 dprintf (variadic): its token reached output and control
# survived (E after D).
if printf '%s' "$o" | grep -q '^P:dprintf-crossed' \
   && printf '%s' "$o" | grep -q '^E:after-dprintf'; then
	emit "sysv_thunk_dprintf_fd2_crosses=yes"
elif printf '%s' "$o" | grep -q '^E:after-dprintf'; then
	emit "sysv_thunk_dprintf_fd2_crosses=no"
else
	emit "sysv_thunk_dprintf_fd2_crosses=faulted"
fi

emit "verdict=yes"
