#!/usr/bin/env bash
# reent-stub-realproc-window-reconcile 1.0 -- WP-56 reent-tls-bringup, item 1.
#
# spike/reent-stub-realproc-window-occupant measured that the raw whole-window
# DR-0028 handover is refused (err=487) against a real cygwin-linked child.
# DR-0068/DR-0069 answered with a reconciling reservation in
# elf_window_reserve_in (loader/exec/reserve.c), so far unit-certified only in
# process (loader/exec/t/unit.c). This spike drives that real function against a
# real cygwin-linked child -- item 1's "reserve ... through a real cygwin-linked
# child" verb -- and measures that the reconcile clears the refusal.
#
# What it measures, per stub, from reconcile-probe (linked with reserve.c):
#   *_stub_links      the stub links in its shape.
#   probe_builds      the probe (probe + reserve.c) builds.
#   raw_whole_window  the naive whole-window VirtualAllocEx verdict (occupant
#                     result reproduced): ok for plain-PE, refused for cygwin.
#   reserve_in        elf_window_reserve_in's verdict -- win_ok when reserved.
#   window_covered    the window fully reserved, no free hole -- the product.
#
# SKIPs (verdict=yes, exit 0) when the WP-26 build tree is absent (crt0.o and
# libcygwin.a, uncommitted build products the real-process link needs).
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

emit "script  reent-stub-realproc-window-reconcile 1.0"
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-window-reconcile.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

L=$repo/loader
E=$L/exec
RP=$E/realproc
loader_srcs="$E/reserve.c $L/map/elf_map.c $L/map/host_mem.c $L/elf/elf_parse.c \
$L/process/process_image.c $L/reloc/elf_reloc.c $L/reloc/reloc_resolve.S"
rp_srcs="$RP/realproc-str.c $RP/realproc-fmt.c $RP/realproc-file.c $RP/realproc-cross.c"
stub_srcs="$E/stub.c $E/exec_kind.c $E/dyn_exec.c $E/dyn_init.c $E/enter.S $loader_srcs"

# ---- the real-process stub (the cygwin-linked shape) --------------------
RPCF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib -DELFSYSV_REALPROC"
RPLIBS="$build/crt0.o -L$build -lcygwin -lgcc -lkernel32"
if gcc $RPCF -I"$E" -o "$tmp/stub-realproc.exe" $stub_srcs $rp_srcs $RPLIBS 2>"$tmp/rp.err"; then
emit "realproc_stub_links=yes"
else
emit "realproc_stub_links=no"
sed 's/^/    ld: /' "$tmp/rp.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the plain-PE control stub (the WP-41 shape) ------------------------
PLCF="-std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wl,--stack,0x100000"
if gcc $PLCF -I"$E" -o "$tmp/stub-plain.exe" $stub_srcs 2>"$tmp/pl.err"; then
emit "plain_stub_links=yes"
else
emit "plain_stub_links=no"
sed 's/^/    ld: /' "$tmp/pl.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the probe, linked with the real reserve.c under measurement --------
if gcc -std=gnu11 -O2 -Wall -I"$E" -o "$tmp/reconcile-probe.exe" \
	"$here/reconcile-probe.c" "$E/reserve.c" -lkernel32 2>"$tmp/probe.err"; then
emit "probe_builds=yes"
else
emit "probe_builds=no"
sed 's/^/    cc: /' "$tmp/probe.err" | emit
emit "verdict=no"; exit 1
fi

emit ""

win() { cygpath -w "$1"; }

# ---- control: the plain-PE child (fast path) ----------------------------
emit "# plain-PE child (control: whole-window fast path)"
plain_out=$("$tmp/reconcile-probe.exe" "$(win "$tmp/stub-plain.exe")" 2>"$tmp/pl.run.err")
[ -s "$tmp/pl.run.err" ] && sed 's/^/    probe: /' "$tmp/pl.run.err" | emit
printf '%s\n' "$plain_out" | while IFS= read -r line; do
	case "$line" in "    "*) emit "$line" ;; *) emit "  plain_$line" ;; esac
done
emit ""

# ---- the measurement: the real-process child (reconcile path) -----------
emit "# real-process child (cygwin-linked: -nostdlib crt0.o -lcygwin)"
rp_out=$("$tmp/reconcile-probe.exe" "$(win "$tmp/stub-realproc.exe")" 2>"$tmp/rp.run.err")
[ -s "$tmp/rp.run.err" ] && sed 's/^/    probe: /' "$tmp/rp.run.err" | emit
printf '%s\n' "$rp_out" | while IFS= read -r line; do
	case "$line" in "    "*) emit "$line" ;; *) emit "  realproc_$line" ;; esac
done
emit ""

# ---- the differential verdict -------------------------------------------
praw=$(printf '%s\n' "$plain_out" | sed -n 's/^raw_whole_window=//p')
pres=$(printf '%s\n' "$plain_out" | sed -n 's/^reserve_in=//p')
pcov=$(printf '%s\n' "$plain_out" | sed -n 's/^window_covered=//p')
rraw=$(printf '%s\n' "$rp_out"    | sed -n 's/^raw_whole_window=//p')
rres=$(printf '%s\n' "$rp_out"    | sed -n 's/^reserve_in=//p')
rcov=$(printf '%s\n' "$rp_out"    | sed -n 's/^window_covered=//p')
rocc=$(printf '%s\n' "$rp_out"    | sed -n 's/^low_window_occupant=//p')

emit "control_plain     raw_whole_window=$praw reserve_in=$pres window_covered=$pcov"
emit "measured_realproc raw_whole_window=$rraw reserve_in=$rres window_covered=$rcov low_window_occupant=$rocc"

if [ "$pres" != win_ok ] || [ "$pcov" != yes ]; then
emit "verdict=harness  (the plain-PE control did not reserve via the fast path: reserve_in=$pres window_covered=$pcov -- the harness or host, not the shape, is in question)"
exit 1
elif [ "$rraw" = ok ]; then
emit "verdict=surprise  (the raw whole-window handover now succeeds against the cygwin-linked child; the occupant obstacle did not reproduce -- rerun clean before believing it)"
exit 1
elif [ "$rres" = win_ok ] && [ "$rcov" = yes ]; then
emit "verdict=cleared  (elf_window_reserve_in's reconciling fallback now reserves the whole window with no free hole against the cygwin-linked child -- item 1's reserve verb works live; this flips from blocked-by-committed-occupant and re-aims the rung toward adopt/yield/place)"
else
emit "verdict=blocked-by-committed-occupant  (the raw whole-window handover is refused against the cygwin-linked child, as the occupant spike found, but elf_window_reserve_in's reconciling fallback ALSO fails: reserve_in=$rres, window_covered=$rcov. The child's low window is not the bare MEM_RESERVE DR-0068 models -- it is low_window_occupant=$rocc: a private reservation PLUS committed pages below the free tail. elf_window_plan refuses any committed occupant (returns -1), so the fallback returns win_err_refused. Item 1's reserve verb does not yet clear a real cygwin-linked child; the design must account for a committed low occupant, not only a reserved one)"
fi
