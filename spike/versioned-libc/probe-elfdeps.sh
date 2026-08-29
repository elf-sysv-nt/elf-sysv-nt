#!/usr/bin/env bash
#
# Put a synthesized libc.so.6 in front of el8's dependency generator.
#
# The design this project rests on gives up PE and writes its own loader so
# that a Linux userland keeps its object format, its symbol versioning and its
# loader semantics. The other three spikes ask whether that can be built. This
# one asks whether building it repairs what it was supposed to repair, and the
# sharpest form of that question is a string: does rpm write
# libc.so.6(GLIBC_2.2.5)(64bit) when it is pointed at a library we made up.
#
# It matters because every el8 package linking libc carries requires of that
# exact shape, and because the surveyed alternative cannot produce it at all.
# doc/symbol-versioning-formats.md records why: PE has nowhere to put a version
# node, rpm has no PE generator, and a dependency comparison against the vendor
# would differ structurally on nearly the whole package set. If the ELF route
# does produce the vendor string, that whole deviation closes.
#
# Four measurements, and the fourth is the one that decides it.
#
#   provides   elfdeps --provides over a synthesized libc.so.6 carrying one
#              verdef node, against the same over el8's own libc.
#   requires   elfdeps --requires over a synthesized program carrying the
#              matching verneed, against the same over an el8 binary.
#   closure    whether the require the program emits is the string the library
#              provides. A generator that produced two spellings of one edge
#              would satisfy nothing, and one node is enough to see it.
#   ladder     the same library synthesized with el8 libc's whole node list
#              rather than one node, its provides set against the vendor's.
#              One node proves the mechanism; the ladder prices the veneer,
#              because a package requiring GLIBC_2.14 is not satisfied by a
#              library that only defines GLIBC_2.2.5.
#
# Nothing is built and nothing is installed. synth-libc.py lays the fixtures
# out byte by byte rather than calling a compiler, on purpose: what this
# project will eventually ship is a synthesized library, and a library that
# gcc and ld produced would be answering an easier question.
#
# Usage:
#   probe-elfdeps.sh [options]
#
# Options:
#   -D DIR, --dest=DIR     The tree fetch-elfdeps.sh built.
#   -w DIR, --work=DIR     Where the fixtures are written. [default: DEST/fixture]
#   -o FILE, --output=FILE Transcript destination; - is stdout. [default: -]
#   -t, --terse            The summary block alone, one key=value per line.
#   -q, --quiet            Errors only.
#   -v, --verbose          Trace each fixture as it is made.
#   -d, --debug            Trace execution; implies --verbose.
#   -V, --version          Print the version and exit.
#   -h, --help             Print this message and exit.
#
# Each option is also settable as PROBE_ELFDEPS_<OPTION>, and the option wins
# over the variable.
#
# Exit codes: 0 the verdict was reached, whatever it was; 1 failure; 2 usage.

set -u

prog=probe-elfdeps
release='probe-elfdeps 1.0'

here=$(cd "$(dirname "$0")" && pwd)

dest=${PROBE_ELFDEPS_DEST:-}
work=${PROBE_ELFDEPS_WORK:-}
output=${PROBE_ELFDEPS_OUTPUT:--}
terse=${PROBE_ELFDEPS_TERSE:-0}
quiet=${PROBE_ELFDEPS_QUIET:-0}
verbose=${PROBE_ELFDEPS_VERBOSE:-0}
debug=${PROBE_ELFDEPS_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
usage_error() { printf '%s: %s\n' "$prog" "$*" >&2; usage >&2; exit 2; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
chat() { [ "$verbose" = 1 ] && note "$@"; return 0; }

while [ $# -gt 0 ]; do
    opt=$1; shift
    val=
    case $opt in --*=*) val=${opt#*=}; opt=${opt%%=*} ;; esac
    takes=0
    case $opt in -D|--dest|-w|--work|-o|--output) takes=1 ;; esac
    if [ "$takes" = 1 ] && [ -z "$val" ]; then
        [ $# -gt 0 ] || usage_error "$opt wants a value"
        val=$1; shift
    fi
    case $opt in
        -D|--dest) dest=$val ;;
        -w|--work) work=$val ;;
        -o|--output) output=$val ;;
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
[ -n "$dest" ] || usage_error "--dest is required"
[ -n "$work" ] || work=$dest/fixture

elfdeps=$dest/sysroot/usr/lib/rpm/elfdeps
libs=$dest/sysroot/usr/lib64
vendor_consumer=$dest/sysroot/usr/bin/rpm
[ -x "$elfdeps" ] || die "no elfdeps under $dest; run fetch-elfdeps.sh first"
[ -x "$vendor_consumer" ] || die "no vendor binary at $vendor_consumer"
command -v python3 >/dev/null 2>&1 || die "python3 is not on PATH"

vendor_libc=$dest/ref/usr/lib64/libc.so.6
[ -e "$vendor_libc" ] || die "no vendor libc under $dest/ref"
target=$(readlink "$vendor_libc" 2>/dev/null || true)
[ -n "$target" ] && vendor_libc=$dest/ref/usr/lib64/$target

# The generator is run with its own libraries in front of it and nothing
# else's. Exporting that path would reach every other process this script
# starts, and el8 libraries under a host toolchain is a way to lose an hour.
deps() { env LD_LIBRARY_PATH="$libs" "$elfdeps" "$@"; }

rm -rf "$work" || die "cannot clear $work"
mkdir -p "$work" || die "cannot create $work"

chat "synthesizing the one-node library and its consumer"
python3 "$here/synth-libc.py" -q -o "$work/libc.so.6" \
    || die "cannot synthesize the library"
python3 "$here/synth-libc.py" -q -c -o "$work/consumer" \
    || die "cannot synthesize the consumer"

deps --provides "$work/libc.so.6" > "$work/synth-provides.txt"
deps --requires "$work/consumer" > "$work/synth-requires.txt"
deps --provides "$vendor_libc" > "$work/vendor-provides.txt"
deps --requires "$vendor_consumer" > "$work/vendor-requires.txt"

ladder=$(sed -n 's/^libc\.so\.6(\([^)]\+\))(64bit)$/\1/p' \
    "$work/vendor-provides.txt" | awk '!seen[$0]++' | paste -sd, -)
[ -n "$ladder" ] || die "the vendor libc offered no version nodes to copy"

chat "synthesizing the full-ladder library"
python3 "$here/synth-libc.py" -q -n "$ladder" -o "$work/libc-ladder.so.6" \
    || die "cannot synthesize the ladder library"
deps --provides "$work/libc-ladder.so.6" > "$work/ladder-provides.txt"

# Which of the two names a versioned provide is read from. In any library a
# linker produced, DT_SONAME and the base verdef node carry the same string,
# so the question never comes up and the answer gets assumed. A library we
# synthesize can get it wrong, and this says what that costs.
chat "synthesizing the split-name library"
python3 "$here/synth-libc.py" -q -b libmisnamed.so.1 \
    -o "$work/libc-split.so.6" || die "cannot synthesize the split library"
deps --provides "$work/libc-split.so.6" > "$work/split-provides.txt"

node=${ladder%%,*}
want="libc.so.6($node)(64bit)"
plain='libc.so.6()(64bit)'

has() { grep -Fxq -- "$2" "$1"; }
yesno() { if "$@"; then printf yes; else printf no; fi; }

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        python3 -c 'import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
    fi
}

sort -u "$work/ladder-provides.txt" > "$work/ladder-sorted.txt"
sort -u "$work/vendor-provides.txt" > "$work/vendor-sorted.txt"
diff "$work/ladder-sorted.txt" "$work/vendor-sorted.txt" > "$work/ladder.diff"
ladder_equal=$?

grep -E '^libc\.so\.6\(GLIBC' "$work/vendor-requires.txt" | sort -u \
    > "$work/vendor-consumer-libc.txt"
missed=$(comm -23 "$work/vendor-consumer-libc.txt" "$work/ladder-sorted.txt" \
    | wc -l)

verdict=no
if has "$work/synth-provides.txt" "$want" \
   && has "$work/synth-requires.txt" "$want" \
   && has "$work/vendor-provides.txt" "$want"; then
    verdict=yes
fi

emit() {
    printf '# Spike 4: does el8 elfdeps read a vendor-shaped Requires off a\n'
    printf '# synthesized libc.so.6?\n#\n'
    printf '# Generated by %s. Rerunning it regenerates this file.\n\n' "$release"
    printf 'run_date=%s\n' "$(date +%F)"
    printf 'probe_host=%s\n' "$(uname -sr)"
    printf 'probe_libc=%s\n' "$(ldd --version 2>/dev/null | head -1)"
    printf 'elfdeps=%s\n' "$elfdeps"
    printf 'vendor_libc=%s\n' "$(basename "$vendor_libc")"
    printf 'vendor_consumer=%s\n' "$(basename "$vendor_consumer")"
    printf 'node=%s\n' "$node"
    printf 'ladder_nodes=%d\n' "$(printf '%s' "$ladder" | awk -F, '{print NF}')"
    printf 'synth_libc_sha256=%s\n' "$(sha256 "$work/libc.so.6")"
    printf 'synth_consumer_sha256=%s\n' "$(sha256 "$work/consumer")"
    printf '\n'
}

sections() {
    printf '## Synthesized libc.so.6, one verdef node, --provides\n\n'
    sed 's/^/    /' "$work/synth-provides.txt"
    printf '\n## Synthesized consumer, matching verneed, --requires\n\n'
    sed 's/^/    /' "$work/synth-requires.txt"
    printf '\n## el8 %s, --provides, first ten of %d\n\n' \
        "$(basename "$vendor_libc")" "$(wc -l < "$work/vendor-provides.txt")"
    head -10 "$work/vendor-provides.txt" | sed 's/^/    /'
    printf '\n## el8 %s, --requires\n\n' "$(basename "$vendor_consumer")"
    sed 's/^/    /' "$work/vendor-requires.txt"
    printf '\n## Full-ladder library against the vendor, sorted and uniqued\n\n'
    if [ "$ladder_equal" = 0 ]; then
        printf '    identical, %d lines each\n' \
            "$(wc -l < "$work/ladder-sorted.txt")"
    else
        sed 's/^/    /' "$work/ladder.diff"
    fi
    printf '\n## A library whose base verdef node is not its DT_SONAME\n\n'
    sed 's/^/    /' "$work/split-provides.txt"
    printf '\n## Version requires of the el8 binary not in the ladder\n\n'
    if [ "$missed" = 0 ]; then
        printf '    none, of %d\n' "$(wc -l < "$work/vendor-consumer-libc.txt")"
    else
        comm -23 "$work/vendor-consumer-libc.txt" "$work/ladder-sorted.txt" \
            | sed 's/^/    /'
    fi
    printf '\n'
}

summary() {
    printf 'synth_provides_node=%s\n' \
        "$(yesno has "$work/synth-provides.txt" "$want")"
    printf 'synth_provides_soname=%s\n' \
        "$(yesno has "$work/synth-provides.txt" "$plain")"
    printf 'synth_requires_node=%s\n' \
        "$(yesno has "$work/synth-requires.txt" "$want")"
    printf 'vendor_provides_same_string=%s\n' \
        "$(yesno has "$work/vendor-provides.txt" "$want")"
    printf 'closure=%s\n' "$verdict"
    printf 'version_provide_follows_verdef_base=%s\n' \
        "$(yesno has "$work/split-provides.txt" \
            "libmisnamed.so.1($node)(64bit)")"
    printf 'ladder_matches_vendor=%s\n' \
        "$([ "$ladder_equal" = 0 ] && printf yes || printf no)"
    printf 'vendor_consumer_version_requires=%d\n' \
        "$(wc -l < "$work/vendor-consumer-libc.txt")"
    printf 'vendor_consumer_requires_unmet=%d\n' "$missed"
    printf 'verdict=%s\n' "$verdict"
}

render() {
    if [ "$terse" = 1 ]; then
        summary
    else
        emit
        sections
        printf '## Summary\n\n'
        summary
    fi
}

if [ "$output" = - ]; then
    render
else
    render > "$output" || die "cannot write $output"
    note "transcript written to $output"
fi

exit 0
