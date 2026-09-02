#!/usr/bin/env bash
# build-host.sh -- link the faced-runtime crossing host (WP-56, DR-0071).
#
# The crossing host is the loader's exec/stub translation-unit set linked in the
# real-process shape: -nostdlib against the WP-26 crt0.o and -lcygwin, with the
# loader/exec/realproc/ seam on (-DELFSYSV_REALPROC), so _dll_crt0 brings the
# faced elfsysv1.dll up as the process's sole Cygwin runtime. This is the exact
# link recipe spike/reent-stub-realproc-run proved (realproc_stub_links=yes,
# reaches --version and crosses fd 2), promoted here into a committed build
# recipe the crossing host and its driver share.
#
# It emits the host beside the faced DLL, so elfsysv1.dll resolves as the
# process's own module when the host is run from there. Both the faced DLL and
# the WP-26 crt0.o are uncommitted build products under a/build/; this recipe
# resolves them against the shared git common dir so it runs from a session
# worktree too, and reports a clear SKIP (exit 2) when they are absent.
#
# Usage: build-host.sh [-o OUTDIR]   (OUTDIR defaults to the faced DLL's dir)
set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
outdir=""
[ "${1:-}" = "-o" ] && { outdir=$2; shift 2; }

# Build products under a/ are gitignored; resolve them against the shared git
# common dir so this runs from a session worktree as well as the checkout.
main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
face=$main/a/build/wp27-face
dll=$face/elfsysv1.dll
cygbuild=$main/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin
[ -n "$outdir" ] || outdir=$face

if [ ! -f "$dll" ] || [ ! -f "$cygbuild/crt0.o" ]; then
	echo "skip=no faced DLL ($dll) or WP-26 crt0.o; build them first" >&2
	exit 2
fi

L=$repo/loader
E=$L/exec
RP=$E/realproc
loader_srcs="$E/reserve.c $L/map/elf_map.c $L/map/host_mem.c $L/elf/elf_parse.c \
$L/process/process_image.c $L/reloc/elf_reloc.c $L/reloc/reloc_resolve.S"
rp_srcs="$RP/realproc-str.c $RP/realproc-fmt.c $RP/realproc-file.c $RP/realproc-cross.c"
host_srcs="$E/stub.c $E/exec_kind.c $E/dyn_exec.c $E/dyn_init.c $E/enter.S $loader_srcs"

CF="-std=gnu11 -O1 -g -mno-red-zone -fno-stack-protector -nostdlib -DELFSYSV_REALPROC"
LIBS="$cygbuild/crt0.o -L$cygbuild -lcygwin -lgcc -lkernel32"

mkdir -p "$outdir"
if gcc $CF -I"$E" -o "$outdir/elfsysv-crossing-host.exe" $host_srcs $rp_srcs $LIBS; then
	echo "$outdir/elfsysv-crossing-host.exe"
	exit 0
fi
echo "link failed" >&2
exit 1
