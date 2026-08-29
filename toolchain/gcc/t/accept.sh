#!/usr/bin/env bash
#
# WP-13's exit criterion, and the obligation WP-11 left behind.
#
# The plan asks two things: a freestanding object compiles, and -mno-red-zone
# is on by default in the specs rather than passed by the caller. WP-11 added
# a third when config.guess was taught to ask the compiler which vendor it is
# building for, since without that define a native build silently configures
# as x86_64-pc-linux-gnu.
#
# The red-zone claim is checked two ways on purpose. -Q --help=target reports
# what the compiler believes its default is, which is the direct question; and
# a leaf that must spill is compiled both ways, which is what the belief is
# worth. A first version checked only codegen, against a fixture that did not
# spill, and passed while proving nothing.
#
# Usage:
#   accept.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR  Where the toolchain is installed.
#                         [default: $HOME/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE
#                         [default: x86_64-elfsysvnt-linux-gnu]
#   -w DIR, --work=DIR    Scratch, cleared on entry. [default: $TMPDIR/wp13]
#   -k, --keep            Leave the scratch behind.
#   -t, --terse           One key=value per line.
#   -q, --quiet           Errors only.
#   -v, --verbose         Name each claim as it passes.
#   -d, --debug           Trace execution; implies --verbose.
#   -V, --version         Print the version and exit.
#   -h, --help            Print this message and exit.
#
# Exit codes: 0 every claim holds, 1 at least one does not, 2 usage error.

set -u

prog=wp13
release='wp13 accept 1.0'

here=$(cd "$(dirname "$0")" && pwd)

prefix=${WP13_PREFIX:-$HOME/x-elfsysvnt}
target=${WP13_TARGET:-x86_64-elfsysvnt-linux-gnu}
work=${WP13_WORK:-${TMPDIR:-/tmp}/wp13}
keep=${WP13_KEEP:-0}
terse=${WP13_TERSE:-0}
quiet=${WP13_QUIET:-0}
verbose=${WP13_VERBOSE:-0}
debug=${WP13_DEBUG:-0}

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
        -P|--prefix|-T|--target|-w|--work) takes=1 ;;
    esac
    if [ "$takes" = 1 ] && [ -z "$val" ]; then
        [ $# -gt 0 ] || usage_error "$opt wants a value"
        val=$1; shift
    fi
    case $opt in
        -P|--prefix) prefix=$val ;;
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
PATH=$prefix/bin:$PATH
CC=$target-gcc
READELF=$target-readelf
command -v "$CC" >/dev/null 2>&1 || die "$CC is not on PATH"
command -v "$READELF" >/dev/null 2>&1 || die "$READELF is not on PATH"

rm -rf "$work"; mkdir -p "$work" || die "cannot create $work"
cd "$work" || die "cannot enter $work"

passes=0
failures=0
claim() {
    what=$1; shift
    if "$@"; then passes=$((passes + 1)); chat "ok: $what"
    else failures=$((failures + 1)); note "FAIL: $what"; fi
}

"$CC" -dumpmachine > machine.txt 2>&1
claim 'the driver names the triple' grep -qx "$target" machine.txt

echo | "$CC" -dM -E - > defines.txt 2>&1
claim '__ELFSYSVNT__ is predefined with nothing passed' \
    grep -q '^#define __ELFSYSVNT__ 1$' defines.txt
# Still Linux to everything that asks, which is the point of putting the name
# in the vendor field rather than the os field.
claim 'and __linux__ survives beside it' grep -q '^#define __linux__ 1$' defines.txt

"$CC" -Q --help=target > target-default.txt 2>&1
"$CC" -Q --help=target -mred-zone > target-opt-in.txt 2>&1
claim '-mno-red-zone is the compiler default' \
    grep -qE '^[[:space:]]*-mno-red-zone[[:space:]]+\[enabled\]' target-default.txt
claim '-mred-zone still turns it back on' \
    grep -qE '^[[:space:]]*-mred-zone[[:space:]]+\[enabled\]' target-opt-in.txt

# What the belief is worth. The volatile array is what forces the spill; a
# fixture the optimiser can keep in registers compiles to the same code either
# way and the test proves nothing.
cat > leaf.c <<'EOF'
long leaf (long a, long b, long c)
{
  volatile long x[4];
  x[0] = a; x[1] = b; x[2] = c; x[3] = a + b + c;
  return x[0] + x[1] + x[2] + x[3];
}
EOF

"$CC" -O2 -S -o leaf-default.s leaf.c || die "the compiler rejected leaf.c"
"$CC" -O2 -mred-zone -S -o leaf-red.s leaf.c || die "-mred-zone was rejected"

below() { grep -cE -e '-[0-9]+\(%rsp\)' "$1"; }

claim 'by default a spilling leaf makes room instead of using the red zone' \
    sh -c 'grep -q "subq.*%rsp" leaf-default.s && [ "$(grep -cE -e "-[0-9]+\(%rsp\)" leaf-default.s)" = 0 ]'
# The negative control. Without it the claim above is satisfied by a compiler
# that cannot generate the sequence at all.
claim 'and with -mred-zone it uses it, so the flag is what is doing the work' \
    sh -c '[ "$(grep -cE -e "-[0-9]+\(%rsp\)" leaf-red.s)" -gt 0 ]'

# x87 stays on. Defaulting the red zone means writing TARGET_SUBTARGET_DEFAULT,
# and i386/unix.h already keeps MASK_80387, MASK_IEEE_FP and MASK_FLOAT_RETURNS
# there. A target header that assigns rather than ORs turns the x87 off, and
# nothing says so until libgcc fails on __mulxc3 much later.
cat > longdouble.c <<'EOF'
long double scale (long double v) { return v * 3.0L; }
EOF
claim 'long double still compiles, so the x87 survived the red-zone default' \
    "$CC" -O2 -c -o longdouble.o longdouble.c

cat > free.c <<'EOF'
int answer (void) { return 42; }
EOF
claim 'a freestanding object compiles' \
    "$CC" -ffreestanding -O2 -c -o free.o free.c
"$READELF" -hW free.o > free-hdr.txt 2>&1
claim 'and its EI_OSABI matches the record' \
    grep -q 'OS/ABI: *UNIX - System V' free-hdr.txt

# doc/target-definition.md fixes this and gcc already agreed, so this guards
# against a later change rather than asserting a patch.
"$CC" -dumpspecs > specs.txt 2>&1
claim 'the driver names the loader the record fixes' \
    grep -q '/lib64/ld-linux-x86-64.so.2' specs.txt

if [ "$terse" = 1 ]; then
    printf 'target=%s\npasses=%d\nfailures=%d\n' "$target" "$passes" "$failures"
else
    note "$passes claims held, $failures did not"
fi

[ "$keep" = 1 ] || { cd /; rm -rf "$work"; }
[ "$failures" = 0 ] || exit 1
exit 0
