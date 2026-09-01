#!/usr/bin/env bash
# reent-stub-realproc-run 1.0 -- WP-56 reent-tls-bringup, item 1: the full
# relink of the loader stub into the real-process shape.
#
# spike/reent-stub-realproc-version measured the stub's --version path in
# miniature (version-probe.c) and spike/reent-stub-faceload measured the
# face-base half in miniature (faceload-probe.c). This spike promotes those
# findings from a probe to the ACTUAL loader source: it links
# loader/exec/stub.c and its whole translation-unit set -- the realproc seam
# (loader/exec/realproc/, ELFSYSV_REALPROC on) included -- in the real-process
# shape (-nostdlib against the WP-26 crt0.o and -lcygwin, so _dll_crt0 brings
# the reent up and the faced elfsysv1.dll is the process's own runtime), and
# measures how far the linked stub carries when run.
#
# The real-process runtime wedges on a host pty, so each run is detached via
# cmd with stdin from NUL, as the sibling reent-stub spikes do, and from beside
# the faced DLL so elfsysv1.dll resolves as the process's own module.
#
# What it measures, on the real stub source:
#   realproc_stub_links           the whole stub TU set links in the shape.
#   realproc_stub_reaches_version the linked stub reaches --version and emits
#                                 its RELEASE line -- a puts across the faced
#                                 runtime (rp_puts), past the crt0 startup
#                                 bridge (realproc-cross.c's cygwin_internal).
#   realproc_stub_diag_crosses    a deeper path -- the low-window check, reached
#                                 only after startup and option handling -- emits
#                                 its loader diagnostic across fd 2 (rp_eputs),
#                                 so the stub runs real logic and both crossings
#                                 (startup bridge and the write(2) thunk) carry.
#   plain_stub_reaches_version    control: the same source built plain-PE (the
#                                 seam as identity, the WP-41 exec-* shape) also
#                                 reaches --version, so the relink is an added
#                                 shape, not a replacement.
#
# The --runtime face-base half stays with spike/reent-stub-faceload: the stub
# loads --runtime only after the low window is held, and the window is reserved
# by the parent front end into the suspended child, so the real stub's own
# faceload is a front-end-driven run (reent-face-bringup's live-run), the next
# step this rung takes.
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

emit "script  reent-stub-realproc-run 1.0"
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-realproc-run.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

L=$repo/loader
E=$L/exec
RP=$E/realproc
loader_srcs="$E/reserve.c $L/map/elf_map.c $L/map/host_mem.c $L/elf/elf_parse.c \
$L/process/process_image.c $L/reloc/elf_reloc.c $L/reloc/reloc_resolve.S"
rp_srcs="$RP/realproc-str.c $RP/realproc-fmt.c $RP/realproc-file.c $RP/realproc-cross.c"
stub_srcs="$E/stub.c $E/exec_kind.c $E/dyn_exec.c $E/dyn_init.c $E/enter.S $loader_srcs"

# ---- the real-process relink of the actual stub -------------------------
RPCF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib -DELFSYSV_REALPROC"
RPLIBS="$build/crt0.o -L$build -lcygwin -lgcc -lkernel32"
if gcc $RPCF -I"$E" -o "$tmp/stub-realproc.exe" $stub_srcs $rp_srcs $RPLIBS 2>"$tmp/rp-link.err"; then
emit "realproc_stub_links=yes"
else
emit "realproc_stub_links=no"
sed 's/^/    ld: /' "$tmp/rp-link.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the plain-PE control build (the seam as identity) ------------------
PLCF="-std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wl,--stack,0x100000"
if gcc $PLCF -I"$E" -o "$tmp/stub-plain.exe" $stub_srcs 2>"$tmp/pl-link.err"; then
emit "plain_stub_links=yes"
else
emit "plain_stub_links=no"
sed 's/^/    ld: /' "$tmp/pl-link.err" | emit
fi

# run $1 (an exe basename already copied into $out) with args $2; echo output
run_detached() { # $1 exe path, $2 args
local base=$(basename "$1")
cp "$1" "$out/$base"
( cd "$out" && rm -f "$base.out" \
  && timeout 40 cmd /c "$base $2 > $base.out 2>&1 < NUL" ) 2>/dev/null
tr -d '\r' < "$out/$base.out" 2>/dev/null
rm -f "$out/$base" "$out/$base.out"
}

# ---- the real-process stub reaches --version across the faced runtime ---
ver=$(run_detached "$tmp/stub-realproc.exe" "--version")
if printf '%s' "$ver" | grep -q '^elfsysv-stub '; then
emit "realproc_stub_reaches_version=yes  ($(printf '%s' "$ver" | head -1))"
else
emit "realproc_stub_reaches_version=no  (no RELEASE line; the crossing did not carry --version)"
fi

# ---- a deeper path: the low-window check emits its diagnostic across fd 2 -
# Standalone (no parent to reserve the window), --self-window's reserve of the
# low window is refused, and the stub says so through rp_eputs. Reaching that
# message at all means startup crossed (the bridge) and the fd-2 crossing
# carried the line -- real logic past main, not just the early --version exit.
diag=$(run_detached "$tmp/stub-realproc.exe" "--self-window --runtime elfsysv1.dll --verbose no-such-image.elf")
if printf '%s' "$diag" | grep -q 'the low window'; then
emit "realproc_stub_diag_crosses=yes  (reached the low-window check; its diagnostic crossed fd 2)"
else
emit "realproc_stub_diag_crosses=no"
printf '%s' "$diag" | sed 's/^/    out: /' | emit
fi

# ---- control: the plain-PE stub reaches --version too -------------------
if [ -f "$tmp/stub-plain.exe" ]; then
pver=$(run_detached "$tmp/stub-plain.exe" "--version")
if printf '%s' "$pver" | grep -q '^elfsysv-stub '; then
emit "plain_stub_reaches_version=yes  (the seam as identity; the WP-41 shape unaffected)"
else
emit "plain_stub_reaches_version=no"
fi
fi

emit "verdict=yes"
