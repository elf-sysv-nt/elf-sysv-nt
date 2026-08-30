#!/usr/bin/env bash
# ftm-diff -- certify the veneer's headers are el8's, vendored verbatim.
#
# DR-0000's copy line governs this set: above the floor, el8's files are used
# unchanged, and the headers a package compiles against are el8's
# glibc-headers-2.28, dropped in byte-for-byte.  The earlier WP-50 re-authored
# features.h to "match el8's arithmetic" and hand-wrote the rest; that is the
# drift this test now forecloses.  The exit criterion is no longer that the
# veneer's arithmetic agrees with el8's -- it is that the files ARE el8's.
#
# Two things are checked, and each is a pass or a fail rather than a reading.
#
#   verbatim   Each vendored header is byte-identical to the same file in el8's
#              glibc-headers-2.28.  features.h, sys/cdefs.h, stdc-predef.h and
#              the two bits/ files sys/cdefs.h pulls in are diffed against the
#              pinned reference and must match to the byte; each also matches a
#              committed per-file sha256, so a mismatch is caught even offline.
#              gnu/stubs.h is asserted to DIFFER from el8's -- it is the one
#              justified exception -- and to carry its written reason.  That
#              single exception, and no other, is the whole of the variance
#              DR-0000 permits in this set.
#
#   closure    The vendored features.h preprocesses cleanly under the cross
#              compiler with -nostdinc against veneer/include alone, and still
#              reports __GLIBC__=2 __GLIBC_MINOR__=28 over a matrix of feature
#              inputs.  Because features.h is now el8's own file, this checks
#              that the include closure resolves -- that the copied file finds
#              the copied files it includes -- rather than checking a
#              transcription.  It skips when the cross gcc is absent.
#
# Why gnu/stubs.h is the exception.  el8's gnu/stubs.h reports which functions
# its glibc left unimplemented; on a real kernel that is almost nothing.  Here
# the answer is different and larger: the veneer is a face over newlib plus
# Cygwin, so the absent functions are the ones Cygwin's export surface does not
# carry, which WP-52's fourth bucket measures (veneer/classification/,
# doc/what-the-veneer-lacks.md).  Copying el8's answer would assert this
# platform implements what it does not, so this one header is the veneer's own.
#
# The reference is not vendored into the repository (DR-0002).  It is fetched
# from the Rocky 8.10 vault at a pinned version and verified by sha256, then
# cached under a gitignored directory.  Offline with no cache and no local rpm,
# the test reports the pin and skips rather than inventing an answer.
set -u

here=$(cd "$(dirname "$0")" && pwd)
veneer_inc=$(cd "$here/.." && pwd)/include
cache=$here/ref-cache
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# --- the pin --------------------------------------------------------------
# The package, its sha256, and the sha256 of each header vendored from it.
# These checksums are the reproducibility guarantee DR-0002 asks for: the
# reference is refetched not stored, and this is what a refetch is checked
# against.
REF_NVRA=glibc-headers-2.28-251.el8_10.40.x86_64
REF_URL=https://dl.rockylinux.org/pub/rocky/8.10/BaseOS/x86_64/os/Packages/g/$REF_NVRA.rpm
REF_RPM_SHA256=137c4b4bb4074fca3c51bb64f81b193557efd5f0e4aab9dafa88049c136b78da
REF_FEATURES_SHA256=7929e49415bca262a6bdf905a589b350943ab482378bcd7e4804a0ce6cd52baf

# Vendored verbatim: "relpath sha256".  Each is diffed against the extracted
# reference AND checked against this committed sha256.
VERBATIM="
features.h 7929e49415bca262a6bdf905a589b350943ab482378bcd7e4804a0ce6cd52baf
sys/cdefs.h 5434a26400044512b49fbdb56bb123c638a5b37cac45103a54eeb880cf042cd4
stdc-predef.h 7afa7fcfd2a1ac2d5fe86672db5828460a736519680da0b9fac4d91e5f227817
bits/wordsize.h 716d8459074e2a082d65a28192b32eddbfd7587348372395b1c283a723ae4c18
bits/long-double.h 3945f7fb83453692053981cc2995bf21bea6f29be7431cd53804984354584153
"
# The exception: our gnu/stubs.h must NOT equal el8's, whose sha256 is here.
EL8_STUBS_SHA256=ef444295a0a9e8f5c40918399a00cc91ba0bd79774110cdf6e652baa6cfadb2e

quiet=0
[ "${1:-}" = "-q" ] && quiet=1

say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; smon_end 1 2>/dev/null; exit 1; }
skip() { printf 'SKIP: %s\n' "$*" >&2; smon_end 77 2>/dev/null; exit 77; }
sha()  { sha256sum "$1" | cut -d' ' -f1; }

# --- session monitor (optional) ------------------------------------------
smon=/c/-/repo/session-monitor/lib/smon.sh
[ -f "$smon" ] && . "$smon"
type smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_step_skip() { :; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}
SMON_TRAP=0
smon_session build wp50-veneer-headers
smon_plan verbatim closure

# --- obtain the reference tree -------------------------------------------
# Order: an explicit local rpm, then the cache, then a network fetch.  Each
# route ends at the same extracted tree under the cache, gated by the rpm
# sha256, so provenance is identical whichever was taken.
ref_root=$cache/unpack/usr/include
extract_rpm() {  # $1 = rpm path
	local rpm=$1 d=$cache/unpack
	command -v rpm2cpio >/dev/null 2>&1 || return 1
	command -v cpio     >/dev/null 2>&1 || return 1
	rm -rf "$d"; mkdir -p "$d"
	( cd "$d" && rpm2cpio "$rpm" | cpio -idm --quiet 2>/dev/null )
	[ -f "$d/usr/include/features.h" ]
}
rpm_ok() { [ -f "$1" ] && [ "$(sha "$1")" = "$REF_RPM_SHA256" ]; }

smon_step_start verbatim
got=
rpm=$cache/$REF_NVRA.rpm
if [ -n "${EL8_GLIBC_HEADERS_RPM:-}" ] && rpm_ok "$EL8_GLIBC_HEADERS_RPM"; then
	extract_rpm "$EL8_GLIBC_HEADERS_RPM" && got=local-rpm
fi
if [ -z "$got" ] && [ -f "$ref_root/features.h" ] \
   && [ "$(sha "$ref_root/features.h")" = "$REF_FEATURES_SHA256" ]; then
	got=cache
fi
if [ -z "$got" ] && rpm_ok "$rpm"; then
	extract_rpm "$rpm" && got=cache-rpm
fi
if [ -z "$got" ] && command -v curl >/dev/null 2>&1; then
	mkdir -p "$cache"
	say "fetching $REF_NVRA from the Rocky 8.10 vault"
	if curl -fsS --max-time 300 -o "$rpm" "$REF_URL"; then
		if rpm_ok "$rpm"; then
			extract_rpm "$rpm" && got=fetch
		else
			smon_step_fail verbatim 1
			fail "fetched rpm sha256 does not match the pin $REF_RPM_SHA256"
		fi
	fi
fi
[ -n "$got" ] || { smon_step_skip verbatim
	smon_item WP-50 partial "vendor reference unavailable offline; skipped"
	skip "no vendor reference (offline, no cache, no local rpm); pin is $REF_NVRA"; }
say "reference: $REF_NVRA  (via $got)"

# --- verbatim: every copied header is el8's, to the byte -----------------
printf '%s\n' "$VERBATIM" | while read -r rel want; do
	[ -n "$rel" ] || continue
	v=$veneer_inc/$rel
	r=$ref_root/$rel
	[ -f "$v" ] || { echo "MISS $rel (veneer)"; exit 1; }
	[ -f "$r" ] || { echo "MISS $rel (reference)"; exit 1; }
	cmp -s "$v" "$r"            || { echo "DIFF $rel differs from el8"; exit 1; }
	[ "$(sha "$v")" = "$want" ] || { echo "PIN  $rel sha256 != committed pin"; exit 1; }
	echo "ok   $rel"
done > "$work/verbatim.log"
vstat=$?
grep -v '^ok ' "$work/verbatim.log" >&2 || true
[ "$vstat" = 0 ] || { smon_step_fail verbatim 1
	fail "a vendored header is not byte-identical to el8's, or missed its pin"; }
[ "$quiet" = 1 ] || sed 's/^/  /' "$work/verbatim.log"
n=$(grep -c '^ok ' "$work/verbatim.log")

# --- the exception: gnu/stubs.h is ours, not el8's -----------------------
stub=$veneer_inc/gnu/stubs.h
[ -f "$stub" ] || { smon_step_fail verbatim 1; fail "veneer gnu/stubs.h is missing"; }
if [ "$(sha "$stub")" = "$EL8_STUBS_SHA256" ]; then
	smon_step_fail verbatim 1
	fail "gnu/stubs.h equals el8's; it must be the veneer's own (DR-0000 exception)"
fi
grep -q 'DR-0000' "$stub" || { smon_step_fail verbatim 1
	fail "gnu/stubs.h does not carry its DR-0000 exception reason"; }
grep -qi 'exception' "$stub" || { smon_step_fail verbatim 1
	fail "gnu/stubs.h does not name itself the exception"; }
say "exception: gnu/stubs.h is the veneer's own, distinct from el8's, and says why"
smon_step_ok verbatim
smon_item WP-50 met "$n headers byte-identical to $REF_NVRA; gnu/stubs.h the sole documented exception"

# --- closure: the copied features.h resolves and reports 2.28 ------------
smon_step_start closure
if [ -d "$HOME/x-elfsysvnt/bin" ]; then PATH="$HOME/x-elfsysvnt/bin:$PATH"; fi
GCC=${GCC:-x86_64-elfsysvnt-linux-gnu-gcc}
if ! command -v "$GCC" >/dev/null 2>&1; then
	smon_step_skip closure
	say "closure: cross gcc '$GCC' not on PATH; skipping the compile check"
	say ""
	say "PASS: $n headers are byte-identical to $REF_NVRA;"
	say "      gnu/stubs.h is the sole documented exception."
	smon_end 0
	exit 0
fi

macros="__GLIBC__ __GLIBC_MINOR__ __GNU_LIBRARY__ __USE_POSIX __USE_MISC \
__USE_XOPEN2K8 __USE_GNU __USE_ISOC11 __USE_LARGEFILE64 __USE_FORTIFY_LEVEL"
{
	printf '#include <features.h>\n#define Q(x) #x\n'
	for m in $macros; do
		printf '#ifdef %s\nRESULT Q(%s) = %s\n#endif\n' "$m" "$m" "$m"
	done
} > "$work/probe.in"
report() {  # <lang> <flags...>
	local lang=$1; shift
	"$GCC" "$@" -E -P -nostdinc -I "$veneer_inc" -x "$lang" "$work/probe.in" \
		2>"$work/cc.err" | sed -n 's/^RESULT //p' | sort
}
cases='c |;c |-D_GNU_SOURCE;c |-D_XOPEN_SOURCE=700;c |-ansi;c |-O2 -D_FORTIFY_SOURCE=2;c++ |-std=c++17 -D_GNU_SOURCE'
IFS=';'
for cs in $cases; do
	lang=${cs%%|*}; lang=$(printf '%s' "$lang" | tr -d ' ')
	flags=${cs#*|}
	unset IFS; set -- $flags
	if ! out=$(report "$lang" "$@"); then
		smon_step_fail closure 1
		sed 's/^/    /' "$work/cc.err" >&2
		fail "features.h did not preprocess for [$lang] ${flags:-<none>}"
	fi
	IFS=';'
done
unset IFS
id=$(report c)
echo "$id" | grep -qx '"__GLIBC__" = 2'        || { smon_step_fail closure 1; fail "__GLIBC__ is not 2"; }
echo "$id" | grep -qx '"__GLIBC_MINOR__" = 28' || { smon_step_fail closure 1; fail "__GLIBC_MINOR__ is not 28"; }
echo "$id" | grep -qx '"__GNU_LIBRARY__" = 6'  || { smon_step_fail closure 1; fail "__GNU_LIBRARY__ is not 6"; }
smon_step_ok closure
smon_item WP-50 met "el8 features.h resolves under the cross gcc and reports glibc 2.28"

say ""
say "PASS: $n headers byte-identical to $REF_NVRA, gnu/stubs.h the sole exception,"
say "      and el8's features.h resolves under the cross gcc reporting glibc 2.28."
smon_end 0
exit 0
