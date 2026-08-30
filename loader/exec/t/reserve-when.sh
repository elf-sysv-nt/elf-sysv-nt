#!/usr/bin/env bash
#
# WP-41's first measurement: where the low window has to be reserved from.
#
# Builds the child probe four ways and runs each, then holds the results to the
# verdict the package needs before any of it can be written:
#
#   late    refused -- the control, and spike 2's arrangement
#   tls     does not link on this toolchain; Cygwin's runtime supplies no TLS
#           directory, so there is no callback slot to put a hook in
#   entry   refused -- the earliest instruction the image owns is still after
#           the kernel placed the initial stack and after cygwin1.dll ran
#   parent  reserved -- the window is taken from outside, into a child created
#           suspended, and the child's stack reserve is small enough that the
#           kernel placed the stack below the window
#
# The fifth case is the control for the fourth: the same parent reservation
# against a child with the default stack reserve is refused, which is what
# makes the small stack part of the answer rather than an incidental detail.
#
# Usage:
#   reserve-when.sh [options]
#
# Options:
#   -o FILE, --out=FILE  Write the transcript here as well as to stdout.
#   -k, --keep           Keep the built probes.
#   -q, --quiet          Errors only.
#   -h, --help           Print this message and exit.
#
# Exit: 0 every route gave its expected answer, 1 one did not, 2 usage.

set -u

prog=reserve-when
here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..

out=
keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  usage; exit 0 ;;
		-o)         out=$2; shift 2 ;;
		--out=*)    out=${1#--out=}; shift ;;
		-k|--keep)  keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--)         shift; break ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp41when.XXXXXX"); fi

say() { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }
rc=0
report=$bin/transcript.txt
: > "$report"
emit() { printf '%s\n' "$*" >> "$report"; [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

emit "WP-41: where the low window has to be reserved from"
emit "host: $(uname -sr)  cc: $($cc -dumpversion)  run: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
emit ""

# The default stack reserve for a Cygwin image is 2 MB, and the kernel places
# that stack at the window base. The small reserve moves it below.
small_stack=0x100000

build() {	# build ROUTE DEFINES [LDFLAGS...]
	local route=$1 defs=$2; shift 2
	$cc $cflags $defs -DARM_ROUTE="\"$route\"" \
		-o "$bin/when-$route" "$here/when.c" "$exec_dir/reserve.c" \
		"$@" 2> "$bin/when-$route.log"
}

expect() {	# expect ROUTE KEY VALUE FILE
	local route=$1 key=$2 want=$3 file=$4 got
	got=$(sed -n "s/^$key=//p" "$file" | head -1)
	if [ "$got" = "$want" ]; then
		emit "    ok        $route: $key=$got"
	else
		emit "    FAILED    $route: $key=$got, wanted $want"
		rc=1
	fi
}

# ---- late: the control -------------------------------------------------
emit "-- late: arm from main, after the CRT"
if build late ""; then
	"$bin/when-late" -q > "$bin/late.txt" 2>&1
	sed 's/^/    /' "$bin/late.txt" >> "$report"
	[ "$quiet" = 1 ] || sed 's/^/    /' "$bin/late.txt"
	expect late when_result refused "$bin/late.txt"
	expect late when_span_4m 0 "$bin/late.txt"
else
	emit "    FAILED    late: the control would not build"
	rc=1
fi
emit ""

# ---- tls: no directory to hang a callback on ---------------------------
emit "-- tls: arm from a .CRT\$XLB callback"
if build tls "-DARM_TLS"; then
	"$bin/when-tls" -q > "$bin/tls.txt" 2>&1
	sed 's/^/    /' "$bin/tls.txt" >> "$report"
	expect tls when_result reserved "$bin/tls.txt"
else
	emit "    measured  tls: does not link on this toolchain"
	sed 's/^/              /' "$bin/when-tls.log" >> "$report"
	[ "$quiet" = 1 ] || sed 's/^/              /' "$bin/when-tls.log"
	emit "              Cygwin's runtime supplies no _tls_used, so the PE has"
	emit "              no TLS directory and there is no callback slot at all."
fi
emit ""

# ---- entry: the earliest instruction the image owns ---------------------
emit "-- entry: arm from a replacement image entry point"
if build entry "-DARM_ENTRY" -Wl,-e,elf_stub_entry; then
	"$bin/when-entry" -q > "$bin/entry.txt" 2>&1
	sed 's/^/    /' "$bin/entry.txt" >> "$report"
	[ "$quiet" = 1 ] || sed 's/^/    /' "$bin/entry.txt"
	expect entry when_result refused "$bin/entry.txt"
	expect entry when_span_4m 0 "$bin/entry.txt"
	expect entry when_stack_at_window 1 "$bin/entry.txt"
else
	emit "    FAILED    entry: would not build"
	rc=1
fi
emit ""

# ---- parent: reserve from outside, into a suspended child ---------------
emit "-- parent: reserve through VirtualAllocEx into a suspended child"
if ! $cc $cflags -o "$bin/when_parent" "$here/when_parent.c" \
	"$exec_dir/reserve.c" 2> "$bin/parent.log"; then
	sed 's/^/    /' "$bin/parent.log"
	fail "the parent driver would not build"
fi

# ARM_ENTRY supplies the early hook and ARM_NONE stops it from arming, so the
# survey and the TIB read happen before cygwin1.dll switches the main thread
# onto a stack of its own. Read from main instead, the TIB would describe
# Cygwin's replacement stack rather than the one the kernel placed, and the
# kernel's is the one that competes for the window.
if build parent "-DARM_ENTRY -DARM_NONE" -Wl,-e,elf_stub_entry \
	-Wl,--stack,$small_stack; then
	"$bin/when_parent" -q "$(cygpath -w "$bin/when-parent.exe")" -q \
		> "$bin/parent.txt" 2>&1
	sed 's/^/    /' "$bin/parent.txt" >> "$report"
	[ "$quiet" = 1 ] || sed 's/^/    /' "$bin/parent.txt"
	expect parent parent_reserved 1 "$bin/parent.txt"
	expect parent when_result reserved "$bin/parent.txt"
	expect parent when_stack_at_window 0 "$bin/parent.txt"
else
	emit "    FAILED    parent: would not build"
	rc=1
fi
emit ""

# ---- parent, default stack: the control for the control ------------------
emit "-- parent, default stack: the same reservation with the stack in the way"
if build bigstack "-DARM_ENTRY -DARM_NONE" -Wl,-e,elf_stub_entry; then
	"$bin/when_parent" -q "$(cygpath -w "$bin/when-bigstack.exe")" -q \
		> "$bin/bigstack.txt" 2>&1
	sed 's/^/    /' "$bin/bigstack.txt" >> "$report"
	[ "$quiet" = 1 ] || sed 's/^/    /' "$bin/bigstack.txt"
	expect bigstack parent_reserved 0 "$bin/bigstack.txt"
	expect bigstack when_stack_at_window 1 "$bin/bigstack.txt"
else
	emit "    FAILED    bigstack: would not build"
	rc=1
fi
emit ""

if [ "$rc" = 0 ]; then
	emit "verdict: neither hook inside the image is early enough. The window is"
	emit "reserved by the parent into a suspended child, and the stub is linked"
	emit "with a $small_stack stack reserve so the kernel places the initial"
	emit "stack below the window rather than in it."
else
	emit "verdict: a route did not give its expected answer; see above."
fi

if [ -n "$out" ]; then
	cp "$report" "$out" || fail "cannot write $out"
fi
[ "$keep" = 1 ] || rm -rf "$bin"
exit $rc
