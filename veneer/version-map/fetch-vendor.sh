#!/usr/bin/env bash
#
# WP-51: fetch and unpack the el8 glibc binaries the version map is built from.
#
# Reads packages.tsv, fetches each pinned RPM from the Rocky 8.10 mirror,
# verifies it by sha256, and unpacks it under the destination with rpmx.py --
# no rpm, rpm2cpio or cpio wanted, since a host that had them would probably be
# el8 already. Per DR-0002 none of this is vendored: the tree it writes is
# regenerable and lives outside the repository. A checksum mismatch stops the
# run rather than extracting a library the map was not measured against.
#
# The destination is reseeded from the archives on every run: each package's
# tree is cleared and rebuilt, so a package dropped from packages.tsv leaves
# nothing behind. The archives themselves are kept and reused while their
# checksum still matches, since they are the slow part and cannot change under
# a pin.
#
# Usage:
#   fetch-vendor.sh [options]
#
# Options:
#   -D DIR, --dest=DIR       Archives and unpacked trees land here. Required.
#   -p FILE, --packages=FILE The pin. [default: beside this script]
#   -m URL, --mirror=URL     Repository root.
#                            [default: https://dl.rockylinux.org/pub/rocky/8.10]
#   -q, --quiet              Errors only.
#   -v, --verbose            Name every package as it is handled.
#   -V, --version            Print the version and exit.
#   -h, --help               Print this message and exit.
#
# Each option is also settable as FETCH_VENDOR_<OPTION>, and the option wins.
# Exit: 0 success, 1 failure, 2 usage error.

set -u

prog=fetch-vendor
release='fetch-vendor 1.0'
here=$(cd "$(dirname "$0")" && pwd)

dest=${FETCH_VENDOR_DEST:-}
packages=${FETCH_VENDOR_PACKAGES:-$here/packages.tsv}
mirror=${FETCH_VENDOR_MIRROR:-https://dl.rockylinux.org/pub/rocky/8.10}
quiet=${FETCH_VENDOR_QUIET:-0}
verbose=${FETCH_VENDOR_VERBOSE:-0}

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
	case $opt in -D|--dest|-p|--packages|-m|--mirror) takes=1 ;; esac
	if [ "$takes" = 1 ] && [ -z "$val" ]; then
		[ $# -gt 0 ] || usage_error "$opt wants a value"
		val=$1; shift
	fi
	case $opt in
		-D|--dest) dest=$val ;;
		-p|--packages) packages=$val ;;
		-m|--mirror) mirror=$val ;;
		-q|--quiet) quiet=1 ;;
		-v|--verbose) verbose=1 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-h|--help) usage; exit 0 ;;
		--) break ;;
		*) usage_error "unknown option $opt" ;;
	esac
done

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

fetched=0
reused=0
unpacked=0

while IFS="$(printf '\t')" read -r pkg repo location want; do
	case $pkg in ''|\#*) continue ;; esac
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
	rm -rf "$dest/$pkg" || die "cannot clear $dest/$pkg"
	mkdir -p "$dest/$pkg" || die "cannot create $dest/$pkg"
	python3 "$here/rpmx.py" "$path" "$dest/$pkg" >/dev/null \
		|| die "cannot unpack $file"
	unpacked=$((unpacked + 1))
done < "$packages"

printf 'dest=%s\n' "$dest"
printf 'fetched=%d\n' "$fetched"
printf 'reused=%d\n' "$reused"
printf 'unpacked=%d\n' "$unpacked"
[ "$quiet" = 1 ] || note "vendor tree ready under $dest"
exit 0
