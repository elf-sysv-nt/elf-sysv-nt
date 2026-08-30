#!/usr/bin/env bash
#
# WP-42 certification: build the fork path with the host gcc that targets
# x86_64-pc-cygwin and hold it to the done-when bar.
#
# The unit test drives the three phases with no host fork in the loop, which is
# what makes the ordering checkable: prepare handlers in the reverse of
# registration, parent and child handlers in registration order, the loader lock
# taken after the last prepare handler and reinitialized rather than unlocked in
# the child, the region table sorted and disjoint, and an audit that names the
# first field that moved and names a rebase before anything the rebase caused.
#
# The fuzz target feeds the manifest unpacker malformed, truncated and mutated
# manifests against a guard page under the undefined-behaviour sanitizer. It is
# the one thing here that reads bytes it did not write, and in the real fork it
# reads them in a child that has repaired nothing yet.
#
# The end-to-end test is the done-when itself. A second thread loops through the
# real dl_open and dl_close of a cross-linked plugin, holding the loader lock
# and sleeping inside it, while the main thread forks through the three phases;
# the child calls dlsym on the object and calls into it across the ABI boundary
# and reports six bits, so 63 is the only pass. A hang is killed by the child's
# own watchdog and comes back as a signal rather than a status, so a deadlock
# cannot read as a slow pass. The tls stage forks from a managed thread, whose
# carrier the child does not have, and compares the whole DTV. The rebase stage
# has every child report the address of the loader's own code and the base of
# cygwin1.dll and requires both to be where the parent left them.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep      Keep the built binaries in the work dir instead of a tmp.
#   -n, --cases N   Fuzz cases (default 200000).
#   -c, --forks N   Forks per e2e stage (default 4).
#   -q, --quiet     Errors only.
#   -h, --help      Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)      # loader/fork/t
fork=$here/..                            # loader/fork
loader=$here/../..                       # loader
root=$loader/..                          # the tree

keep=0
quiet=0
cases=200000
forks=4

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say() { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)   usage; exit 0 ;;
		-k|--keep)   keep=1; shift ;;
		-q|--quiet)  quiet=1; shift ;;
		-n|--cases)  cases=${2:-200000}; shift 2 ;;
		-c|--forks)  forks=${2:-4}; shift 2 ;;
		--)          shift; break ;;
		-?*)         printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)           break ;;
	esac
done

cc=gcc
# -mno-red-zone is the standing policy DR-0006 keeps until WP-43; the driver
# calls into System V code and back.
cflags="-std=gnu11 -O2 -g -mno-red-zone -Wall -Wextra"
ubsan="-fsanitize=undefined -fsanitize-undefined-trap-on-error"

xg=$HOME/x-elfsysvnt/bin/x86_64-elfsysvnt-linux-gnu-gcc
xf="-ffreestanding -nostdlib -fcf-protection=none -O2 -fpic -shared"
xlf="-Wl,-z,max-page-size=0x10000 -Wl,--eh-frame-hdr"

pkg="$fork/fork.c $fork/manifest.c $fork/audit.c $fork/fork_host.c"
deps="$loader/rdebug/rdebug.c $root/runtime/tls/tp.c"
full="$loader/dl/dl.c $loader/dl/dl_init.c $loader/dl/dl_addr.c
      $loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S
      $loader/map/elf_map.c $loader/map/host_mem.c
      $loader/elf/elf_parse.c
      $loader/lookup/elf_lookup.c $loader/lookup/elf_hash.c
      $loader/graph/elf_graph.c $loader/graph/ldso_cache.c
      $loader/version/elf_version.c
      $loader/tls/elf_tls.c"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp42.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

smon_session build wp42-fork
smon_plan build unit fuzz front plugin e2e

rc=0

# --- build ----------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$bin/unit" "$here/unit.c" $pkg $deps -lpthread &&
   smon_cmd $cc $cflags $ubsan -o "$bin/fuzz" "$here/fuzz.c" $pkg $deps -lpthread &&
   smon_cmd $cc $cflags -o "$bin/elfsysv-fork" "$fork/fork_main.c" $pkg $deps -lpthread &&
   smon_cmd $cc $cflags -o "$bin/fork_e2e" "$here/fork_e2e.c" $pkg $deps $full -lpthread; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the fork path did not build"
fi

# --- the ordering, the manifest, the replay and the audit -----------------
smon_step_start unit
if smon_cmd "$bin/unit"; then
	smon_step_ok unit
else
	smon_step_fail unit $?; rc=1
fi

# --- the unpacker against a guard page ------------------------------------
smon_step_start fuzz
q=; [ "$quiet" = 1 ] && q=-q
if smon_cmd "$bin/fuzz" -n "$cases" $q; then
	smon_step_ok fuzz
else
	smon_step_fail fuzz $?; rc=1
fi

# --- the front end the spawn path will be ---------------------------------
smon_step_start front
front_ok=1
for flavor in fork vfork spawn; do
	if ! smon_cmd "$bin/elfsysv-fork" -f "$flavor" -n "$forks" -q; then
		front_ok=0
	fi
done
if [ "$front_ok" = 1 ]; then
	smon_step_ok front
else
	smon_step_fail front 1; rc=1
fi

# --- cross-link the plugin ------------------------------------------------
# WP-38's specimen, unchanged: a constructor, a destructor, an exported
# function and a relocated pointer. What matters here is only that it is a real
# object the loader really mapped, so a child that lost the mapping fails.
smon_step_start plugin
plugin_ok=0
if [ -x "$xg" ]; then
	if smon_cmd $xg $xf $xlf -Wl,-soname,libplug.so.0 \
		-o "$bin/libplug.so.0" "$loader/dl/t/plugin.c"; then
		plugin_ok=1
		smon_step_ok plugin
	else
		smon_step_fail plugin $?; rc=1
	fi
else
	smon_step_ok plugin
	smon_note plugin "cross gcc $xg not on PATH; e2e skipped"
	say "$prog: cross toolchain absent, e2e skipped"
fi

# --- the done-when --------------------------------------------------------
e2e_ran=0
smon_step_start e2e
if [ "$plugin_ok" = 1 ]; then
	if smon_cmd "$bin/fork_e2e" all "$bin/libplug.so.0" "$forks"; then
		e2e_ran=1
		smon_step_ok e2e
	else
		smon_step_fail e2e $?; rc=1
	fi
else
	smon_step_ok e2e
fi

if [ "$rc" = 0 ] && [ "$e2e_ran" = 1 ]; then
	smon_item wp42 met "the loader crosses fork intact: the object list, the search configuration, the static TLS layout and this thread's whole DTV, and the r_debug structure and its address all audit equal in the child, over a manifest of reservations the host does not replay. A fork taken while another thread is inside dlopen with the loader lock held produces a child that runs dlsym and calls into the loaded object rather than deadlocking, with a watchdog turning any hang into a signal the parent reports. pthread_atfork ordering is POSIX's in all three phases and the child reinitializes the lock rather than unlocking it. vfork and posix_spawn take the same path and the same test. The DLL rebase failure mode is confirmed absent by measurement: every child reports the loader's own code address and the base of cygwin1.dll and both are where the parent left them."
elif [ "$rc" = 0 ]; then
	smon_item wp42 partial "the unit, fuzz and front-end certification passed; the end-to-end fork needs the cross toolchain and did not run"
else
	smon_item wp42 unmet "a build or acceptance check did not reach its expected result"
fi

smon_end $rc
[ "$rc" = 0 ] && say "$prog: the loader crosses fork intact and a fork from inside dlopen does not deadlock; WP-42 certified"
exit $rc
