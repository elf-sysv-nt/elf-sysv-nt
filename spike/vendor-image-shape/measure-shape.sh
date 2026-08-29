#!/usr/bin/env bash
#
# Read the shape of el8's own binaries, rather than remembering it.
#
# WP-10 has to write down four values that every shipped artifact will carry,
# and three of them are only defensible if they agree with what Red Hat
# already shipped: the EI_OSABI byte, the .note.ABI-tag payload, and the
# dynamic linker SONAME that every el8 PT_INTERP names. This walks a small
# pinned slice of the el8 set and counts what is actually there.
#
# Two items off the plan's Not verified lists ride along, because the tree
# needed for the first question answers them for free. PT_LOAD alignment
# decides how often WP-41's reserve-the-span-first constraint bites, and
# whether rpm-build carries elfdeps and fileattrs/elf.attr decides whether
# WP-62 is confirmation or work.
#
# The archives are kept between runs and reused when the pin still matches;
# the unpacked tree is cleared and rebuilt every run, so a package dropped
# from the pin leaves nothing behind.
#
# Usage:
#   measure-shape.sh [options]
#
# Options:
#   -D DIR, --dest=DIR       Archives and the unpacked tree land here.
#   -p FILE, --packages=FILE The pin. [default: beside this one]
#   -m URL, --mirror=URL     Repository root.
#                            [default: https://dl.rockylinux.org/pub/rocky/8.10]
#   -x PATH, --rpmx=PATH     The unpacker.
#                            [default: ../versioned-libc/rpmx.py]
#   -t, --terse              The counts alone, one key=value per line.
#   -q, --quiet              Errors only.
#   -v, --verbose            Name every package as it is handled.
#   -d, --debug              Trace execution; implies --verbose.
#   -V, --version            Print the version and exit.
#   -h, --help               Print this message and exit.
#
# Each option is also settable as MEASURE_SHAPE_<OPTION>, and the option wins
# over the variable.
#
# Exit codes: 0 success, 1 failure, 2 usage error.

set -u

prog=measure-shape
release='measure-shape 1.0'

here=$(cd "$(dirname "$0")" && pwd)

dest=${MEASURE_SHAPE_DEST:-}
packages=${MEASURE_SHAPE_PACKAGES:-$here/packages.tsv}
mirror=${MEASURE_SHAPE_MIRROR:-https://dl.rockylinux.org/pub/rocky/8.10}
rpmx=${MEASURE_SHAPE_RPMX:-$here/../versioned-libc/rpmx.py}
terse=${MEASURE_SHAPE_TERSE:-0}
quiet=${MEASURE_SHAPE_QUIET:-0}
verbose=${MEASURE_SHAPE_VERBOSE:-0}
debug=${MEASURE_SHAPE_DEBUG:-0}

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
        -D|--dest|-p|--packages|-m|--mirror|-x|--rpmx) takes=1 ;;
    esac
    if [ "$takes" = 1 ] && [ -z "$val" ]; then
        [ $# -gt 0 ] || usage_error "$opt wants a value"
        val=$1; shift
    fi
    case $opt in
        -D|--dest) dest=$val ;;
        -p|--packages) packages=$val ;;
        -m|--mirror) mirror=$val ;;
        -x|--rpmx) rpmx=$val ;;
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
[ -r "$packages" ] || die "cannot read the pin: $packages"
[ -r "$rpmx" ] || die "cannot read the unpacker: $rpmx"
command -v python3 >/dev/null 2>&1 || die "python3 is not on PATH"
command -v curl >/dev/null 2>&1 || die "curl is not on PATH"
command -v readelf >/dev/null 2>&1 || die "readelf is not on PATH"

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        python3 -c 'import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
    fi
}

mkdir -p "$dest/rpm" || die "cannot create $dest/rpm"
rm -rf "$dest/ref" || die "cannot clear the unpacked tree"
mkdir -p "$dest/ref" || die "cannot create the unpacked tree"

fetched=0
reused=0

while IFS="$(printf '\t')" read -r repo location want; do
    case $repo in ''|\#*) continue ;; esac
    file=$(basename "$location")
    path=$dest/rpm/$file
    if [ -f "$path" ] && [ "$(sha256 "$path")" = "$want" ]; then
        reused=$((reused + 1))
        chat "have $file"
    else
        rm -f "$path"
        chat "fetch $file"
        curl -fsS --retry 3 --retry-delay 2 -o "$path" \
            "$mirror/$repo/$location" || die "cannot fetch $file"
        got=$(sha256 "$path")
        [ "$got" = "$want" ] || die "checksum mismatch on $file: $got"
        fetched=$((fetched + 1))
    fi
    python3 "$rpmx" "$path" "$dest/ref" >/dev/null || die "cannot unpack $file"
done < "$packages"

# An ELF file is one that starts with the magic. Extension and mode both lie
# here: glibc ships .so.debug companions that are ELF and uninteresting, and
# ships plain scripts under names that look like programs.
list=$dest/elf-files
: > "$list"
find "$dest/ref" -type f ! -name '*.debug' -print > "$dest/all-files"
while read -r f; do
    magic=$(head -c 4 "$f" 2>/dev/null | od -An -tx1 | tr -d ' \n')
    [ "$magic" = "7f454c46" ] && printf '%s\n' "$f" >> "$list"
done < "$dest/all-files"
rm -f "$dest/all-files"

count=$(wc -l < "$list" | tr -d ' ')
[ "$count" -gt 0 ] || die "the pin unpacked no ELF files at all"
chat "$count ELF files"

# First argument is the sed script that pulls the field out; everything after
# it is handed to readelf as-is, which is why the order looks backwards.
tally() {
    script=$1; shift
    while read -r f; do
        readelf "$@" "$f" 2>/dev/null
    done < "$list" | sed -n "$script" | sort | uniq -c | sort -rn
}

section() { [ "$terse" = 1 ] || printf '\n## %s\n\n' "$1"; }

emit() { [ "$terse" = 1 ] || sed 's/^ */    /'; }

[ "$terse" = 1 ] || cat <<'HEADER'
# What shape are el8's own binaries? The four counts WP-10 needs, and two
# items off the plan's Not verified lists that the same tree answers.
#
# Generated by measure-shape 1.0. Rerunning it regenerates this file.

HEADER

printf 'run_date=%s\n' "$(date +%Y-%m-%d)"
printf 'mirror=%s\n' "$mirror"
printf 'packages=%d\n' "$(grep -cve '^#' -e '^$' "$packages")"
printf 'fetched=%d\n' "$fetched"
printf 'reused=%d\n' "$reused"
printf 'elf_files=%d\n' "$count"
printf 'readelf=%s\n' "$(readelf --version | head -1)"

section 'e_type'
tally '/^  Type:/s/^  Type: *//p' -h | emit

section 'EI_OSABI'
tally '/^  OS\/ABI:/s/^  OS\/ABI: *//p' -h | emit

section 'PT_LOAD p_align'
tally '/^  LOAD /s/.*[ 	]\([0-9a-fx]*\)$/\1/p' -lW | emit

section '.note.ABI-tag'
tally '/NT_GNU_ABI_TAG/s/.*\(OS: .*\)$/\1/p' -nW | emit

# No -W here. readelf's -p takes the section as its own argument, so the
# bundled -pW spelling every other call in this file uses would ask for a
# section named W and then treat .interp as a file.
section 'PT_INTERP'
tally '/^  \[ *0\]/s/^  \[ *0\] *//p' -p .interp | emit

section 'DT_SONAME'
tally '/(SONAME)/s/.*\[\(.*\)\]$/\1/p' -dW | emit

# The OSABI split is the one result a count alone misreads: the answer is not
# a ratio but which objects sit on the GNU side, and it is always glibc's own.
section 'Objects whose EI_OSABI is not ELFOSABI_NONE'
while read -r f; do
    readelf -h "$f" 2>/dev/null | grep -q 'UNIX - GNU' && printf '%s\n' "${f#$dest/ref}"
done < "$list" | sort | emit

section 'rpm-build dependency generator'
attrdir=$dest/ref/usr/lib/rpm/fileattrs
if [ -x "$dest/ref/usr/lib/rpm/elfdeps" ]; then
    printf 'elfdeps present, %d bytes\n' \
        "$(wc -c < "$dest/ref/usr/lib/rpm/elfdeps" | tr -d ' ')" | emit
else
    printf 'elfdeps ABSENT\n' | emit
fi
printf '%d fileattrs\n' "$(ls "$attrdir" 2>/dev/null | wc -l | tr -d ' ')" | emit
[ "$terse" = 1 ] || sed 's/^/    /' "$attrdir/elf.attr" 2>/dev/null

[ "$terse" = 1 ] || note "measured $count ELF files under $dest/ref"
exit 0
