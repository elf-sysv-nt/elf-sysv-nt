#!/usr/bin/env bash
#
# WP-41 certification: build the branch, the window, the stub and the front
# end, then hold them to the done-when bar.
#
#   unit         the classifier, the `#!' line, the kernel's vector rebuild,
#                the depth limit, and the host's command-line quoting, all as
#                pure decisions over fixtures
#   fuzz         the same classifier against malformed and truncated heads,
#                each placed against a guard page, under the undefined
#                behaviour sanitizer
#   when         where the low window has to be reserved from, which is the
#                measurement the package's plan asks for before anything else
#   exec-elf     execve on an ELF binary from a Cygwin program: the front end
#                classifies, starts the stub with the window reserved in it,
#                and the specimen reads back argc, argv, envp, the auxv, its
#                own read-only and zero-filled data, and the psABI alignment
#   exec-script  a `#!' script still works, through the same resolver, with
#                the vector the kernel would have built
#   exec-chain   a two-hop chain and its argument order
#   exec-loop    a `#!' cycle is refused rather than followed
#   exec-nonelf  a file the host owns comes back as the host's, not as an error
#   exec-kind    the stub classifies a parsed image before it enters: static
#                keeps the direct-entry path, dynamic is owed the crossing,
#                a bare shared object is refused
#   dyn-cross    the stub runs the dynamic crossing and enters: an interp-
#                bearing image calls across into an ELF runtime and leaves
#                with the call's result, while the static path is undisturbed
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries in the work dir instead of a tmp.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
exec_dir=$here/..
loader=$here/../..

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
# The stack reserve is not a tuning choice. The kernel places the initial
# thread's stack before any instruction of the image runs, and with the
# toolchain's default two megabytes it places it exactly where the ELF world
# has to go, so the parent's reservation is refused. See DR-0028.
stubldflags="-Wl,--stack,0x100000"

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp41.XXXXXX"); fi

loader_srcs="$exec_dir/reserve.c $loader/map/elf_map.c $loader/map/host_mem.c \
$loader/elf/elf_parse.c $loader/process/process_image.c \
$loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S"

smon_session build wp41-exec-dispatch
smon_plan unit fuzz when specimen stub frontend exec-elf exec-script \
	exec-chain exec-loop exec-nonelf exec-kind dyn-cross

rc=0
say() { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

check() {	# check NAME GOT WANT
	if [ "$2" = "$3" ]; then
		say "    ok        $1: $2"
	else
		say "    FAILED    $1: got $2, wanted $3"
		rc=1
	fi
}

smon_step_start unit
if smon_cmd $cc $cflags -o "$bin/unit" "$here/unit.c" "$exec_dir/binfmt.c" \
	"$exec_dir/dispatch.c" "$exec_dir/reserve.c" && smon_cmd "$bin/unit"; then
	smon_step_ok unit
else
	smon_step_fail unit $?; rc=1
fi

smon_step_start fuzz
if smon_cmd $cc $cflags -fsanitize=undefined -fsanitize-undefined-trap-on-error \
	-o "$bin/fuzz" "$here/fuzz.c" "$exec_dir/binfmt.c" &&
   smon_cmd "$bin/fuzz" -n 200000; then
	smon_step_ok fuzz
else
	smon_step_fail fuzz $?; rc=1
fi

smon_step_start when
if smon_cmd bash "$here/reserve-when.sh" -q; then
	say "    ok        when: every route gave its recorded answer"
	smon_step_ok when
else
	say "    FAILED    when: a route changed its answer; rerun reserve-when.sh"
	smon_step_fail when $?; rc=1
fi

smon_step_start specimen
if smon_cmd $cross-gcc -static -nostdlib -no-pie -ffreestanding \
	-fcf-protection=none -Wl,-z,max-page-size=0x10000 \
	-o "$bin/specimen" "$here/specimen.S"; then
	smon_step_ok specimen
else
	smon_step_fail specimen $?; fail "specimen build failed (cross toolchain on PATH?)"
fi

smon_step_start stub
if smon_cmd $cc $cflags $stubldflags -o "$bin/elfsysv-stub" \
	"$exec_dir/stub.c" "$exec_dir/exec_kind.c" "$exec_dir/dyn_exec.c" \
	"$exec_dir/dyn_init.c" \
	"$exec_dir/enter.S" $loader_srcs; then
	smon_step_ok stub
else
	smon_step_fail stub $?; fail "stub build failed"
fi

smon_step_start frontend
if smon_cmd $cc $cflags -o "$bin/elfsysv-exec" "$exec_dir/exec_main.c" \
	"$exec_dir/dispatch.c" "$exec_dir/binfmt.c" "$exec_dir/reserve.c"; then
	smon_step_ok frontend
else
	smon_step_fail frontend $?; fail "front end build failed"
fi

export ELFSYSV_STUB=$bin/elfsysv-stub.exe
run=$bin/elfsysv-exec

# The specimen's seven checks, one per bit, so 127 is the only passing status.
smon_step_start exec-elf
"$run" "$bin/specimen" one two > /dev/null 2>&1
check "exec-elf" "$?" 127
[ "$rc" = 0 ] && smon_step_ok exec-elf || smon_step_fail exec-elf 1

# A `#!' script, run through the same resolver and handed to the host with the
# vector the kernel's rebuild produced.
smon_step_start exec-script
printf '#!/bin/echo interpreted\n' > "$bin/script.sh"
chmod +x "$bin/script.sh"
got=$("$run" -f "$bin/script.sh" tail 2>&1)
check "exec-script" "$got" "interpreted $bin/script.sh tail"
smon_step_ok exec-script

# Two hops. The vector reads interpreter, then each file that named one, in
# the order the hops were taken, then what was left of the original.
smon_step_start exec-chain
printf '#!/bin/echo two\n' > "$bin/inner.sh"
chmod +x "$bin/inner.sh"
printf '#!%s\n' "$bin/inner.sh" > "$bin/outer.sh"
chmod +x "$bin/outer.sh"
got=$("$run" -f "$bin/outer.sh" rest 2>&1)
check "exec-chain" "$got" "two $bin/inner.sh $bin/outer.sh rest"
smon_step_ok exec-chain

# A cycle. The depth limit is the detector, and the refusal has to name it.
smon_step_start exec-loop
printf '#!%s\n' "$bin/loop.sh" > "$bin/loop.sh"
chmod +x "$bin/loop.sh"
"$run" -r "$bin/loop.sh" > /dev/null 2>&1
check "exec-loop-status" "$?" 125
"$run" -r "$bin/loop.sh" 2>&1 | grep -q binfmt_err_depth
check "exec-loop-named" "$?" 0
smon_step_ok exec-loop

# Something the host owns. Not an error: a verdict, with the file the host
# should run.
smon_step_start exec-nonelf
got=$("$run" -r /bin/echo.exe 2>&1 | sed -n 's/^exec_kind=//p')
check "exec-nonelf" "$got" host
smon_step_ok exec-nonelf

# The stub classifies a parsed image before it enters: a static executable
# keeps this direct-entry path, a dynamic one (bzip2's shape) is refused as
# owed the crossing rather than entered into a fault, and a bare shared object
# is refused as no program on this route. DR-0058.
smon_step_start exec-kind
if smon_cmd bash "$here/exec-kind-stub.sh" -q; then
	say "    ok        exec-kind: the stub classifies before it enters"
	smon_step_ok exec-kind
else
	say "    FAILED    exec-kind: the stub's classification branch"
	smon_step_fail exec-kind $?; rc=1
fi

# The stub runs the dynamic crossing between the map and the entry: an interp-
# bearing image is linked against an ELF runtime through dyn_exec_link, so its
# PLT resolves into the runtime, and it is entered and leaves with the result
# of a cross-object call. The static path is checked undisturbed. DR-0058.
smon_step_start dyn-cross
if smon_cmd bash "$here/dyn-cross-stub.sh" -q; then
	say "    ok        dyn-cross: the stub crosses a dynamic image and enters"
	smon_step_ok dyn-cross
else
	say "    FAILED    dyn-cross: the stub's dynamic crossing branch"
	smon_step_fail dyn-cross $?; rc=1
fi

if [ "$rc" = 0 ]; then
	smon_item wp41 met "an ELF binary runs from a Cygwin program through the \
magic-byte branch, with the low window reserved into the stub while it was \
suspended -- the only point early enough, measured; a #! script still works \
through the same resolver and comes out with the vector the kernel would have \
built; the order between the ELF, #! and PE cases is one classifier over one \
head read, written down in DR-0027; and the four-hop interpreter limit is \
enforced and is what refuses a cycle"
else
	smon_item wp41 unmet "a WP-41 check did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: all exec-dispatch checks passed"; }
exit $rc
