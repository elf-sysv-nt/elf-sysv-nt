#!/usr/bin/env bash
#
# Is the non-PIE bzip2 that WP-56 parked on el8's artifact or the harness's?
#
# The acceptance harness builds bzip2 with `make CC=%CC% bzip2`, the naked
# Makefile, and gets an ET_EXEC whose first PT_LOAD wants 0x400000. The
# question is whether that is what el8 ships. It is not, and this measures
# the gap three ways rather than arguing it: what Red Hat's own binary rpm
# contains, what the spec and the rpm macros ask for, and what the project's
# cross toolchain produces from the same source under each flag set.
#
# The archives are kept between runs and reused when the pin still matches;
# the unpacked trees and both build trees are cleared and rebuilt every run.
#
# Usage:
#   measure-vendor-build.sh [options]
#
# Options:
#   -D DIR, --dest=DIR       Archives, unpacked trees and builds land here.
#   -p FILE, --packages=FILE The pin. [default: beside this one]
#   -m URL, --mirror=URL     Repository root.
#                            [default: https://dl.rockylinux.org/pub/rocky/8.10]
#   -x PATH, --rpmx=PATH     The unpacker.
#                            [default: ../versioned-libc/rpmx.py]
#   -c PATH, --cross=PATH    The cross compiler.
#                            [default: the target gcc under /c/-/x-elfsysvnt/bin]
#   -q, --quiet              Errors only.
#   -v, --verbose            Name every step as it is taken.
#   -d, --debug              Trace execution; implies --verbose.
#   -V, --version            Print the version and exit.
#   -h, --help               Print this message and exit.
#
# Each option is also settable as MEASURE_VENDOR_BUILD_<OPTION>, and the
# option wins over the variable.
#
# Exit codes: 0 success, 1 failure, 2 usage error.

set -u

prog=measure-vendor-build
release='measure-vendor-build 1.0'

here=$(cd "$(dirname "$0")" && pwd)
xbin=/c/-/x-elfsysvnt/bin/x86_64-elfsysvnt-linux-gnu

dest=${MEASURE_VENDOR_BUILD_DEST:-}
packages=${MEASURE_VENDOR_BUILD_PACKAGES:-$here/packages.tsv}
mirror=${MEASURE_VENDOR_BUILD_MIRROR:-https://dl.rockylinux.org/pub/rocky/8.10}
rpmx=${MEASURE_VENDOR_BUILD_RPMX:-$here/../versioned-libc/rpmx.py}
cross=${MEASURE_VENDOR_BUILD_CROSS:-$xbin-gcc}
quiet=${MEASURE_VENDOR_BUILD_QUIET:-0}
verbose=${MEASURE_VENDOR_BUILD_VERBOSE:-0}
debug=${MEASURE_VENDOR_BUILD_DEBUG:-0}

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
        -D|--dest|-p|--packages|-m|--mirror|-x|--rpmx|-c|--cross) takes=1 ;;
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
        -c|--cross) cross=$val ;;
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
command -v "$cross" >/dev/null 2>&1 || die "no cross compiler at $cross"

readelf=${cross%-gcc}-readelf
command -v "$readelf" >/dev/null 2>&1 || readelf=readelf
command -v "$readelf" >/dev/null 2>&1 || die "no readelf beside $cross or on PATH"

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        python3 -c 'import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"
    fi
}

mkdir -p "$dest/rpm" || die "cannot create $dest/rpm"
rm -rf "$dest/ref" "$dest/naked" "$dest/vendor" || die "cannot clear the work trees"
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
        # A Windows curl with no Cygwin curl beside it rejects a POSIX -o path,
        # so hand it the Windows form where cygpath exists. -o rather than a
        # redirect keeps curl's clean per-retry truncation.
        o=$path; command -v cygpath >/dev/null 2>&1 && o=$(cygpath -w "$path")
        curl -fsS --retry 3 --retry-delay 2 -o "$o" \
            "$mirror/$repo/$location" || die "cannot fetch $file"
        got=$(sha256 "$path")
        [ "$got" = "$want" ] || die "checksum mismatch on $file: $got"
        fetched=$((fetched + 1))
    fi
    python3 "$rpmx" "$path" "$dest/ref" >/dev/null || die "cannot unpack $file"
done < "$packages"

vendor_bin=$dest/ref/usr/bin/bzip2
[ -f "$vendor_bin" ] || die "the binary rpm carried no /usr/bin/bzip2"
spec=$dest/ref/bzip2.spec
[ -f "$spec" ] || die "the source rpm carried no bzip2.spec"
macros=$dest/ref/usr/lib/rpm/redhat/macros
[ -f "$macros" ] || die "redhat-rpm-config carried no macros file"

# e_type as one word: EXEC or DYN. readelf spells a PIE two ways depending on
# its vintage ("DYN (Shared object file)" before binutils 2.35 or so, "DYN
# (Position-Independent Executable file)" after), and the parenthetical is
# therefore provenance, not a finding. Take the bare type.
etype() { "$readelf" -h "$1" | sed -n 's/^  Type: *\([A-Z]*\).*/\1/p'; }

# Where the image asks to be placed: "zero" for a relocatable image whose
# first PT_LOAD is zero-based, the literal address otherwise. The address is
# the finding here, unusually -- 0x400000 is exactly the contended window --
# so it is stated as a word the rerun must reproduce.
loadbase() {
    local v
    v=$("$readelf" -lW "$1" | awk '/^  LOAD/ { print $3; exit }')
    case $v in
        0x0000000000000000) printf 'zero\n' ;;
        *) printf '0x%x\n' "$((v))" ;;
    esac
}

# DR-0061's requirement, checked rather than assumed: no two PT_LOADs of
# unlike protection may share a 0x10000 host allocation granule. Reported as
# yes or no, never as the segment table, which moves with every rebuild.
granule=$((0x10000))
separable() {
    local prevflags= prevend=-1 flags line vaddr memsz start end
    "$readelf" -lW "$1" | grep '^  LOAD' | while read -r line; do
        set -- $line
        vaddr=$3; memsz=$6
        flags=$(printf '%s' "$line" | sed 's/^.*0x[0-9a-f]* *\([RWE ]*[RWE]\) *0x[0-9a-f]*$/\1/' | tr -d ' ')
        start=$((vaddr)); end=$((start + memsz - 1))
        if [ -n "$prevflags" ] && [ "$flags" != "$prevflags" ]; then
            if [ $((prevend / granule)) -ge $((start / granule)) ]; then
                printf 'no\n'; return 0
            fi
        fi
        prevflags=$flags; prevend=$end
    done | grep -q no && printf 'no\n' || printf 'yes\n'
}

# The two cross builds, from the same unpacked source, differing only in the
# flags the harness hands make. `naked` is acceptance/packages.tsv's current
# build line; `vendor` is what el8's macro chain expands to, with the two
# -specs= arguments replaced by the flags those specs files inject, because
# the specs files are Red Hat's and do not exist in this sysroot.
vendor_cflags="-O2 -g -pipe -Wall -Werror=format-security -Wp,-D_FORTIFY_SOURCE=2"
vendor_cflags="$vendor_cflags -fexceptions -fstack-protector-strong -grecord-gcc-switches"
vendor_cflags="$vendor_cflags -m64 -mtune=generic -fasynchronous-unwind-tables"
vendor_cflags="$vendor_cflags -fstack-clash-protection -fPIE -D_FILE_OFFSET_BITS=64"
vendor_ldflags="-Wl,-z,relro -Wl,-z,now -pie"

tarball=$(ls "$dest"/ref/bzip2-*.tar.gz 2>/dev/null | head -1)
[ -n "$tarball" ] || die "the source rpm carried no bzip2 tarball"

unpack_into() {
    local into=$1
    rm -rf "$into"; mkdir -p "$into"
    tar -C "$into" --strip-components=1 -xf "$tarball" || die "cannot untar into $into"
}

unpack_into "$dest/naked"
chat "building naked"
( cd "$dest/naked" && make CC="$cross" bzip2 ) > "$dest/naked/build.log" 2>&1 \
    || die "the naked build failed; see $dest/naked/build.log"

unpack_into "$dest/vendor"
chat "building under the el8-effective flags"
( cd "$dest/vendor" && make CC="$cross" CFLAGS="$vendor_cflags" \
    LDFLAGS="$vendor_ldflags" bzip2 ) > "$dest/vendor/build.log" 2>&1 \
    || die "the vendor-flags build failed; see $dest/vendor/build.log"

# A yes/no on whether a file contains a string, so the macro chain is reported
# as findings rather than as pasted vendor text that a repackage could reflow.
# -- before the pattern, because two of the patterns are flags (-fPIE, -pie)
# and grep otherwise reads them as its own options.
carries() { grep -qF -e "$2" -- "$1" && printf 'yes\n' || printf 'no\n'; }

cat <<'HEADER'
# Is the non-PIE bzip2 WP-56 parked on el8's artifact or the harness's? Three
# readings: what Red Hat shipped, what the macro chain asks for, and what this
# project's cross toolchain produces from the same source under each flag set.
#
# Generated by measure-vendor-build 1.0. Rerunning it regenerates this file.

HEADER

printf 'script  measure-vendor-build 1.0\n'
printf 'run_date  %s\n' "$(date +%Y-%m-%d)"
printf 'mirror  %s\n' "$mirror"
printf 'compiler  %s\n' "$("$cross" --version | head -1)"
printf 'readelf  %s\n' "$("$readelf" --version | head -1)"
printf 'fetched  %d\n' "$fetched"
printf 'reused  %d\n' "$reused"

v_type=$(etype "$vendor_bin")
n_type=$(etype "$dest/naked/bzip2")
f_type=$(etype "$dest/vendor/bzip2")

printf '\n## What Red Hat shipped\n\n'
printf 'vendor_e_type=%s\n' "$v_type"
printf 'vendor_load_base=%s\n' "$(loadbase "$vendor_bin")"
printf 'vendor_granule_separable=%s\n' "$(separable "$vendor_bin")"

printf '\n## The macro chain behind it\n\n'
printf 'spec_build_uses_rpm_opt_flags=%s\n' "$(carries "$spec" 'CFLAGS="$RPM_OPT_FLAGS')"
printf 'spec_build_uses_global_ldflags=%s\n' "$(carries "$spec" 'LDFLAGS="%{__global_ldflags}"')"
printf 'optflags_pulls_hardened_cc1=%s\n' "$(carries "$macros" 'redhat-hardened-cc1')"
printf 'ldflags_pulls_hardened_ld=%s\n' "$(carries "$macros" 'redhat-hardened-ld')"
printf 'hardened_build_defaults_on=%s\n' \
    "$(grep -qE '^%_hardened_build[[:space:]]+1' "$macros" && echo yes || echo no)"
printf 'hardened_cc1_injects_fpie=%s\n' \
    "$(carries "$dest/ref/usr/lib/rpm/redhat/redhat-hardened-cc1" '-fPIE')"
printf 'hardened_ld_injects_pie=%s\n' \
    "$(carries "$dest/ref/usr/lib/rpm/redhat/redhat-hardened-ld" '-pie')"

printf '\n## What the harness builds from the same source\n\n'
printf 'naked_e_type=%s\n' "$n_type"
printf 'naked_load_base=%s\n' "$(loadbase "$dest/naked/bzip2")"
printf 'vendor_flags_e_type=%s\n' "$f_type"
printf 'vendor_flags_load_base=%s\n' "$(loadbase "$dest/vendor/bzip2")"
printf 'vendor_flags_granule_separable=%s\n' "$(separable "$dest/vendor/bzip2")"

# The other half of the same question. Building bzip2 the vendor's way settles
# one package; whether the macro set this project ships would build any package
# the vendor's way is a different claim, and toolchain/rpm/README.md had it
# down as written from documentation and never checked. The chain is already
# unpacked here, so the check costs nothing extra.
printf '\n## The macro set we ship, against the vendor'"'"'s\n\n'
python3 "$here/expand-flags.py" --vendor-root "$dest/ref" \
	--ours "$here/../../toolchain/rpm/macros.elfsysvnt" \
	|| die "the flag expansion failed"

# The verdict is derived, not asserted. It holds only while el8 ships a PIE,
# the naked line produces an ET_EXEC, and the vendor-effective line reproduces
# the vendor's shape; if Red Hat ever shipped an ET_EXEC bzip2 this flips and
# the reframe that rests on it dies with it.
printf '\n## Verdict\n\n'
if [ "$v_type" = DYN ] && [ "$n_type" = EXEC ] && [ "$f_type" = DYN ]; then
    printf 'verdict=the-non-pie-image-is-the-harness-artifact\n'
else
    printf 'verdict=refuted (vendor=%s naked=%s vendor_flags=%s)\n' \
        "$v_type" "$n_type" "$f_type"
fi

note "measured the vendor binary and two cross builds under $dest"
exit 0
