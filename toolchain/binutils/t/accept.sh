#!/usr/bin/env bash
#
# WP-12's exit criterion, run rather than argued.
#
# Three claims, and the plan states all three: as accepts .symver, ld accepts
# --version-script and produces a .gnu.version_d that readelf -V prints, and a
# linked object's EI_OSABI and .note.ABI-tag match doc/target-definition.md.
#
# The second is the one worth the trouble. The failure this exists to catch is
# not a linker that rejects --version-script; it is a linker that accepts the
# option, links without complaint, and silently discards the version names,
# which is exactly the trap symbol-versioning-formats.md records for the PE
# route. So the test reads the verdefs back out rather than checking an exit
# code.
#
# There is no libc yet, so everything links -nostdlib. That is the whole point
# of running this at WP-12 instead of waiting for WP-53.
#
# Usage:
#   accept.sh [options]
#
# Options:
#   -B DIR, --bindir=DIR  Where the cross tools live. [default: from PATH]
#   -T TRIPLE, --target=TRIPLE
#                         Tool prefix. [default: x86_64-elfsysvnt-linux-gnu]
#   -w DIR, --work=DIR    Scratch. Cleared on entry. [default: $TMPDIR/wp12]
#   -k, --keep            Leave the scratch behind for inspection.
#   -t, --terse           One key=value per line.
#   -q, --quiet           Errors only.
#   -v, --verbose         Show each command's output.
#   -d, --debug           Trace execution; implies --verbose.
#   -V, --version         Print the version and exit.
#   -h, --help            Print this message and exit.
#
# Exit codes: 0 every claim holds, 1 at least one does not, 2 usage error.

set -u

prog=accept
release='accept 1.0'

here=$(cd "$(dirname "$0")" && pwd)

bindir=${ACCEPT_BINDIR:-}
target=${ACCEPT_TARGET:-x86_64-elfsysvnt-linux-gnu}
work=${ACCEPT_WORK:-${TMPDIR:-/tmp}/wp12}
keep=${ACCEPT_KEEP:-0}
terse=${ACCEPT_TERSE:-0}
quiet=${ACCEPT_QUIET:-0}
verbose=${ACCEPT_VERBOSE:-0}
debug=${ACCEPT_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

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
        -B|--bindir|-T|--target|-w|--work) takes=1 ;;
    esac
    if [ "$takes" = 1 ] && [ -z "$val" ]; then
        [ $# -gt 0 ] || usage_error "$opt wants a value"
        val=$1; shift
    fi
    case $opt in
        -B|--bindir) bindir=$val ;;
        -T|--target) target=$val ;;
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

[ -z "$bindir" ] || PATH=$bindir:$PATH
AS=$target-as
LD=$target-ld
READELF=$target-readelf
OBJDUMP=$target-objdump
for t in "$AS" "$LD" "$READELF" "$OBJDUMP"; do
    command -v "$t" >/dev/null 2>&1 || usage_error "$t is not on PATH"
done

rm -rf "$work"
mkdir -p "$work" || usage_error "cannot create $work"

passes=0
failures=0

# A claim and the shell test that settles it. The message is printed on
# failure only, because a run that prints five lines saying everything is
# fine is a run nobody reads.
claim() {
    what=$1; shift
    if "$@"; then
        passes=$((passes + 1))
        chat "ok: $what"
    else
        failures=$((failures + 1))
        note "FAIL: $what"
    fi
}

run() {
    if [ "$verbose" = 1 ]; then
        "$@"
    else
        "$@" > "$work/last.out" 2>&1
    fi
}

cd "$work" || usage_error "cannot enter $work"

# Everything below builds on these three, so a failure here is fatal rather
# than counted: there is nothing to assert about an object that never
# assembled.
run "$AS" -o symver.o "$here/symver.s" || { note "the assembler rejected symver.s"; exit 1; }
run "$AS" -o note.o "$here/abi-note.s" || { note "the assembler rejected abi-note.s"; exit 1; }
run "$AS" -o ifunc.o "$here/ifunc.s" || { note "the assembler rejected ifunc.s"; exit 1; }

"$READELF" -sW symver.o > syms.txt 2>&1
claim '.symver survives assembly at both nodes' \
    sh -c 'grep -q "memcpy@GLIBC_2.2.5" syms.txt && grep -q "memcpy@@GLIBC_2.14" syms.txt'

run "$LD" -shared -soname libc.so.6 -o lib.so symver.o note.o \
    --version-script="$here/version.map" ||
    { note "the linker rejected --version-script"; exit 1; }

"$READELF" -VW lib.so > ver.txt 2>&1

# The trap this whole test exists for: a linker that takes the option, links
# clean, and drops the names.
claim 'ld wrote a .gnu.version_d readelf can print' \
    grep -q 'Version definition section' ver.txt
claim 'both nodes are defined, not just the younger' \
    sh -c 'grep -q "Name: GLIBC_2.2.5" ver.txt && grep -q "Name: GLIBC_2.14" ver.txt'
claim 'the parent chain is recorded' \
    grep -q 'Parent 1: GLIBC_2.2.5' ver.txt

# Spike 4's condition on WP-53, checked here because it is cheaper to find at
# WP-12: elfdeps formats the versioned Provides off the base verdef node and
# the unversioned one off DT_SONAME, so a library whose two disagree produces
# dependencies that do not match the vendor's.
claim 'the base verdef node carries DT_SONAME' \
    grep -q 'Flags: BASE .* Name: libc.so.6' ver.txt

"$READELF" -sW lib.so > dyn.txt 2>&1
claim 'the two bodies are separate symbols' \
    sh -c 'grep -q "memcpy@GLIBC_2.2.5" dyn.txt && grep -q "memcpy@@GLIBC_2.14" dyn.txt'

"$READELF" -hW lib.so > hdr.txt 2>&1
"$READELF" -nW lib.so > notes.txt 2>&1
"$READELF" -lW lib.so > segs.txt 2>&1

claim 'an ordinary object gets ELFOSABI_NONE' \
    grep -q 'OS/ABI: *UNIX - System V' hdr.txt
claim 'the ABI-tag reads back as Linux 3.2.0' \
    grep -q 'OS: Linux, ABI: 3.2.0' notes.txt
claim 'the note landed in a PT_NOTE' \
    grep -q '^  NOTE' segs.txt

run "$LD" -shared -o ifunc.so ifunc.o ||
    { note "the linker rejected an ifunc object"; exit 1; }
"$READELF" -hW ifunc.so > ihdr.txt 2>&1

# The other half of the EI_OSABI rule. Without this the first claim proves
# only that the byte is zero, not that it is zero for a reason.
claim 'an ifunc promotes the object to ELFOSABI_GNU' \
    grep -q 'OS/ABI: *UNIX - GNU' ihdr.txt

# The fourth criterion, added after the first three were already met. ld
# relaxes the psABI's TLS sequences in place and writes its own %fs-relative
# fetch, which none of the claims above would have caught; spike/ld-tls-relaxation/
# measured it and the bfd patch under ../patches/ refuses the relocations that
# license the rewrite.
#
# One object per model, not one object carrying three. A combined object
# proves only that the first model met was refused, and a draft of that patch
# passed exactly that way while local dynamic still linked and leaked.
refused() {
    model=$1
    run "$AS" --defsym "MODEL_$model=1" -o "tls$model.o" "$here/tls-models.s" ||
        { note "the assembler rejected model $model"; return 1; }
    # Assembling has to keep working. gas is not the layer at fault here, and
    # a target that could not express the sequences could not diagnose them.
    "$LD" -o "tls$model.exe" "tls$model.o" -e _start > "tls$model.err" 2>&1
    [ $? -ne 0 ] || return 1
    grep -q 'thread pointer in the FS segment' "tls$model.err"
}

claim 'general dynamic is refused'         refused GD
claim 'local dynamic is refused'           refused LD
claim 'initial exec is refused'            refused IE

# And the other half: refusing everything would also pass the three above.
leaks() {
    run "$AS" --defsym MODEL_LE=1 -o tlsLE.o "$here/tls-models.s" || return 1
    run "$LD" -o tlsLE.exe tlsLE.o -e _start || return 1
    ! "$OBJDUMP" -d tlsLE.exe | grep -q '%fs:'
}
claim 'local exec still links, with no %fs in the output' leaks

if [ "$terse" = 1 ]; then
    printf 'target=%s\npasses=%d\nfailures=%d\n' "$target" "$passes" "$failures"
else
    note "$passes claims held, $failures did not"
fi

[ "$keep" = 1 ] || { cd /; rm -rf "$work"; }

[ "$failures" = 0 ] || exit 1
exit 0
