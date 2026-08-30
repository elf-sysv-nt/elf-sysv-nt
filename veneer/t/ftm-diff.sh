#!/usr/bin/env bash
# ftm-diff -- certify the veneer's <features.h> against el8's glibc 2.28.
#
# The exit criterion for WP-50 is that a package probing the headers gets one
# answer, not two.  The half of that which can be checked before there is a
# library to link is this: the veneer's <features.h> must resolve the __USE_*
# family -- the macros every other header branches on -- to exactly what el8's
# own <features.h> resolves them to, for the same input feature-test macros.
#
# So this compiles a probe that reports every relevant macro, preprocesses it
# twice over a matrix of -D/-std inputs, and diffs the two reports.  The two
# runs share one include directory in all but one file: sys/cdefs.h,
# gnu/stubs.h and stdc-predef.h are the veneer's in both, and only
# <features.h> is swapped between the veneer's and the vendor's.  That isolates
# the thing under test -- the feature-test arithmetic -- from everything else,
# and it is why the diff being empty means the arithmetic matches rather than
# that two whole header trees happen to agree.
#
# The vendor reference is not vendored into the repository (DR-0002).  It is
# fetched from the Rocky 8.10 vault at a pinned version and verified by
# sha256, then cached under a gitignored directory.  Offline with no cache and
# no local rpm, the test reports the pin and skips rather than inventing an
# answer.
set -u

here=$(cd "$(dirname "$0")" && pwd)
veneer_inc=$(cd "$here/.." && pwd)/include
cache=$here/ref-cache
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# --- the pin --------------------------------------------------------------
REF_NVRA=glibc-headers-2.28-251.el8_10.40.x86_64
REF_URL=https://dl.rockylinux.org/pub/rocky/8.10/BaseOS/x86_64/os/Packages/g/$REF_NVRA.rpm
REF_RPM_SHA256=137c4b4bb4074fca3c51bb64f81b193557efd5f0e4aab9dafa88049c136b78da
REF_FEATURES_SHA256=7929e49415bca262a6bdf905a589b350943ab482378bcd7e4804a0ce6cd52baf

say()  { printf '%s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
skip() { printf 'SKIP: %s\n' "$*" >&2; exit 77; }

# --- the cross compiler ---------------------------------------------------
if [ -d "$HOME/x-elfsysvnt/bin" ]; then
	PATH="$HOME/x-elfsysvnt/bin:$PATH"
fi
GCC=${GCC:-x86_64-elfsysvnt-linux-gnu-gcc}
command -v "$GCC" >/dev/null 2>&1 || skip "cross gcc '$GCC' not on PATH"

# --- obtain the vendor <features.h> --------------------------------------
# Order: an explicit local rpm, then the cache, then a network fetch.  Each
# route ends with the same sha256 gate, so provenance is identical whichever
# was taken.
ref_features=$cache/features.h
extract_from_rpm() {
	# $1 = path to the glibc-headers rpm
	local rpm=$1 d=$work/rpm
	command -v rpm2cpio >/dev/null 2>&1 || return 1
	command -v cpio     >/dev/null 2>&1 || return 1
	mkdir -p "$d"
	( cd "$d" && rpm2cpio "$rpm" | cpio -idm 2>/dev/null )
	[ -f "$d/usr/include/features.h" ] || return 1
	mkdir -p "$cache"
	cp "$d/usr/include/features.h" "$ref_features"
}

got=
if [ -n "${EL8_GLIBC_HEADERS_RPM:-}" ] && [ -f "$EL8_GLIBC_HEADERS_RPM" ]; then
	if [ "$(sha256sum "$EL8_GLIBC_HEADERS_RPM" | cut -d' ' -f1)" = "$REF_RPM_SHA256" ]; then
		extract_from_rpm "$EL8_GLIBC_HEADERS_RPM" && got=local-rpm
	else
		say "note: EL8_GLIBC_HEADERS_RPM sha256 does not match the pin; ignoring it"
	fi
fi

if [ -z "$got" ] && [ -f "$ref_features" ] \
   && [ "$(sha256sum "$ref_features" | cut -d' ' -f1)" = "$REF_FEATURES_SHA256" ]; then
	got=cache
fi

if [ -z "$got" ]; then
	if command -v curl >/dev/null 2>&1; then
		say "fetching $REF_NVRA from the Rocky 8.10 vault"
		if curl -fsS --max-time 300 -o "$work/ref.rpm" "$REF_URL"; then
			if [ "$(sha256sum "$work/ref.rpm" | cut -d' ' -f1)" = "$REF_RPM_SHA256" ]; then
				extract_from_rpm "$work/ref.rpm" && got=fetch
			else
				fail "fetched rpm sha256 does not match the pin $REF_RPM_SHA256"
			fi
		fi
	fi
fi

[ -n "$got" ] || skip "no vendor reference (offline, no cache, no local rpm); pin is $REF_NVRA"
[ "$(sha256sum "$ref_features" | cut -d' ' -f1)" = "$REF_FEATURES_SHA256" ] \
	|| fail "reference features.h sha256 does not match the pin"
say "reference: $REF_NVRA  (features.h via $got)"

# --- two include trees, differing only in features.h ----------------------
ven=$work/ven
ref=$work/ref
cp -a "$veneer_inc" "$ven"
cp -a "$veneer_inc" "$ref"
cp "$ref_features" "$ref/features.h"

# --- the probe ------------------------------------------------------------
# Each macro is reported on a RESULT line, guarded so an undefined macro
# produces no line at all.  Q() stringizes the name without expanding it, so
# the label survives even when the macro expands to a value on the right.
macros="
__GLIBC__ __GLIBC_MINOR__ __GNU_LIBRARY__ __GLIBC_USE_DEPRECATED_GETS
_DEFAULT_SOURCE _POSIX_SOURCE _POSIX_C_SOURCE _XOPEN_SOURCE _XOPEN_SOURCE_EXTENDED
_LARGEFILE_SOURCE _LARGEFILE64_SOURCE _ATFILE_SOURCE
_ISOC95_SOURCE _ISOC99_SOURCE _ISOC11_SOURCE
__USE_ISOC11 __USE_ISOC99 __USE_ISOC95 __USE_ISOCXX11
__USE_POSIX __USE_POSIX2 __USE_POSIX199309 __USE_POSIX199506 __USE_POSIX_IMPLICITLY
__USE_XOPEN __USE_XOPEN_EXTENDED __USE_UNIX98
__USE_XOPEN2K __USE_XOPEN2KXSI __USE_XOPEN2K8 __USE_XOPEN2K8XSI
__USE_LARGEFILE __USE_LARGEFILE64 __USE_FILE_OFFSET64
__USE_MISC __USE_ATFILE __USE_GNU __USE_FORTIFY_LEVEL __USE_EXTERN_INLINES
__KERNEL_STRICT_NAMES
"
{
	printf '#include <features.h>\n'
	printf '#define Q(x) #x\n'
	for m in $macros; do
		printf '#ifdef %s\n' "$m"
		printf 'RESULT Q(%s) = %s\n' "$m" "$m"
		printf '#endif\n'
	done
} > "$work/probe.in"

report() {  # report <include-dir> <lang> <flags...>
	local dir=$1 lang=$2; shift 2
	"$GCC" "$@" -E -P -nostdinc -I "$dir" -x "$lang" "$work/probe.in" 2>/dev/null \
		| sed -n 's/^RESULT //p' | sort
}

# --- the matrix -----------------------------------------------------------
# Each case is "lang | flags".  lang is c or c++; flags is what the compiler
# and the user would set together.  The set covers every branch the feature
# arithmetic can take: the default, strict-ANSI at each C standard, each POSIX
# and X/Open level, the ISO C source macros, the large-file and at-file
# switches, the reentrancy synonyms, the deprecated BSD/SVID aliases, the
# fortify levels under optimisation, C++ at three standards, and a few
# accumulations that exercise the precedence rules.
cases='
c   |
c   | -ansi
c   | -std=c99
c   | -std=c11
c   | -std=c17
c   | -std=gnu99
c   | -std=gnu11
c   | -std=gnu17
c   | -D_GNU_SOURCE
c   | -D_DEFAULT_SOURCE
c   | -D_POSIX_SOURCE
c   | -D_POSIX_C_SOURCE=1
c   | -D_POSIX_C_SOURCE=2
c   | -D_POSIX_C_SOURCE=199309L
c   | -D_POSIX_C_SOURCE=199506L
c   | -D_POSIX_C_SOURCE=200112L
c   | -D_POSIX_C_SOURCE=200809L
c   | -D_XOPEN_SOURCE
c   | -D_XOPEN_SOURCE=500
c   | -D_XOPEN_SOURCE=600
c   | -D_XOPEN_SOURCE=700
c   | -D_XOPEN_SOURCE=600 -D_XOPEN_SOURCE_EXTENDED
c   | -D_ISOC99_SOURCE
c   | -D_ISOC11_SOURCE
c   | -D_LARGEFILE_SOURCE
c   | -D_LARGEFILE64_SOURCE
c   | -D_FILE_OFFSET_BITS=64
c   | -D_ATFILE_SOURCE
c   | -D_REENTRANT
c   | -D_THREAD_SAFE
c   | -D_BSD_SOURCE
c   | -D_SVID_SOURCE
c   | -ansi -D_POSIX_C_SOURCE=200112L
c   | -std=c11 -D_XOPEN_SOURCE=700
c   | -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64
c   | -O2
c   | -O2 -D_FORTIFY_SOURCE=1
c   | -O2 -D_FORTIFY_SOURCE=2
c   | -O2 -D_FORTIFY_SOURCE=3 -D_GNU_SOURCE
c++ | -std=c++98
c++ | -std=c++11
c++ | -std=c++17
c++ | -std=gnu++17 -D_GNU_SOURCE
'

# --- run it ---------------------------------------------------------------
pass=0 diffs=0 total=0
IFS='
'
for line in $cases; do
	case $line in ''|'#'*) continue ;; esac
	lang=${line%%|*}; lang=$(printf '%s' "$lang" | tr -d ' ')
	flags=${line#*|}
	# word-split flags on spaces deliberately
	unset IFS
	set -- $flags
	total=$((total+1))
	label="[$lang] ${flags:-<none>}"
	v=$(report "$ven" "$lang" "$@")
	r=$(report "$ref" "$lang" "$@")
	if [ "$v" = "$r" ]; then
		pass=$((pass+1))
		# say "ok    $label"
	else
		diffs=$((diffs+1))
		say "DIFF  $label"
		diff <(printf '%s\n' "$r") <(printf '%s\n' "$v") \
			| sed 's/^/        /'
	fi
	IFS='
'
done
unset IFS

# --- identity assertion, independent of the diff --------------------------
id=$(report "$ven" c)
echo "$id" | grep -qx '"__GLIBC__" = 2'        || fail "veneer __GLIBC__ is not 2"
echo "$id" | grep -qx '"__GLIBC_MINOR__" = 28' || fail "veneer __GLIBC_MINOR__ is not 28"
echo "$id" | grep -qx '"__GNU_LIBRARY__" = 6'  || fail "veneer __GNU_LIBRARY__ is not 6"

say ""
say "cases: $total   matched: $pass   diverged: $diffs"
if [ "$diffs" -ne 0 ]; then
	fail "$diffs of $total input sets resolve __USE_* differently from el8"
fi
say "PASS: __GLIBC__=2 __GLIBC_MINOR__=28, and all $total input sets resolve the"
say "      __USE_* family exactly as $REF_NVRA does."
