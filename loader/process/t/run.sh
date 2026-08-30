#!/usr/bin/env bash
#
# WP-40 certification: build the process-image builder and its scaffolding,
# then hold it to the done-when bar. The unit tests check the layout as pure
# arithmetic across every parity of the vector; the image test maps a static
# ELF (WP-32), builds the psABI stack over it, enters it through the trampoline,
# and reads argc, argv[0], envp and the auxv back off the stack the specimen
# was handed, asserting the alignment and %rdx contract on the way; and the
# differential holds the auxv the builder produced against one a real Linux
# kernel builds, requiring that the two differ only in the entries that
# describe the platform and that AT_SYSINFO_EHDR is absent without the consumer
# faulting on its absence.
#
# The Linux reference is recaptured if a Linux is reachable (WSL here) and
# otherwise read from the committed transcript, so the differential runs on a
# machine with no Linux at hand as well.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries in the work dir instead of a tmp.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or test failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
proc=$here/..
map=$here/../../map
elf=$here/../../elf

keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  usage; exit 0 ;;
		-k|--keep)  keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--)         shift; break ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

# The cross toolchain that emits the static ELF entry specimen.
export PATH="$HOME/x-elfsysvnt/bin:$PATH"
cross=x86_64-elfsysvnt-linux-gnu

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
common="-static -nostdlib -no-pie -O2 -ffreestanding -fno-stack-protector -fcf-protection=none"

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp40.XXXXXX"); fi
specimen=$bin/specimen
unit=$bin/unit_test
imaget=$bin/image_test
built=$bin/built-auxv.txt
# The committed reference, and a scratch path for a fresh capture. A recapture
# is written to the scratch path, never over the committed transcript, so a run
# leaves the tree clean; the committed one is the fallback when no Linux answers.
committed=$(ls "$here"/linux-auxv-*.txt 2>/dev/null | sort | tail -1)
fresh=$bin/linux-auxv-fresh.txt
linuxref=$committed

smon_session build wp40-process-image
smon_plan specimen unit-build unit-run image-build image-run capture differential

rc=0

srcs_lib="$proc/process_image.c $map/elf_map.c $map/host_mem.c $elf/elf_parse.c"

smon_step_start specimen
if smon_cmd $cross-gcc $common -Wl,-z,max-page-size=0x10000 \
	-Wl,-Ttext-segment=0x10000000 -o "$specimen" "$here/specimen.c"; then
	smon_step_ok specimen
else
	smon_step_fail specimen $?; fail "specimen build failed (cross toolchain on PATH?)"
fi

smon_step_start unit-build
if smon_cmd $cc $cflags -o "$unit" "$here/unit.c" $srcs_lib; then
	smon_step_ok unit-build
else
	smon_step_fail unit-build $?; fail "unit build failed"
fi

smon_step_start unit-run
if smon_cmd "$unit"; then smon_step_ok unit-run; else smon_step_fail unit-run $?; rc=1; fi

smon_step_start image-build
if smon_cmd $cc $cflags -o "$imaget" \
	"$here/image_test.c" "$here/enter.S" $srcs_lib; then
	smon_step_ok image-build
else
	smon_step_fail image-build $?; fail "image_test build failed"
fi

smon_step_start image-run
if smon_cmd "$imaget" --dump-auxv "$specimen" > "$built"; then
	[ "$quiet" = 1 ] || cat "$built"
	smon_step_ok image-run
else
	[ "$quiet" = 1 ] || cat "$built"
	smon_step_fail image-run $?; rc=1
fi

# The Linux reference: recapture into a scratch file if a Linux answers, else
# fall back to the committed transcript. The committed file is never rewritten.
smon_step_start capture
if command -v wsl >/dev/null 2>&1 && \
   smon_cmd bash "$here/capture-linux-auxv.sh" -o "$fresh"; then
	linuxref=$fresh
	smon_step_ok capture
elif [ -n "${committed:-}" ] && [ -f "$committed" ]; then
	linuxref=$committed
	smon_note capture "no Linux reachable; using committed $committed"
	smon_step_ok capture
else
	smon_step_fail capture 1; fail "no Linux reference and none reachable to capture one"
fi

smon_step_start differential
if smon_cmd bash "$here/differential.sh" "$built" "$linuxref"; then
	smon_step_ok differential
else
	smon_step_fail differential $?; rc=1
fi

if [ "$rc" = 0 ]; then
	smon_item wp40 met "a static ELF is mapped, a psABI stack built over it with argc/argv/envp/auxv in the kernel's mold and 16-byte alignment honored, entered with %rsp on argc and %rdx carrying the atexit handler, and read back correctly by the entered image; the auxv differs from a real Linux kernel's only in the platform it describes, with AT_SYSINFO_EHDR absent and tolerated"
else
	smon_item wp40 unmet "a WP-40 check did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: all process-image checks passed"; }
exit $rc
