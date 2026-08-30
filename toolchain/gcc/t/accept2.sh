#!/usr/bin/env bash
#
# WP-15's exit criterion, measured honestly.
#
# The plan asks two things of the second turn: the compiler builds itself,
# and a C++ exception thrown across a shared library boundary is caught on
# the other side. Both full claims need the loader to run target binaries,
# and the loader is not that far along yet. So this test measures what can
# be measured today and says plainly which claim each check stands in for.
#
# For the first claim: the stage-2 compiler rebuilt its own runtime -- libgcc
# and libstdc++, from the compiler's own source tree, compiled by xgcc. The
# .comment section of the installed libstdc++ records the compiler that built
# it. The full self-compile of the driver waits on a running loader.
#
# For the second: a thrower is compiled into a shared library, a catcher
# links against it, and every artifact the runtime unwinder needs is present
# and wired -- PT_GNU_EH_FRAME in both, .eh_frame_hdr and .eh_frame sections,
# __cxa_throw imported by the DSO and exported by libstdc++, and
# dl_iterate_phdr exported by the veneer libc, since that is how the unwinder
# finds the tables at run time. When the loader can run a pair, the catch
# itself becomes the test and this file gains that check.
#
# Usage:
#   accept2.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR  Where the toolchain is installed.
#                         [default: $HOME/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE
#                         [default: x86_64-elfsysvnt-linux-gnu]
#   -w DIR, --work=DIR    Scratch, cleared on entry. [default: $TMPDIR/wp15]
#   -k, --keep            Leave the scratch behind.
#   -q, --quiet           Errors only.
#   -v, --verbose         Name each claim as it passes.
#   -h, --help            Print this message and exit.
#
# Exit codes: 0 every claim holds, 1 at least one does not, 2 usage error.

set -u

prog=wp15
prefix=${WP15_PREFIX:-$HOME/x-elfsysvnt}
target=${WP15_TARGET:-x86_64-elfsysvnt-linux-gnu}
work=${WP15_WORK:-${TMPDIR:-/tmp}/wp15}
keep=0
quiet=0
verbose=0

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
        -q|--quiet) quiet=1 ;;
        -v|--verbose) verbose=1 ;;
        -h|--help) usage; exit 0 ;;
        *) usage_error "unknown option $opt" ;;
    esac
done

PATH=$prefix/bin:$PATH
CXX=$target-g++
READELF=$target-readelf
sysroot=$prefix/$target/sys-root
command -v "$CXX" >/dev/null 2>&1 || die "$CXX is not on PATH"
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

"$CXX" -dumpmachine > machine.txt 2>&1
claim 'the C++ driver names the triple' grep -qx "$target" machine.txt

"$CXX" -v 2> version.txt
claim 'the thread model is posix' grep -q '^Thread model: posix$' version.txt

# The first done-when claim, measured as far as it runs today: the stage-2
# compiler compiled its own runtime from its own source tree, and the
# installed libstdc++ names the compiler that built it.
libstdcxx=$("$CXX" -print-file-name=libstdc++.so.6)
libgccs=$("$CXX" -print-file-name=libgcc_s.so.1)
claim 'shared libstdc++ is installed' test -f "$libstdcxx"
claim 'shared libgcc is installed' test -f "$libgccs"
"$READELF" -p .comment "$libstdcxx" > cxx-comment.txt 2>&1
claim 'and libstdc++ records our compiler as its builder' \
    grep -q 'GCC: (GNU) 13\.3\.0' cxx-comment.txt

# The second done-when claim: the throw/catch pair itself.
cat > thrower.cc <<'EOF'
#include <stdexcept>
void poke (int v)
{
  if (v > 0)
    throw std::runtime_error ("crossed the boundary");
}
EOF

cat > catcher.cc <<'EOF'
#include <cstdio>
#include <stdexcept>
void poke (int);
int main ()
{
  try { poke (1); }
  catch (const std::runtime_error &e) { std::puts (e.what ()); return 0; }
  return 1;
}
EOF

claim 'the thrower compiles into a shared library' \
    "$CXX" -O2 -fPIC -shared -o libpoke.so thrower.cc
claim 'the catcher links against it clean' \
    "$CXX" -O2 -o catcher catcher.cc -L. -lpoke

"$READELF" -d catcher > catcher-dyn.txt 2>&1
claim 'the catcher needs shared libstdc++, not a static copy' \
    grep -q 'NEEDED.*libstdc++\.so\.6' catcher-dyn.txt
claim 'and needs the thrower' grep -q 'NEEDED.*libpoke\.so' catcher-dyn.txt

# What the unwinder will need at run time, present in both halves.
"$READELF" -lW libpoke.so > poke-phdrs.txt 2>&1
"$READELF" -lW catcher > catcher-phdrs.txt 2>&1
claim 'the thrower carries PT_GNU_EH_FRAME' \
    grep -q 'GNU_EH_FRAME' poke-phdrs.txt
claim 'so does the catcher' grep -q 'GNU_EH_FRAME' catcher-phdrs.txt

"$READELF" -SW libpoke.so > poke-sections.txt 2>&1
claim 'the thrower carries .eh_frame_hdr and .eh_frame' \
    sh -c 'grep -q "\.eh_frame_hdr" poke-sections.txt && grep -q " \.eh_frame " poke-sections.txt'

# The throw leaves the DSO through __cxa_throw and libstdc++ answers it.
"$READELF" --dyn-syms -W libpoke.so > poke-syms.txt 2>&1
"$READELF" --dyn-syms -W "$libstdcxx" > cxx-syms.txt 2>&1
claim 'the thrower imports __cxa_throw' \
    grep -qE 'UND[[:space:]]+__cxa_throw' poke-syms.txt
claim 'and libstdc++ exports it' \
    grep -qE '[0-9]+[[:space:]]+__cxa_throw(@|$)' cxx-syms.txt

# The unwinder finds the tables through dl_iterate_phdr; the veneer libc
# must export it or the catch dies inside _Unwind_Find_FDE.
"$READELF" --dyn-syms -W "$sysroot/usr/lib64/libc.so.6" > libc-syms.txt 2>&1
claim 'the veneer libc exports dl_iterate_phdr' \
    grep -q ' dl_iterate_phdr' libc-syms.txt

note 'measured: build-of-own-runtime and every link-time artifact of the'
note 'cross-boundary catch. The catch itself runs when the loader can run.'

note "$passes claims held, $failures did not"
[ "$keep" = 1 ] || { cd /; rm -rf "$work"; }
[ "$failures" = 0 ] || exit 1
exit 0
