#!/usr/bin/env bash
#
# The bar for the glibc port, written before the port (README.md § The bar).
# Four claims, the fourth a stretch that is reported and does not gate:
#
#   1. glibc is installed in the sysroot and check-no-syscall passes over
#      every object it installed: no syscall instruction, no %fs access.
#   2. A static C program using printf links against it, and its disassembly
#      is clean by the same check.
#   3. That program runs under lk-host, prints hello, and exits 0.
#   4. (stretch) The same program linked dynamically runs the same way
#      through the rebuilt ld.so.
#
# Usage:
#   accept.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR  Where the toolchain is installed.
#                         [default: $ELFSYSVNT_PREFIX]
#   -T TRIPLE, --target=TRIPLE
#                         The triple is doc/design/target-definition.md's;
#                         change it there rather than here.
#                         [default: x86_64-elfsysvnt-linux-gnu]
#   -w DIR, --work=DIR    Scratch, cleared on entry. [default: $TMPDIR/glibc-accept]
#       --cc=CC           The host compiler for lk-host.
#                         [default: x86_64-w64-mingw32-gcc]
#   -k, --keep            Leave the scratch behind.
#   -t, --terse           One key=value per line.
#   -q, --quiet           Errors only.
#   -v, --verbose         Name each claim as it passes.
#   -d, --debug           Trace execution; implies --verbose.
#   -V, --version         Print the version and exit.
#   -h, --help            Print this message and exit.
#
# Each option is also settable as GLIBC_ACCEPT_<OPTION>, and the option wins.
#
# Exit codes: 0 claims 1 to 3 hold, 1 at least one does not, 2 usage error,
#             77 a toolchain the claims need is absent so nothing was checked.

set -u

prog=glibc-accept
release='glibc-accept 1.0'

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
. "$root/bin/roots.sh"

prefix=${GLIBC_ACCEPT_PREFIX:-$ELFSYSVNT_PREFIX}
target=${GLIBC_ACCEPT_TARGET:-x86_64-elfsysvnt-linux-gnu}
work=${GLIBC_ACCEPT_WORK:-${TMPDIR:-/tmp}/glibc-accept}
cc=${GLIBC_ACCEPT_CC:-x86_64-w64-mingw32-gcc}
keep=${GLIBC_ACCEPT_KEEP:-0}
terse=${GLIBC_ACCEPT_TERSE:-0}
quiet=${GLIBC_ACCEPT_QUIET:-0}
verbose=${GLIBC_ACCEPT_VERBOSE:-0}
debug=${GLIBC_ACCEPT_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

usage_error() { printf '%s: %s\n' "$prog" "$*" >&2; usage >&2; exit 2; }

note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

chat() { [ "$verbose" = 1 ] && note "$@"; return 0; }

while [ $# -gt 0 ]; do
    opt=$1; shift
    val=
    case $opt in
        --*=*) val=${opt#*=}; opt=${opt%%=*} ;;
    esac
    takes=0
    case $opt in
        -P|--prefix|-T|--target|-w|--work|--cc) takes=1 ;;
    esac
    if [ "$takes" = 1 ] && [ -z "$val" ]; then
        [ $# -gt 0 ] || usage_error "$opt wants a value"
        val=$1; shift
    fi
    case $opt in
        -P|--prefix) prefix=$val ;;
        -T|--target) target=$val ;;
        -w|--work) work=$val ;;
        --cc) cc=$val ;;
        -k|--keep) keep=1 ;;
        --no-keep) keep=0 ;;
        -t|--terse) terse=1 ;;
        --no-terse) terse=0 ;;
        -q|--quiet) quiet=1 ;;
        --no-quiet) quiet=0 ;;
        -v|--verbose) verbose=1 ;;
        --no-verbose) verbose=0 ;;
        -d|--debug) debug=1; verbose=1 ;;
        -V|--version) printf '%s\n' "$release"; exit 0 ;;
        -h|--help) usage; exit 0 ;;
        --) break ;;
        *) usage_error "unknown option $opt" ;;
    esac
done

[ "$debug" = 1 ] && set -x
PATH=$prefix/bin:$PATH
CC=$target-gcc
sysroot=$prefix/$target/sys-root
check=$here/../check-no-syscall
command -v "$CC" >/dev/null 2>&1 || { note "$CC is not on PATH; nothing checked"; exit 77; }
command -v python3 >/dev/null 2>&1 || { note "python3 is not on PATH; nothing checked"; exit 77; }

rm -rf "$work"; mkdir -p "$work/root" || die "cannot create $work"
cd "$work" || die "cannot enter $work"

passes=0
failures=0
stretch=
claim() {
    what=$1; shift
    if "$@"; then passes=$((passes + 1)); chat "ok: $what"
    else failures=$((failures + 1)); note "FAIL: $what"; fi
}

# 1. Installed, and clean.  The loader's SONAME is the one the target
# definition fixes; libc.a is what claim 2 links; libc.so.6 and ld.so are
# what claim 4 needs.
claim '1a. libc.a, libc.so.6 and ld-linux-x86-64.so.2 are in the sysroot' \
    test -f "$sysroot/usr/lib64/libc.a" -a -f "$sysroot/usr/lib64/libc.so.6" \
         -a -f "$sysroot/usr/lib64/ld-linux-x86-64.so.2"
claim '1b. the glibc headers are in the sysroot' \
    test -f "$sysroot/usr/include/gnu/stubs-64.h" -a -f "$sysroot/usr/include/stdio.h"
claim '1c. no installed object carries a syscall instruction or an %fs access' \
    python3 "$check" -q "$sysroot/usr/lib64" "$sysroot/usr/bin" "$sysroot/usr/sbin" \
        "$sysroot/usr/libexec"

# 2. A static hello links and is clean.  -no-pie because a static PIE needs
# the self-relocation path, which is claim 4's kind of question, not this one.
cp "$here/hello.c" hello.c
claim '2a. a static hello links against the new glibc' \
    "$CC" -O2 -static -no-pie -o hello-static hello.c
claim '2b. and its disassembly is clean' \
    python3 "$check" -q hello-static

# 3. It runs under lk-host.  The core is a host program built with mingw; a
# machine without that compiler reports the claim unchecked rather than failed.
lkhost=$work/lk-host.exe
have_host=0
if command -v "$cc" >/dev/null 2>&1; then
    if "$root/core/build.sh" -q --cc="$cc" -o "$lkhost"; then have_host=1
    else note "lk-host did not build; claims 3 and 4 fail"; fi
else
    note "$cc is not on PATH; claims 3 and 4 cannot run and count as failed"
fi

run_hello() {
    # lk-host is a Windows binary and opens the paths its own runtime sees.
    elf=$1
    elf_arg=$elf; root_arg=$work/root
    if command -v cygpath >/dev/null 2>&1; then
        elf_arg=$(cygpath -w "$elf"); root_arg=$(cygpath -w "$root_arg")
    fi
    "$lkhost" --quiet --timeout=20000 --root="$root_arg" --exe=/hello "$elf_arg" \
        > "$elf.out" 2> "$elf.err"
    rc=$?
    tr -d '\r' < "$elf.out" > "$elf.txt"
    [ "$rc" = 0 ] && [ "$(cat "$elf.txt")" = hello ]
}

if [ "$have_host" = 1 ] && [ -f hello-static ]; then
    claim '3. the static hello prints hello and exits 0 under lk-host' \
        run_hello "$work/hello-static"
else
    failures=$((failures + 1))
fi

# 4. Stretch: dynamic.  The loader and libc are copied into the run root at
# the paths PT_INTERP and the search path name, since the kernel resolves
# them inside the Linux root it is given.
if [ "$have_host" = 1 ] && "$CC" -O2 -o hello-dyn hello.c 2>/dev/null; then
    mkdir -p root/lib64 root/usr/lib64
    cp "$sysroot/usr/lib64/ld-linux-x86-64.so.2" "$sysroot/usr/lib64/libc.so.6" root/usr/lib64/
    cp root/usr/lib64/ld-linux-x86-64.so.2 root/lib64/
    if python3 "$check" -q hello-dyn && run_hello "$work/hello-dyn"; then
        stretch=held; chat "ok: 4. (stretch) the dynamic hello runs through the rebuilt ld.so"
    else
        stretch=missed; note "stretch not met: 4. the dynamic hello does not run"
    fi
else
    stretch=unbuilt
fi

if [ "$terse" = 1 ]; then
    printf 'target=%s\npasses=%d\nfailures=%d\nstretch=%s\n' "$target" "$passes" "$failures" "$stretch"
else
    note "$passes claims held, $failures did not; stretch $stretch"
fi

[ "$keep" = 1 ] || { cd /; rm -rf "$work"; }
[ "$failures" = 0 ] || exit 1
exit 0
