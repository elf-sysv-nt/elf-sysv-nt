#!/usr/bin/env bash
#
# WP-14's exit criterion: a static hello links, and the spike 2 stub runs it.
#
# Spike 2 had to synthesize its specimen. Its README says so plainly -- this
# machine had no toolchain that emitted a static ELF, so make-elf.py wrote the
# headers by hand and payload.S could not name an address because no linker
# was in the loop. WP-12 removed that constraint and this is the first thing
# to use the fact: the same stub, the same protocol, against an image our own
# ld produced with real program headers, a real .bss and a real symbol table.
#
# The stub is spike 2's and is built here rather than copied, so a change to
# it is caught by this test rather than silently diverged from.
#
# Usage:
#   run-tests.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR  Where the toolchain is installed.
#                         [default: $HOME/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE
#                         [default: x86_64-elfsysvnt-linux-gnu]
#   -S DIR, --spike=DIR   Spike 2's directory.
#                         [default: ../../../spike/map-and-jump]
#   -w DIR, --work=DIR    Scratch, cleared on entry. [default: $TMPDIR/wp14]
#   -k, --keep            Leave the scratch behind.
#   -t, --terse           One key=value per line.
#   -q, --quiet           Errors only.
#   -v, --verbose         Show each command's output.
#   -d, --debug           Trace execution; implies --verbose.
#   -V, --version         Print the version and exit.
#   -h, --help            Print this message and exit.
#
# Exit codes: 0 every claim holds, 1 at least one does not, 2 usage error.

set -u

prog=wp14
release='wp14 run-tests 1.0'

here=$(cd "$(dirname "$0")" && pwd)

prefix=${WP14_PREFIX:-$HOME/x-elfsysvnt}
target=${WP14_TARGET:-x86_64-elfsysvnt-linux-gnu}
spike=${WP14_SPIKE:-$here/../../../spike/map-and-jump}
work=${WP14_WORK:-${TMPDIR:-/tmp}/wp14}
keep=${WP14_KEEP:-0}
terse=${WP14_TERSE:-0}
quiet=${WP14_QUIET:-0}
verbose=${WP14_VERBOSE:-0}
debug=${WP14_DEBUG:-0}

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
        -P|--prefix|-T|--target|-S|--spike|-w|--work) takes=1 ;;
    esac
    if [ "$takes" = 1 ] && [ -z "$val" ]; then
        [ $# -gt 0 ] || usage_error "$opt wants a value"
        val=$1; shift
    fi
    case $opt in
        -P|--prefix) prefix=$val ;;
        -T|--target) target=$val ;;
        -S|--spike) spike=$val ;;
        -w|--work) work=$val ;;
        -k|--keep) keep=1 ;;
        -t|--terse) terse=1 ;;
        -q|--quiet) quiet=1 ;;
        -v|--verbose) verbose=1 ;;
        -d|--debug) debug=1; verbose=1 ;;
        -V|--version) printf '%s\n' "$release"; exit 0 ;;
        -h|--help) usage; exit 0 ;;
        --) break ;;
        *) usage_error "unknown option $opt" ;;
    esac
done

[ "$debug" = 1 ] && set -x
[ -d "$spike" ] || die "no spike 2 directory at $spike"

PATH=$prefix/bin:$PATH
CC=$target-gcc
LD=$target-ld
AS=$target-as
READELF=$target-readelf
sysroot=$prefix/$target/sys-root
libdir=$sysroot/usr/lib64

for t in "$AS" "$LD" "$READELF"; do
    command -v "$t" >/dev/null 2>&1 || die "$t is not on PATH; WP-12 first"
done
command -v "$CC" >/dev/null 2>&1 || die "$CC is not on PATH; WP-13 first"
[ -f "$libdir/crt1.o" ] || die "no crt1.o under $libdir; run build-csu first"

rm -rf "$work"; mkdir -p "$work" || die "cannot create $work"
cd "$work" || die "cannot enter $work"

passes=0
failures=0

claim() {
    what=$1; shift
    if "$@"; then
        passes=$((passes + 1)); chat "ok: $what"
    else
        failures=$((failures + 1)); note "FAIL: $what"
    fi
}

run() {
    if [ "$verbose" = 1 ]; then "$@"; else "$@" > "$work/last.out" 2>&1; fi
}

# Compiled with the cross compiler, which is the first time in this program
# that a C file becomes an ELF object for the target rather than by hand.
run "$CC" -c -O1 -g -ffreestanding -o hello.o "$here/hello.c" ||
    die "the cross compiler rejected hello.c"
run "$AS" --64 -o exit-spike2.o "$here/exit-spike2.S" ||
    die "the assembler rejected exit-spike2.S"

# Statically linked, non-PIE, at spike 2's own link base. 0x10000000 rather
# than 0x400000 because spike 2 measured the low span refused twenty times in
# twenty inside a Cygwin process; WP-41 owns fixing that and this test is not
# the place to relitigate it.
run "$LD" -static --no-dynamic-linker -Ttext-segment=0x10000000 \
    -o hello.elf "$libdir/crt1.o" "$libdir/crti.o" hello.o exit-spike2.o \
    "$libdir/crtn.o" ||
    die "the static link failed"

"$READELF" -hW hello.elf > hdr.txt 2>&1
"$READELF" -lW hello.elf > seg.txt 2>&1
"$READELF" -sW hello.elf > sym.txt 2>&1

claim 'it links at all'            test -s hello.elf
claim 'it is a static executable'  grep -q 'EXEC (Executable file)' hdr.txt
claim 'no interpreter is named'    sh -c '! grep -q INTERP seg.txt'
claim 'the entry is _start' \
    sh -c 'e=$(sed -n "s/.*Entry point address: *//p" hdr.txt);
           s=$(awk "/ _start\$/ {print \$2}" sym.txt | head -1);
           [ "0x$(printf %x $((s)))" = "$e" ] 2>/dev/null ||
           [ "$e" = "0x$(echo $s | sed "s/^0*//")" ]'
claim 'EI_OSABI matches the record' grep -q 'OS/ABI: *UNIX - System V' hdr.txt
# readelf -lW columns are Type Offset VirtAddr PhysAddr FileSiz MemSiz Flg
# Align, so MemSiz is $6 and FileSiz is $5. A first version compared $6 with
# $7, which is MemSiz against the flags string: never equal, so the claim
# passed on every image including ones with no .bss at all.
claim 'a segment has memsz above filesz, so there is a .bss to zero' \
    sh -c 'awk "/^  LOAD/ { if (strtonum(\$6) > strtonum(\$5)) f=1 } END { exit !f }" seg.txt'
claim 'the stack is not executable' \
    sh -c '! grep -q "GNU_STACK.* RWE" seg.txt'

# Spike 2's stub, built from its own sources so a change there is caught here.
run gcc -O1 -g -Wall -o stub.exe "$spike/stub.c" "$spike/enter.S" ||
    die "spike 2's stub did not build"

# The image reports through the handshake block and leaves by restoring the
# stack pointer the stub parked. A timeout means it neither reported nor left,
# which is a different failure from reporting the wrong thing.
timeout 30 ./stub.exe --terse --quiet hello.elf > ran.txt 2>&1
ran=$?

claim 'the stub mapped and entered it' test "$ran" -eq 0
claim 'the image came back' grep -q . ran.txt

if [ "$terse" = 1 ]; then
    printf 'target=%s\npasses=%d\nfailures=%d\nimage=%s\n' \
        "$target" "$passes" "$failures" "$(wc -c < hello.elf | tr -d ' ')"
else
    note "$passes claims held, $failures did not"
    [ "$quiet" = 1 ] || sed 's/^/    /' ran.txt
fi

[ "$keep" = 1 ] || { cd /; rm -rf "$work"; }
[ "$failures" = 0 ] || exit 1
exit 0
