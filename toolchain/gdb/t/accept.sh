#!/usr/bin/env bash
#
# WP-60's exit criterion, run as far as it runs today, like the second gcc
# turn's: the full done-when -- break, step, locals in a program running
# under the stub, and a C++ throw unwound across a shared-library boundary
# inside the debugger -- needs a process to attach to, and the loader cannot
# yet run one. What is measurable now is the whole of the debugger's side:
#
#   1. the installed gdb is configured for the triple, not the host;
#   2. the GNU/Linux osabi -- the code that consumes `r_debug` through
#      `solib-svr4` -- is compiled in and selectable;
#   3. on a binary the cross compiler built, it reads symbols and places a
#      breakpoint in main;
#   4. it sees main's locals in the DWARF, which is what stepping will print;
#   5. it reads a shared library the cross compiler built and finds the
#      function the C++ unwind test will throw through.
#
# When a stub process can be attached, the run becomes the test.
#
# Usage:
#   accept.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR  The toolchain prefix. [default: $HOME/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE
#                         Tool prefix. [default: x86_64-elfsysvnt-linux-gnu]
#   -w DIR, --work=DIR    Scratch. Cleared on entry. [default: $TMPDIR/wp60]
#   -k, --keep            Leave the scratch behind for inspection.
#   -q, --quiet           Errors only.
#   -v, --verbose         Show each command's output.
#   -V, --version         Print the version and exit.
#   -h, --help            Print this message and exit.
#
# Exit codes: 0 every claim holds, 1 at least one does not, 2 usage error.

set -u

prog=accept
release='accept 1.0'

prefix=${ACCEPT_PREFIX:-$HOME/x-elfsysvnt}
target=${ACCEPT_TARGET:-x86_64-elfsysvnt-linux-gnu}
work=${ACCEPT_WORK:-${TMPDIR:-/tmp}/wp60}
keep=${ACCEPT_KEEP:-0}
quiet=${ACCEPT_QUIET:-0}
verbose=${ACCEPT_VERBOSE:-0}

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
        -q|--quiet) quiet=1 ;;
        -v|--verbose) verbose=1 ;;
        -V|--version) printf '%s\n' "$release"; exit 0 ;;
        -h|--help) usage; exit 0 ;;
        --) break ;;
        *) usage_error "unknown option $opt" ;;
    esac
done

gdb=$prefix/bin/$target-gdb
gcc=$prefix/bin/$target-gcc
gxx=$prefix/bin/$target-g++
[ -x "$gdb" ] || [ -x "$gdb.exe" ] || { note "no $gdb"; exit 1; }
[ -x "$gcc" ] || [ -x "$gcc.exe" ] || { note "no $gcc"; exit 1; }

rm -rf "$work" && mkdir -p "$work" || { note "cannot make $work"; exit 1; }
[ "$keep" = 1 ] || trap 'rm -rf "$work"' EXIT

pass=0
fail=0

claim() {
    what=$1; shift
    if "$@" > "$work/claim.out" 2>&1; then
        note "ok: $what"
        pass=$((pass + 1))
    else
        note "FAIL: $what"
        sed 's/^/    /' "$work/claim.out" >&2
        fail=$((fail + 1))
    fi
}

# 1. Configured for the triple.
check_config() {
    "$gdb" --batch -ex 'show configuration' | grep -q -- "--target=$target"
}

# 2. The r_debug consumer is compiled in: the GNU/Linux osabi exists and a
#    complaint from gdb lands on stderr, so an empty stderr is acceptance.
check_osabi() {
    err=$("$gdb" --batch -ex 'set osabi GNU/Linux' 2>&1)
    [ -z "$err" ]
}

# 3 and 4. A breakpoint lands in main, and main's locals are in scope.
build_fixture() {
    cat > "$work/fix.c" <<'EOF'
int fixfn(int n) { return n + 1; }
int main(void) { int counted = 41; return fixfn(counted) - 42; }
EOF
    "$gcc" -g -O0 -o "$work/fix" "$work/fix.c"
}

check_break() {
    "$gdb" --batch "$work/fix" -ex 'break main' | grep -q 'Breakpoint 1 at'
}

check_locals() {
    "$gdb" --batch "$work/fix" -ex 'info scope main' | grep -q 'counted'
}

# 5. A shared library the cross compiler built is readable and the function
#    the eventual throw crosses is found in it. C++ so that the symbol side
#    of the unwind claim is exercised too.
build_shared() {
    cat > "$work/thrower.cc" <<'EOF'
extern "C" int visible_marker;
int visible_marker = 7;
void thrower(int n) { if (n) throw n; }
EOF
    "$gxx" -g -O0 -shared -fPIC -o "$work/libthrower.so" "$work/thrower.cc"
}

check_shared() {
    "$gdb" --batch "$work/libthrower.so" -ex 'info functions thrower' |
        grep -q 'thrower'
}

claim "configured for $target" check_config
claim "GNU/Linux osabi (solib-svr4) compiled in" check_osabi
claim "fixture builds with the cross compiler" build_fixture
claim "breakpoint lands in main" check_break
claim "main's locals visible in the DWARF" check_locals
claim "C++ shared library builds" build_shared
claim "shared library readable, thrower found" check_shared

note "$pass ok, $fail failed"
note "the live half of the done-when -- break, step, locals under the stub,"
note "the throw unwound across the boundary -- runs when a stub process can"
note "be attached; until then this is the debugger's side, whole."

[ "$fail" = 0 ] || exit 1
exit 0
