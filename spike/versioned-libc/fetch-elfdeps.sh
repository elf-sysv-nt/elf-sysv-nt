#!/usr/bin/env bash
#
# Stand el8's own dependency generator up on a host that is not el8.
#
# The question this spike asks is what rpm's elfdeps writes when it is pointed
# at a library we synthesized, and the only authority on that is the elfdeps
# el8 ships. Reading its source and predicting the answer is the habit these
# spikes exist to break, so the binary itself is fetched and run.
#
# It runs off its own libraries rather than the host's. rpm-build carries
# /usr/lib/rpm/elfdeps; librpm, librpmio, libelf and the dozen libraries under
# those come from the same el8 set, are unpacked into one directory, and are
# put in front of elfdeps as LD_LIBRARY_PATH. What it borrows from the host is
# glibc alone, and only forward: a binary linked against 2.28 runs on 2.43,
# which is the direction glibc guarantees.
#
# The vendor glibc is fetched too and is deliberately not part of that path.
# Its libc.so.6 is an input to the probe -- the thing our synthesized library
# is compared against -- and a 2.28 libc on the loader path of a 2.43 host
# breaks every process the run starts, including this script's own.
#
# Nothing here wants root. No rpm, no rpm2cpio and no cpio are wanted either,
# because a host that had them would probably be el8 already; rpmx.py walks
# the header chain and the newc payload in Python. Every package is pinned by
# checksum in packages.tsv and a mismatch stops the run.
#
# The sysroot is reseeded from the archives on every run rather than added to,
# so a package dropped from packages.tsv leaves nothing behind. The archives
# themselves are kept and reused when their checksum still matches, since they
# are the slow part and they cannot change under a pin.
#
# Usage:
#   fetch-elfdeps.sh [options]
#
# Options:
#   -D DIR, --dest=DIR       Archives, sysroot and reference tree land here.
#   -p FILE, --packages=FILE The pin. [default: beside this one]
#   -m URL, --mirror=URL     Repository root.
#                            [default: https://dl.rockylinux.org/pub/rocky/8.10]
#   -k, --keep-rpm           Keep the downloaded archives. This is the default.
#       --no-keep-rpm        Delete each archive once it is unpacked.
#   -t, --terse              The summary block alone, one key=value per line.
#   -q, --quiet              Errors only.
#   -v, --verbose            Name every package as it is handled.
#   -d, --debug              Trace execution; implies --verbose.
#   -V, --version            Print the version and exit.
#   -h, --help               Print this message and exit.
#
# Each option is also settable as FETCH_ELFDEPS_<OPTION>, and the option wins
# over the variable.
#
# Exit codes: 0 success, 1 failure, 2 usage error.

set -u

prog=fetch-elfdeps
release='fetch-elfdeps 1.0'

here=$(cd "$(dirname "$0")" && pwd)

dest=${FETCH_ELFDEPS_DEST:-}
packages=${FETCH_ELFDEPS_PACKAGES:-$here/packages.tsv}
mirror=${FETCH_ELFDEPS_MIRROR:-https://dl.rockylinux.org/pub/rocky/8.10}
keeprpm=${FETCH_ELFDEPS_KEEP_RPM:-1}
terse=${FETCH_ELFDEPS_TERSE:-0}
quiet=${FETCH_ELFDEPS_QUIET:-0}
verbose=${FETCH_ELFDEPS_VERBOSE:-0}
debug=${FETCH_ELFDEPS_DEBUG:-0}

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
        -D|--dest|-p|--packages|-m|--mirror) takes=1 ;;
    esac
    if [ "$takes" = 1 ] && [ -z "$val" ]; then
        [ $# -gt 0 ] || usage_error "$opt wants a value"
        val=$1; shift
    fi
    case $opt in
        -D|--dest) dest=$val ;;
        -p|--packages) packages=$val ;;
        -m|--mirror) mirror=$val ;;
        -k|--keep-rpm) keeprpm=1 ;;
        --no-keep-rpm) keeprpm=0 ;;
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
command -v python3 >/dev/null 2>&1 || die "python3 is not on PATH"
command -v curl >/dev/null 2>&1 || die "curl is not on PATH"

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        python3 -c 'import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
    fi
}

mkdir -p "$dest/rpm" || die "cannot create $dest/rpm"
rm -rf "$dest/sysroot" "$dest/ref" || die "cannot clear the derived trees"
mkdir -p "$dest/sysroot" "$dest/ref" || die "cannot create the derived trees"

fetched=0
reused=0
unpacked=0

while IFS="$(printf '\t')" read -r role repo location want; do
    case $role in ''|\#*) continue ;; esac
    file=$(basename "$location")
    path=$dest/rpm/$file
    if [ -f "$path" ] && [ "$(sha256 "$path")" = "$want" ]; then
        reused=$((reused + 1))
        chat "have $file"
    else
        rm -f "$path"
        chat "fetch $file"
        # -o wants a path curl's own runtime understands: a Windows curl (with
        # no Cygwin curl beside it) rejects a POSIX -o path, so give it the
        # Windows form via cygpath where that exists, the POSIX path where it
        # does not. -o keeps curl's clean per-retry truncation a redirect loses.
        o=$path; command -v cygpath >/dev/null 2>&1 && o=$(cygpath -w "$path")
        curl -fsS --retry 3 --retry-delay 2 -o "$o" \
            "$mirror/$repo/$location" || die "cannot fetch $file"
        got=$(sha256 "$path")
        [ "$got" = "$want" ] || die "checksum mismatch on $file: $got"
        fetched=$((fetched + 1))
    fi
    case $role in
        sysroot) into=$dest/sysroot ;;
        ref) into=$dest/ref ;;
        *) die "unknown role $role for $file" ;;
    esac
    python3 "$here/rpmx.py" "$path" "$into" >/dev/null || die "cannot unpack $file"
    unpacked=$((unpacked + 1))
    [ "$keeprpm" = 1 ] || rm -f "$path"
done < "$packages"

elfdeps=$dest/sysroot/usr/lib/rpm/elfdeps
attrs=$(ls "$dest/sysroot/usr/lib/rpm/fileattrs" 2>/dev/null | wc -l)
libc=$dest/ref/usr/lib64/libc.so.6
[ -x "$elfdeps" ] || die "the el8 set did not carry /usr/lib/rpm/elfdeps"
[ -e "$libc" ] || die "the el8 glibc did not carry /usr/lib64/libc.so.6"

libdirs=$dest/sysroot/usr/lib64
LD_LIBRARY_PATH=$libdirs "$elfdeps" --help >/dev/null 2>&1
rc=$?
[ "$rc" -le 2 ] || die "el8 elfdeps will not run here: exit $rc"

printf 'dest=%s\n' "$dest"
printf 'mirror=%s\n' "$mirror"
printf 'fetched=%d\n' "$fetched"
printf 'reused=%d\n' "$reused"
printf 'unpacked=%d\n' "$unpacked"
printf 'elfdeps=%s\n' "$elfdeps"
printf 'elfdeps_package=%s\n' \
    "$(awk -F'\t' '/rpm-build-4/ { n = split($3, p, "/"); sub(/\.rpm$/, "", p[n]); print p[n]; exit }' "$packages")"
printf 'fileattrs=%d\n' "$attrs"
printf 'vendor_libc=%s\n' "$libc"
printf 'ld_library_path=%s\n' "$libdirs"

[ "$terse" = 1 ] || note "sysroot ready under $dest/sysroot"
exit 0
