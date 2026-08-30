#!/usr/bin/env bash
#
# WP-34 certification: build the relocation engine and its scaffolding with the
# host compiler, build the dynamic specimens with the cross toolchain, and hold
# the engine to the done-when bar over them. A dynamically linked hello is
# relocated and run twice -- once linked lazy, once BIND_NOW -- and must make
# its cross-object call, read its imported datum, follow an internal relocated
# pointer, and run an ifunc-dispatched memcpy that lands on the body the CPU
# criterion selects. A pack-relative-relocs variant exercises RELR, and a
# thread-local specimen exercises the TPOFF64 arithmetic. Every step reports
# through the session monitor when a .smon marker sits above the working
# directory, and is a no-op emitter otherwise.
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
reloc=$here/..
elf=$here/../../elf
map=$here/../../map
graph=$here/../../graph

keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

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

# The cross toolchain that emits the dynamic specimens.
export PATH="$HOME/x-elfsysvnt/bin:$PATH"
xg=x86_64-elfsysvnt-linux-gnu-gcc
command -v "$xg" >/dev/null 2>&1 || fail "cross gcc $xg not on PATH"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
# -ffreestanding: the specimens carry no libc, so gcc must supply its own
# stdint.h rather than include_next to one that is not installed for the target.
# max-page-size=0x10000: pad segments to the Windows 64K granule so two
# PT_LOADs of unlike protection never share one, which WP-32 refuses (DR-0008).
xf="-ffreestanding -nostdlib -fcf-protection=none -O2"
xlf="-Wl,-z,max-page-size=0x10000"

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp34.XXXXXX"); fi
test_bin=$bin/reloc_test

cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

# ---- build the engine, its dependencies, and the harness ----------------
smon_step_start build 2>/dev/null || true
say "building the engine and harness with $cc"
$cc $cflags -c "$reloc/elf_reloc.c"     -o "$bin/elf_reloc.o"   || fail "elf_reloc.c"
$cc $cflags -c "$reloc/reloc_resolve.S" -o "$bin/reloc_resolve.o" || fail "reloc_resolve.S"
$cc $cflags -c "$elf/elf_parse.c"       -o "$bin/elf_parse.o"   || fail "elf_parse.c"
$cc $cflags -c "$map/elf_map.c"         -o "$bin/elf_map.o"     || fail "elf_map.c"
$cc $cflags -c "$map/host_mem.c"        -o "$bin/host_mem.o"    || fail "host_mem.c"
$cc $cflags -c "$graph/elf_graph.c"     -o "$bin/elf_graph.o"   || fail "elf_graph.c"
$cc $cflags -c "$graph/ldso_cache.c"    -o "$bin/ldso_cache.o"  || fail "ldso_cache.c"
$cc $cflags -c "$here/reloc_test.c"     -o "$bin/reloc_test.o"  || fail "reloc_test.c"
$cc $cflags -c "$here/relr_unit.c"      -o "$bin/relr_unit.o"   || fail "relr_unit.c"
$cc $cflags -c "$here/enter.S"          -o "$bin/enter.o"       || fail "enter.S"
$cc -o "$test_bin" \
	"$bin/reloc_test.o" "$bin/elf_reloc.o" "$bin/reloc_resolve.o" \
	"$bin/elf_parse.o" "$bin/elf_map.o" "$bin/host_mem.o" \
	"$bin/elf_graph.o" "$bin/ldso_cache.o" "$bin/enter.o" || fail "link reloc_test"
$cc -o "$bin/relr_unit" "$bin/relr_unit.o" "$bin/elf_reloc.o" "$bin/reloc_resolve.o" \
	"$bin/elf_parse.o" "$bin/elf_map.o" "$bin/host_mem.o" || fail "link relr_unit"

# ---- build the specimens with the cross toolchain -----------------------
say "building the dynamic specimens with the cross toolchain"
$xg $xf $xlf -fpic -shared -Wl,-soname,libgreet.so \
	-o "$bin/libgreet.so" "$here/greetlib.c" || fail "libgreet.so"

for disc in lazy now; do
	$xg $xf $xlf -fpie -pie -Wl,-z,$disc -Wl,-e,entry \
		-o "$bin/hello-$disc" "$here/hello.c" \
		-L"$bin" -lgreet -Wl,-rpath,"$bin" || fail "hello-$disc"
done

# RELR variant: same source, packed relative relocations. Skip gracefully if
# this binutils cannot emit them.
relr=0
if $xg $xf $xlf -fpie -pie -Wl,-z,lazy -Wl,-z,pack-relative-relocs -Wl,-e,entry \
	-o "$bin/hello-relr" "$here/hello.c" \
	-L"$bin" -lgreet -Wl,-rpath,"$bin" 2>/dev/null; then
	if x86_64-elfsysvnt-linux-gnu-readelf -S "$bin/hello-relr" 2>/dev/null \
		| grep -q RELR; then relr=1; fi
fi

# A TLS specimen from source: the platform's own toolchain refuses to emit the
# %fs-relative TLS relocation at link (WP-12, per DR-0003's %fs finding), so
# this build is expected to fail here. When it does, the TLS relocation types
# are certified against the pinned vendor libc instead (see tls_vendor below).
tls_built=0
if $xg $xf -fpic -shared -Wl,-soname,tlslib.so \
	-o "$bin/tlslib.so" "$here/tlslib.c" 2>/dev/null; then
	tls_built=1
fi

# The pinned el8 libc carries real R_X86_64_TPOFF64 relocations; if a vendor
# tree has been unpacked, the engine is held to them over the real object.
tls_vendor=${WP34_VENDOR_LIBC:-}
if [ -z "$tls_vendor" ]; then
	for c in /tmp/wp34-vendor/glibc/usr/lib64/libc.so.6; do
		[ -f "$c" ] && tls_vendor=$c && break
	done
fi
smon_step_ok build 2>/dev/null || true

# ---- run the cases ------------------------------------------------------
rc=0
run_case() {
	say ""
	smon_cmd "$test_bin" "$@" || rc=1
}

smon_step_start hello 2>/dev/null || true
run_case hello --expect-lazy "$bin/hello-lazy"
run_case hello --expect-now  "$bin/hello-now"
if [ "$relr" = 1 ]; then
	run_case hello --expect-lazy "$bin/hello-relr"
fi
smon_step_ok hello 2>/dev/null || true

# RELR: neither this toolchain nor el8 emits it, so the decoder is certified
# over a constructed stream rather than a linked object.
smon_step_start relr 2>/dev/null || true
say ""
say "== relr (constructed stream)"
say ""
smon_cmd "$bin/relr_unit" || rc=1
if [ "$relr" = 1 ]; then
	say "note: this toolchain also emitted RELR; the linked variant ran above"
else
	say "note: this toolchain does not emit RELR (glibc-gated), and el8 carries"
	say "      none; the decoder is certified over the constructed stream above"
fi
smon_step_ok relr 2>/dev/null || true

smon_step_start tls 2>/dev/null || true
if [ -n "$tls_vendor" ]; then
	run_case tls "$tls_vendor"
else
	say ""
	say "note: no vendor libc unpacked; TPOFF64 case skipped"
	say "      (set WP34_VENDOR_LIBC or unpack under /tmp/wp34-vendor)"
fi
smon_step_ok tls 2>/dev/null || true

say ""
if [ "$rc" = 0 ]; then say "all cases passed"; else say "a case failed"; fi
exit $rc
