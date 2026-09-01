#!/usr/bin/env bash
#
# DR-0061's exit criterion: the cross toolchain links every image
# granule-separable by default, so no two PT_LOAD segments of unlike protection
# share the host's 0x10000 allocation granule and the loader (DR-0008) need not
# refuse an image the harness itself built.
#
# The claim is checked two ways. The driver's own specs must carry the
# max-page-size default -- the direct question, what the toolchain believes.
# And a real link is inspected: a trivial program links to four PT_LOAD segments
# of mixed protection (R, R+X, R, R+W), and every one of them must be aligned to
# at least the granule, which is what the belief is worth. A specs default that
# some later change drops would pass the first check and fail the second.
#
# Usage:
#   granule-default.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR  Where the toolchain is installed.
#                         [default: $HOME/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE
#                         [default: x86_64-elfsysvnt-linux-gnu]
#   -w DIR, --work=DIR    Scratch, cleared on entry. [default: $TMPDIR/wp56-gran]
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

prog=granule-default
release='granule-default 1.0'

here=$(cd "$(dirname "$0")" && pwd)

prefix=${GRAN_PREFIX:-$HOME/x-elfsysvnt}
target=${GRAN_TARGET:-x86_64-elfsysvnt-linux-gnu}
work=${GRAN_WORK:-${TMPDIR:-/tmp}/wp56-gran}
keep=${GRAN_KEEP:-0}
terse=${GRAN_TERSE:-0}
quiet=${GRAN_QUIET:-0}
verbose=${GRAN_VERBOSE:-0}
debug=${GRAN_DEBUG:-0}

# The host allocation granule the mapper protects at (DR-0008).
granule=$((0x10000))

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

# A bare main links to several PT_LOAD segments of mixed protection, the same
# shape a vendor image has; it is the specimen both claims below rest on.
cat > t.c <<'EOF'
int main(void) { return 0; }
EOF
"$CC" -O2 -o t t.c || die "the compiler rejected t.c"

# What the toolchain believes its default is: the effective link it would run.
# -### honors the installed specs file; -dumpspecs shows only the built-in
# specs and would miss the default this test exists to guard.
"$CC" -### -o probe t.c > link.txt 2>&1 || true
claim 'the driver carries a max-page-size link default' \
    grep -q 'max-page-size=0x10000' link.txt

# What the belief is worth: every PT_LOAD p_align must be at least the granule.
# With each segment aligned to a 0x10000 boundary, no two of unlike protection
# can share one granule -- the property DR-0061 requires and DR-0008 refuses.
# readelf -lW prints each LOAD on one line with p_align last.
"$READELF" -lW t > phdrs.txt 2>&1
aligns=$(awk '$1 == "LOAD" { print $NF }' phdrs.txt)
[ -n "$aligns" ] || die "no PT_LOAD segments found in the specimen"

nload=0
small=0
for a in $aligns; do
    nload=$((nload + 1))
    av=$((a))
    if [ "$av" -lt "$granule" ]; then small=$((small + 1)); fi
done

claim 'the specimen has more than one PT_LOAD, so the shape is real' \
    sh -c "[ $nload -gt 1 ]"
claim 'every PT_LOAD is aligned to at least the granule' \
    sh -c "[ $small -eq 0 ]"

if [ "$terse" = 1 ]; then
    printf 'target=%s\nloads=%d\nsub_granule=%d\npasses=%d\nfailures=%d\n' \
        "$target" "$nload" "$small" "$passes" "$failures"
else
    note "$passes claims held, $failures did not ($nload PT_LOAD, $small below granule)"
fi

[ "$keep" = 1 ] || { cd /; rm -rf "$work"; }
[ "$failures" = 0 ] || exit 1
exit 0
