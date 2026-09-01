#!/usr/bin/env bash
# reent-stub-link 1.0 -- does the loader's PE host stub link in the
# real-process shape, and does the resulting stub start?
#
# acceptance/reent/README.md item 1 asks that loader/exec/stub.c be relinked in
# the real-process shape -- -nostdlib against the WP-26 crt0.o and -lcygwin, the
# shape spike/reent-bringup proved carries a reent-consuming body -- so _dll_crt0
# brings the reent up the sanctioned way. This spike measures how far that gets
# on this tree, so the rung rests on a reproduced fact rather than a plan.
#
# The finding, reproduced: the link SUCCEEDS (realproc_stub_links=yes) -- the
# stub's whole translation unit set links -nostdlib + crt0 + -lcygwin, once
# -lgcc supplies the compiler builtins (__chkstk_ms) -nostdlib drops. But the
# resulting stub, run standalone, does NOT reach its --version path
# (realproc_stub_reaches_version=no): it faults during startup. The stub is a
# minimal non-PIE PE that adopts a parent-reserved low window (DR-0028); run as
# a real process of the faced runtime, _dll_crt0 lays out its own low mappings
# and the two collide before main. So item 1 is not a link change alone -- the
# stub's window/image-base contract has to be reconciled with the real-process
# startup, which is the WP-41/WP-43-shaped work the README names. The reent
# across the loader (item 3) stays deferred behind that and the WP-53 libc.so.6.
#
# The faced runtime wedges on a host pty, so the built stub is run detached via
# cmd with stdin from NUL, the way spike/reent-bringup/measure.sh runs its probe.
#
# SKIPs (verdict yes, exit 0) when the faced DLL or the WP-26 build tree are
# absent, both being uncommitted build products.
set -u

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
exec_dir=$repo/loader/exec
loader=$repo/loader
dest=""
[ "${1:-}" = "-o" ] && { dest=$2; shift 2; }

emit() { if [ -n "$dest" ]; then printf '%s\n' "$*" >>"$dest"; else printf '%s\n' "$*"; fi; }
emit_lines() { while IFS= read -r ln; do emit "$ln"; done; }
[ -n "$dest" ] && : >"$dest"

# Build products under a/ are gitignored -- they live only in the main checkout,
# never in a session worktree. Resolve a/ against the shared git common dir.
main=$(cd "$(git -C "$repo" rev-parse --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
[ -n "$main" ] || main=$repo
out=$main/a/build/wp27-face
dll=$out/elfsysv1.dll
build=$main/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin
release=$(sed -n 's/.*RELEASE\[\][[:space:]]*=[[:space:]]*"\(.*\)".*/\1/p' "$exec_dir/stub.c" | head -1)

emit "script  reent-stub-link 1.0"
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/reent-stub-link.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c \
$loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

# The real-process shape: -nostdlib so the default crt is not linked, WP-26
# crt0.o so _dll_crt0 runs, -lcygwin for the faced runtime, -lkernel32 for the
# Win32 the stub calls, -lgcc for the builtins -nostdlib drops. -Wl,--stack is
# the stub's reduced reserve (DR-0028); it does not change the finding.
if gcc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
-mno-red-zone -fno-stack-protector -nostdlib -Wl,--stack,0x100000 \
-o "$tmp/reent-stub.exe" \
"$exec_dir/stub.c" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" \
"$exec_dir/dyn_init.c" "$exec_dir/enter.S" $loader_srcs \
"$build/crt0.o" -L"$build" -lcygwin -lkernel32 -lgcc 2>"$tmp/build.err"; then
emit "realproc_stub_links=yes"
else
emit "realproc_stub_links=no"
sed 's/^/    ld: /' "$tmp/build.err" | emit_lines
emit "verdict=no"
exit 1
fi

# Does the linked stub reach its --version path? Run it detached from the pty.
cp "$tmp/reent-stub.exe" "$out/reent-stub.exe"
( cd "$out" && rm -f reent-stub.ver \
&& timeout 60 cmd /c "reent-stub.exe --version > reent-stub.ver 2>&1 < NUL" ) 2>/dev/null
got=$(tr -d '\r\n' < "$out/reent-stub.ver" 2>/dev/null)
rm -f "$out/reent-stub.exe" "$out/reent-stub.ver"

if [ "$got" = "$release" ]; then
emit "realproc_stub_reaches_version=yes"
else
emit "realproc_stub_reaches_version=no"
fi
emit "verdict=yes"
