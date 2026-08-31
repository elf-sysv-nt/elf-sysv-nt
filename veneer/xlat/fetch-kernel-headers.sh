#!/usr/bin/env bash
#
# WP-55: fetch and unpack the el8 kernel headers the Linux-side probes
# compile against.  glibc's headers defer to <linux/*> and <asm/*> for
# errno values and flag bits, so the extraction needs the kernel-headers
# package beside the vendored glibc set.  Per DR-0002 it is fetched and
# checksum-pinned, never vendored: the unpacked tree lands under
# gitignored a/vendor/xlat and is reproducible from the pin below.
#
# Usage:
#   fetch-kernel-headers.sh [-D DIR]
#
# Options:
#   -D DIR   Archive and unpacked tree land here.
#            [default: <repo-root>/a/vendor/xlat]
#
# Prints the include directory path on success.
# Exit codes: 0 success, 1 failure, 2 usage error.

set -u

here=$(cd "$(dirname "$0")" && pwd)

REF_NVRA=kernel-headers-4.18.0-553.el8_10.x86_64
REF_URL=https://dl.rockylinux.org/pub/rocky/8.10/BaseOS/x86_64/os/Packages/k/$REF_NVRA.rpm
REF_RPM_SHA256=63c006d4afd10c77e0505b01bbef7cd1aa7f4250dac9abc84edc4c7ba71445f0

# One cache for every worktree: gitignored a/ beside the main .git.
common=$(cd "$here" && git rev-parse --git-common-dir 2>/dev/null) || common=
case $common in /*) ;; *) common=$here/$common ;; esac
if [ -n "$common" ]; then
  dest=$(cd "$common/.." && pwd)/a/vendor/xlat
else
  dest=$here/../../a/vendor/xlat
fi
while getopts D: opt; do
  case $opt in
    D) dest=$OPTARG ;;
    *) echo "usage: fetch-kernel-headers.sh [-D DIR]" >&2; exit 2 ;;
  esac
done

fail() { echo "fetch-kernel-headers: $*" >&2; exit 1; }

mkdir -p "$dest"
rpm=$dest/$REF_NVRA.rpm
if [ ! -f "$rpm" ]; then
  echo "fetching $REF_NVRA.rpm" >&2
  curl -fsSL -o "$rpm" "$REF_URL" || fail "fetch failed and no cached copy"
fi
got=$(sha256sum "$rpm" | cut -d' ' -f1)
[ "$got" = "$REF_RPM_SHA256" ] || fail "sha256 mismatch on $rpm: $got"

inc=$dest/kernel-headers/usr/include
if [ ! -d "$inc/linux" ]; then
  rm -rf "$dest/kernel-headers"
  python3 "$here/../version-map/rpmx.py" "$rpm" "$dest/kernel-headers" \
    usr/include || fail "unpack failed"
fi
[ -f "$inc/linux/errno.h" ] || fail "unpacked tree lacks linux/errno.h"
echo "$inc"
