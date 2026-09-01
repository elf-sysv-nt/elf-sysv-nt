#!/usr/bin/env bash
# reent-stub-realproc-window-occupant 1.0 -- WP-56 reent-tls-bringup, item 1.
#
# spike/reent-stub-realproc-faceload measured that the DR-0028 low-window
# handover is refused for the real-process stub but not the plain-PE one, and
# attributed it to "the cygwin-linked child already holds the low region at
# suspend". This spike measures that attribution directly: it builds both stubs
# and a Win32 probe that spawns each CREATE_SUSPENDED, walks the child with
# VirtualQueryEx before any user code runs, and reports what -- if anything --
# occupies the 0x400000 low window, plus the handover (VirtualAllocEx) verdict
# reproduced against the live child.
#
# What it measures, per stub:
#   *_stub_links        the stub links in its shape.
#   probe_builds        the Win32 occupant probe builds.
#   plain_window_free   the low window is MEM_FREE in the plain-PE child at
#                       suspend, and the handover reserve_in succeeds (control).
#   realproc_window     window_free / occupant class / span in the cygwin-linked
#                       child, and whether the handover is refused -- the
#                       attribution, now measured rather than inferred.
#
# SKIPs (verdict=yes, exit 0) when the WP-26 build tree is absent (crt0.o and
# libcygwin, uncommitted build products the real-process link needs).
set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }
emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
[ -n "$dest" ] && : >"$dest"

main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
build=$main/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin

emit "script  reent-stub-realproc-window-occupant 1.0"
emit ""
emit "host        $(hostname)"
emit "compiler    $(gcc --version 2>/dev/null | head -1)"
emit "binutils    $(ld --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -f "$build/crt0.o" ] || [ ! -f "$build/libcygwin.a" ]; then
emit "skip=no WP-26 build tree (crt0.o + libcygwin); build it first"
emit "verdict=yes"
exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-window-occupant.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

L=$repo/loader
E=$L/exec
RP=$E/realproc
loader_srcs="$E/reserve.c $L/map/elf_map.c $L/map/host_mem.c $L/elf/elf_parse.c \
$L/process/process_image.c $L/reloc/elf_reloc.c $L/reloc/reloc_resolve.S"
rp_srcs="$RP/realproc-str.c $RP/realproc-fmt.c $RP/realproc-file.c $RP/realproc-cross.c"
stub_srcs="$E/stub.c $E/exec_kind.c $E/dyn_exec.c $E/dyn_init.c $E/enter.S $loader_srcs"

# ---- the real-process stub (the shape whose handover is refused) --------
RPCF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib -DELFSYSV_REALPROC"
RPLIBS="$build/crt0.o -L$build -lcygwin -lgcc -lkernel32"
if gcc $RPCF -I"$E" -o "$tmp/stub-realproc.exe" $stub_srcs $rp_srcs $RPLIBS 2>"$tmp/rp.err"; then
emit "realproc_stub_links=yes"
else
emit "realproc_stub_links=no"
sed 's/^/    ld: /' "$tmp/rp.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the plain-PE control stub (the WP-41 shape the handover was for) ----
PLCF="-std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wl,--stack,0x100000"
if gcc $PLCF -I"$E" -o "$tmp/stub-plain.exe" $stub_srcs 2>"$tmp/pl.err"; then
emit "plain_stub_links=yes"
else
emit "plain_stub_links=no"
sed 's/^/    ld: /' "$tmp/pl.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the occupant probe --------------------------------------------------
if gcc -std=gnu11 -O2 -Wall -o "$tmp/occupant-probe.exe" \
	"$here/occupant-probe.c" -lkernel32 2>"$tmp/probe.err"; then
emit "probe_builds=yes"
else
emit "probe_builds=no"
sed 's/^/    cc: /' "$tmp/probe.err" | emit
emit "verdict=no"; exit 1
fi

emit ""

# probe drives CreateProcessA, which knows no mounts -- pass a Windows path.
win() { cygpath -w "$1"; }

run_probe() {
	"$tmp/occupant-probe.exe" "$(win "$1")" 2>"$tmp/probe.run.err"
	probe_st=$?
	[ -s "$tmp/probe.run.err" ] && sed 's/^/    probe: /' "$tmp/probe.run.err" | emit
}

# ---- control: the plain-PE child ----------------------------------------
emit "# plain-PE child (control: the shape the handover was written for)"
run_probe "$tmp/stub-plain.exe" | while IFS= read -r line; do emit "  plain_$line"; done
plain_out=$("$tmp/occupant-probe.exe" "$(win "$tmp/stub-plain.exe")" 2>/dev/null)
emit ""

# ---- the measurement: the real-process child ----------------------------
emit "# real-process child (cygwin-linked: -nostdlib crt0.o -lcygwin)"
rp_out=$("$tmp/occupant-probe.exe" "$(win "$tmp/stub-realproc.exe")" 2>/dev/null)
printf '%s\n' "$rp_out" | while IFS= read -r line; do
	case "$line" in
	"    "*) emit "$line" ;;
	*) emit "  realproc_$line" ;;
	esac
done
emit ""

# ---- the differential verdict -------------------------------------------
pfree=$(printf '%s\n' "$plain_out" | sed -n 's/^window_free=//p')
pres=$(printf '%s\n' "$plain_out"  | sed -n 's/^reserve_in=\([a-z]*\).*/\1/p')
rfree=$(printf '%s\n' "$rp_out"    | sed -n 's/^window_free=//p')
rres=$(printf '%s\n' "$rp_out"     | sed -n 's/^reserve_in=\([a-z]*\).*/\1/p')
rocc=$(printf '%s\n' "$rp_out"     | sed -n 's/^occupant=//p')

emit "handover_control=plain window_free=$pfree reserve_in=$pres"
emit "handover_measured=realproc window_free=$rfree reserve_in=$rres occupant=${rocc:-none}"

if [ "$pres" = ok ] && [ "$rres" = refused ]; then
emit "verdict=yes  (the handover is refused only in the real-process shape; occupant=${rocc:-unknown} covers the window at suspend, so the attribution holds -- the cygwin-linked child holds the low region before any user code runs)"
elif [ "$pres" = ok ] && [ "$rres" = ok ]; then
emit "verdict=surprise  (the handover now succeeds in both shapes; the faceload spike's obstacle did not reproduce -- rerun clean before believing it)"
else
emit "verdict=partial  (control handover did not succeed; the harness or host, not the shape difference, is in question)"
fi
