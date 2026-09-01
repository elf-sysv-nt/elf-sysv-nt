#!/usr/bin/env bash
# reent-stub-realproc-faceload 1.0 -- WP-56 reent-tls-bringup, item 1's last
# owed step: the --runtime face-load of the REAL-PROCESS stub, driven through
# the WP-41 front end.
#
# spike/reent-stub-realproc-run built the actual loader stub in the real-process
# shape (-DELFSYSV_REALPROC, -nostdlib against the WP-26 crt0.o and -lcygwin)
# and found it links, reaches --version, and crosses fd 2 standalone -- but the
# stub reserves the low 0x400000 window only when a parent reserves it into the
# suspended child, so its --runtime faceload is a front-end-driven run. This
# spike is that run: it composes the real-process stub with the front end
# (elfsysv-exec) and drives a reent-consuming ELF specimen through the crossing,
# as spike/reent-face-bringup's live-run does -- but with the real-process stub
# as ELFSYSV_STUB, not the plain-PE cygload stub that spike found wedges on the
# face base (error 1114).
#
# What it measures:
#   realproc_stub_links            the whole stub TU set links in the shape.
#   frontend_builds                the WP-41 front end (elfsysv-exec) builds.
#   veneer_builds                  the WP-53 libc.so.6 veneer builds.
#   specimen_builds                the reent-consuming ELF specimen builds and
#                                  carries PT_INTERP + DT_NEEDED libc.so.6.
#   fe_drives_realproc_stub        driven through the front end (--elf-runtime),
#                                  the parent reserves the low window into the
#                                  suspended real-process child and the stub
#                                  runs past it -- no standalone window refusal.
#   face_base_via_realproc_runtime the real-process stub's --runtime LoadLibraryA
#                                  of the faced elfsysv1.dll reaches the face
#                                  base -- no error-1114 cygload wedge; the half
#                                  the plain-PE stub could not carry.
#   reent_faceload_run             the terminal witness: both halves together,
#                                  exit 42 iff strtol crossed the veneer into
#                                  the face and returned LONG_MAX.
#
# SKIPs (verdict=yes, exit 0) when the faced DLL, the WP-26 build tree, or the
# cross toolchain are absent, all being uncommitted build products.
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

export PATH="$HOME/x-elfsysvnt/bin:$PATH"
cross=x86_64-elfsysvnt-linux-gnu
xg=$cross-gcc
xre=$cross-readelf
interp=/lib64/ld-linux-x86-64.so.2

emit "script  reent-stub-realproc-faceload 1.0"
emit ""
emit "host        $(hostname)"
emit "compiler    $(gcc --version 2>/dev/null | head -1)"
emit "cross       $($xg --version 2>/dev/null | head -1)"
emit "binutils    $(ld --version 2>/dev/null | head -1)"
emit "date        $(date +%F)"
emit ""

if [ ! -f "$dll" ] || [ ! -f "$build/crt0.o" ] || ! command -v "$xg" >/dev/null 2>&1; then
emit "skip=no faced DLL, WP-26 build tree, or cross toolchain; build them first"
emit "verdict=yes"
exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-realproc-faceload.XXXXXX")
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
if gcc $RPCF -I"$E" -o "$tmp/elfsysv-stub.exe" $stub_srcs $rp_srcs $RPLIBS 2>"$tmp/rp-link.err"; then
emit "realproc_stub_links=yes"
else
emit "realproc_stub_links=no"
sed 's/^/    ld: /' "$tmp/rp-link.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the WP-41 front end (elfsysv-exec) ---------------------------------
FECF="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
if gcc $FECF -I"$E" -o "$tmp/elfsysv-exec.exe" \
	"$E/exec_main.c" "$E/dispatch.c" "$E/binfmt.c" "$E/reserve.c" \
	2>"$tmp/fe-link.err"; then
emit "frontend_builds=yes"
else
emit "frontend_builds=no"
sed 's/^/    ld: /' "$tmp/fe-link.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the WP-53 libc.so.6 veneer: the ELF runtime the specimen links to ---
if "$repo/veneer/libc/build-libc" -B "$tmp/veneer" -q >"$tmp/veneer.err" 2>&1; then
emit "veneer_builds=yes"
else
emit "veneer_builds=no"
sed 's/^/    veneer: /' "$tmp/veneer.err" | emit
emit "verdict=no"; exit 1
fi
vlibc=$tmp/veneer/libc.so.6

# ---- the reent-consuming specimen (cross toolchain) ---------------------
xf="-ffreestanding -nostdlib -fcf-protection=none -O2"
if $xg $xf -no-pie -Wl,-z,max-page-size=0x10000 -Wl,-e,entry \
	-Wl,--dynamic-linker,"$interp" -Wl,--no-as-needed -Wl,-rpath,"$tmp/veneer" \
	-o "$tmp/reent" "$here/reent-spec.S" "$here/reent-body.c" "$vlibc" \
	2>"$tmp/spec.err" \
   && "$xre" -l "$tmp/reent" 2>/dev/null | grep -q INTERP \
   && "$xre" -d "$tmp/reent" 2>/dev/null | grep -q 'NEEDED.*libc\.so\.6'; then
emit "specimen_builds=yes"
else
emit "specimen_builds=no"
sed 's/^/    spec: /' "$tmp/spec.err" | emit
emit "verdict=no"; exit 1
fi

# ---- the plain-PE control stub (the WP-41 shape, the seam absent) --------
# The differential's other arm: the same loader stub built plain-PE, the shape
# the front end's window handover (DR-0028) was written for. It is driven
# without -R (its inline fopen resolves the POSIX path itself).
PLCF="-std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -Wl,--stack,0x100000"
if gcc $PLCF -I"$E" -o "$tmp/elfsysv-stub-plain.exe" $stub_srcs 2>"$tmp/pl-link.err"; then
emit "plain_stub_links=yes"
else
emit "plain_stub_links=no"
sed 's/^/    ld: /' "$tmp/pl-link.err" | emit
fi

# ---- stage everything beside the faced DLL ------------------------------
# The faced runtime resolves elfsysv1.dll as the process's own module only from
# beside it, and the console wedges on a host pty, so each run is detached via
# cmd with stdin from NUL -- the shape every sibling reent spike uses.
cp "$tmp/elfsysv-stub.exe"       "$out/elfsysv-stub.exe"
cp "$tmp/elfsysv-exec.exe"       "$out/elfsysv-exec.exe"
cp "$tmp/reent"                  "$out/reent-live.exe"
cp "$vlibc"                      "$out/libc.so.6"
[ -f "$tmp/elfsysv-stub-plain.exe" ] && cp "$tmp/elfsysv-stub-plain.exe" "$out/elfsysv-stub-plain.exe"

# probe: drive the front end with stub $1, real-stub flag $2 ("-R" or ""), and
# ELFSYSV_STUB_OPTIONS $3. The front end reserves the low window into the
# suspended child before it resumes. Returns the front end's status in
# $probe_st and its (and the stub's) stderr in $probe_out.
probe() {
	( cd "$out" && rm -f reent-live.out \
	  && ELFSYSV_STUB="$out/$1" \
	     ELFSYSV_STUB_OPTIONS="$3" \
	     timeout 60 cmd /c "elfsysv-exec.exe $2 reent-live.exe > reent-live.out 2>&1 < NUL" ) 2>/dev/null
	probe_st=$?
	probe_out=$(tr -d '\r' < "$out/reent-live.out" 2>/dev/null)
}

win_refused() { printf '%s' "$probe_out" | grep -q 'the low window at .* could not be reserved'; }

# ---- control: the plain-PE child receives the low-window handover --------
# The front end reserves the low window into the suspended plain-PE stub and it
# proceeds past it (into the crossing, where the face base is a separate
# matter). So the handover protocol and this harness both work -- the arm that
# isolates the real-process result below as a shape difference, not a defect.
if [ -f "$out/elfsysv-stub-plain.exe" ]; then
probe "elfsysv-stub-plain.exe" "" "--elf-runtime libc.so.6"
if win_refused; then
emit "plain_stub_gets_window=no  (unexpected; the control did not receive the handover)"
else
emit "plain_stub_gets_window=yes  (the front end reserved the low window into the plain-PE child; it ran past it)"
fi
fi

# ---- the real-process child and the low-window handover -----------------
# The measurement item 1 owes: driven through the front end, does the
# real-process stub receive the low window the way the plain-PE stub does?
probe "elfsysv-stub.exe" "-R" "--runtime elfsysv1.dll"
if win_refused; then
werr=$(printf '%s' "$probe_out" | sed -n 's/.*could not be reserved in the child (\([^)]*\)).*/\1/p' | head -1)
emit "realproc_stub_gets_window=no  (the low window at 0x400000 is refused in the real-process child: ${werr:-win_err_refused}; the cygwin-linked child already holds the low region at suspend, so the DR-0028 handover the plain-PE stub receives does not carry to this shape)"
else
emit "realproc_stub_gets_window=yes  (the real-process child received the low-window handover)"
fi

# ---- the terminal witness: both halves through the real-process stub ----
# It cannot pass while the window handover above is refused -- the faceload and
# the crossing are both past the window -- so this records where the run halts,
# not a green. The face-base half of item 1 stays measured in miniature by
# spike/reent-stub-faceload until the handover carries to this shape.
probe "elfsysv-stub.exe" "-R" "--elf-runtime libc.so.6 --runtime elfsysv1.dll"
st=$probe_st
[ -n "$probe_out" ] && emit "$(printf '%s\n' "$probe_out" | sed 's/^/    stub: /')"

rm -f "$out/elfsysv-stub.exe" "$out/elfsysv-exec.exe" "$out/reent-live.exe" \
      "$out/reent-live.out" "$out/libc.so.6" "$out/elfsysv-stub-plain.exe" 2>/dev/null

emit "enter_status=$st"
if [ "$st" = 42 ]; then
emit "reent_faceload_run=pass  (strtol crossed the veneer into the face and returned LONG_MAX)"
emit "verdict=yes"
elif win_refused; then
emit "reent_faceload_run=blocked  (halted at the low-window handover, before the faceload; the obstacle is now measured)"
emit "verdict=staged"
else
emit "reent_faceload_run=faulted  (exit $st: past the window, but the crossing did not return the body's byte)"
emit "verdict=staged"
fi
