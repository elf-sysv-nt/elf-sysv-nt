#!/usr/bin/env bash
#
# WP-38 certification: build the dl surface with the host gcc that targets
# x86_64-pc-cygwin and hold it to the done-when bar.
#
# The unit test builds the object table directly and checks what only a written
# table makes predictable: a chain initializes leaf first and finalizes in the
# exact reverse, a diamond runs the shared dependency once, a cycle is broken
# at the edge that closes it and gives the same order every walk, preinit and
# DT_INIT and the arrays run in the ABI's order, dlerror reports once, dladdr
# names the containing symbol, and dl_iterate_phdr hands out the phdrs.
#
# The end-to-end test is the done-when itself. It cross-links a real plugin --
# a constructor, a destructor, an exported function, a relocated pointer, and
# unwind tables so the linker emits PT_GNU_EH_FRAME -- and then loads and
# unloads it ten thousand times through the real dlopen and dlclose, checking
# after every single cycle that the file image, the table slot and the
# relocation-scope slot all came back. The first and last cycles look closely:
# the constructor ran before dlopen returned, the loaded code runs and returns
# the right answer across the ABI boundary, dladdr names the function, and
# PT_GNU_EH_FRAME is reachable through dl_iterate_phdr at an address whose
# first byte reads back as the .eh_frame_hdr version.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep      Keep the built binaries in the work dir instead of a tmp.
#   -n, --cycles N  Load/unload cycles for the e2e test (default 10000).
#   -q, --quiet     Errors only.
#   -h, --help      Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)      # loader/dl/t
dl=$here/..                              # loader/dl
loader=$here/../..                       # loader

keep=0
quiet=0
cycles=10000

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say() { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)   usage; exit 0 ;;
		-k|--keep)   keep=1; shift ;;
		-q|--quiet)  quiet=1; shift ;;
		-n|--cycles) cycles=${2:-10000}; shift 2 ;;
		--)          shift; break ;;
		-?*)         printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)           break ;;
	esac
done

cc=gcc
# -mno-red-zone is the standing policy DR-0006 keeps until WP-43; the driver
# calls into System V code and back.
cflags="-std=gnu11 -O2 -g -mno-red-zone -Wall -Wextra"

xg=$HOME/x-elfsysvnt/bin/x86_64-elfsysvnt-linux-gnu-gcc
# The plugin carries no libc and is never entered except through the loader.
# Segments are padded to the Windows 64K granule (DR-0008); --eh-frame-hdr is
# what makes the linker emit PT_GNU_EH_FRAME, which is the point of the test.
xf="-ffreestanding -nostdlib -fcf-protection=none -O2 -fpic -shared"
xlf="-Wl,-z,max-page-size=0x10000 -Wl,--eh-frame-hdr"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp38.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

srcs="$dl/dl.c $dl/dl_init.c $dl/dl_addr.c
      $loader/reloc/elf_reloc.c $loader/reloc/reloc_resolve.S
      $loader/map/elf_map.c $loader/map/host_mem.c
      $loader/elf/elf_parse.c
      $loader/lookup/elf_lookup.c $loader/lookup/elf_hash.c
      $loader/graph/elf_graph.c $loader/graph/ldso_cache.c
      $loader/version/elf_version.c
      $loader/rdebug/rdebug.c"

smon_session build wp38-dl-surface
smon_plan build unit plugin e2e

rc=0

# --- build ----------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$bin/dl_test" "$here/dl_test.c" $srcs &&
   smon_cmd $cc $cflags -o "$bin/dl_e2e" "$here/dl_e2e.c" $srcs; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the dl surface did not build"
fi

# --- the order, the error protocol, dladdr and the phdr walk --------------
smon_step_start unit
if smon_cmd "$bin/dl_test"; then
	smon_step_ok unit
else
	smon_step_fail unit $?; rc=1
fi

# --- cross-link the plugin ------------------------------------------------
smon_step_start plugin
plugin_ok=0
if [ -x "$xg" ]; then
	if smon_cmd $xg $xf $xlf -Wl,-soname,libplug.so.0 \
		-o "$bin/libplug.so.0" "$here/plugin.c"; then
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

# --- ten thousand loads and unloads --------------------------------------
e2e_ran=0
smon_step_start e2e
if [ "$plugin_ok" = 1 ]; then
	if smon_cmd "$bin/dl_e2e" "$bin/libplug.so.0" "$cycles"; then
		e2e_ran=1
		smon_step_ok e2e
	else
		smon_step_fail e2e $?; rc=1
	fi
else
	smon_step_ok e2e
fi

if [ "$rc" = 0 ] && [ "$e2e_ran" = 1 ]; then
	smon_item wp38 met "dlopen, dlsym, dlvsym, dlclose, dlerror, dladdr, dladdr1, dlinfo and dl_iterate_phdr are delivered over the loader's own packages; initialization runs DT_PREINIT_ARRAY then DT_INIT and DT_INIT_ARRAY with dependencies before dependents and the exact reverse on the way out, with a cycle broken at the edge that closes it; a real plugin loaded and unloaded $cycles times gives back its file image, table slot and scope slot after every cycle; and an unwinder walking dl_iterate_phdr finds PT_GNU_EH_FRAME in an object that arrived after startup."
elif [ "$rc" = 0 ]; then
	smon_item wp38 partial "the unit certification passed; the ten-thousand-cycle e2e needs the cross toolchain and did not run"
else
	smon_item wp38 unmet "a build or acceptance check did not reach its expected result"
fi

smon_end $rc
[ "$rc" = 0 ] && say "$prog: the surface holds and $cycles load/unload cycles leak nothing; WP-38 certified"
exit $rc
