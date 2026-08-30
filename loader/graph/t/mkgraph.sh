#!/usr/bin/env bash
# WP-33: build the constructed dependency graphs the tests walk.
#
# Each graph is a set of tiny shared objects (and one non-PIE root) built with
# the cross toolchain, so they are real x86-64 Linux ELF that a real ld.so also
# has an opinion about. They carry no libc: every DT_NEEDED points at another
# object in the graph, so resolution stays inside the directories the test
# controls and nothing is pulled from the host. RPATH and RUNPATH are written
# as $ORIGIN-relative, which resolves the same whether the tree is read as
# /c/... under Cygwin or /mnt/c/... under a real ld.so in WSL.
#
# The cases: a diamond (shared dependency reached two ways), an RPATH-versus-
# LD_LIBRARY_PATH precedence pair, a RUNPATH inheritance pair, an $ORIGIN
# lookup, and a missing dependency.
#
# Usage: mkgraph.sh [OUTDIR]
# Rerunning regenerates everything; the graphs are not committed.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/graph}
cross=${CROSS:-x86_64-elfsysvnt-linux-gnu}
gcc=$cross-gcc
base=0x10000000

rm -rf "$out"
mkdir -p "$out"

# A shared object exporting one unique symbol, given soname, rpath/runpath, and
# dependencies. Args: outfile soname dtflag rpath needed...
#   dtflag: --disable-new-dtags (RPATH) or --enable-new-dtags (RUNPATH) or -
mklib() {
	local of=$1 soname=$2 dtflag=$3 rpath=$4; shift 4
	local sym=${soname//[.-]/_}
	local src=$out/.$sym.c
	echo "int ${sym}_sym(void){return 1;}" > "$src"
	local args=(-shared -nostdlib -fPIC -Wl,-soname,"$soname")
	[ "$dtflag" != - ] && args+=(-Wl,"$dtflag")
	[ -n "$rpath" ] && args+=(-Wl,-rpath,"$rpath")
	"$gcc" "${args[@]}" -o "$of" "$src" "$@"
	rm -f "$src"
}

# A non-PIE root program with given soname-less identity, rpath/runpath, and
# dependencies. Args: outfile dtflag rpath needed...
mkroot() {
	local of=$1 dtflag=$2 rpath=$3; shift 3
	local src=$out/.root.c
	echo "extern int x(void);int _start(void){return 0;}" > "$src"
	local args=(-nostdlib -no-pie -fPIC -Wl,-Ttext-segment="$base")
	[ "$dtflag" != - ] && args+=(-Wl,"$dtflag")
	[ -n "$rpath" ] && args+=(-Wl,-rpath,"$rpath")
	"$gcc" "${args[@]}" -o "$of" "$src" "$@"
	rm -f "$src"
}

# --- diamond: root -> a, b ; a -> d ; b -> d --------------------------------
d=$out/diamond; mkdir -p "$d"
mklib "$d/libd.so.0" libd.so.0 - '$ORIGIN'
mklib "$d/liba.so.0" liba.so.0 - '$ORIGIN' "$d/libd.so.0"
mklib "$d/libb.so.0" libb.so.0 - '$ORIGIN' "$d/libd.so.0"
mkroot "$d/main" - '$ORIGIN' "$d/liba.so.0" "$d/libb.so.0"

# --- precedence: same soname in rp/ and lp/ ---------------------------------
p=$out/prec; mkdir -p "$p/rp" "$p/lp"
mklib "$p/rp/libpick.so.0" libpick.so.0 - '' 
mklib "$p/lp/libpick.so.0" libpick.so.0 - ''
# RPATH form: rpath searched before LD_LIBRARY_PATH -> resolves in rp/
mkroot "$p/main_rpath"   --disable-new-dtags '$ORIGIN/rp' "$p/rp/libpick.so.0"
# RUNPATH form: LD_LIBRARY_PATH searched before runpath -> resolves in lp/
mkroot "$p/main_runpath" --enable-new-dtags  '$ORIGIN/rp' "$p/rp/libpick.so.0"

# --- inheritance: root -> a ; a -> t ; t only in deep/ ----------------------
i=$out/inherit; mkdir -p "$i/deep"
mklib "$i/deep/libt.so.0" libt.so.0 - ''
mklib "$i/liba.so.0"      liba.so.0 - '' "$i/deep/libt.so.0"
# RPATH is inherited: a resolves libt through root's rpath -> found
mkroot "$i/main_rpath"   --disable-new-dtags '$ORIGIN:$ORIGIN/deep' "$i/liba.so.0"
# RUNPATH is not inherited: a cannot see root's deep/ -> libt not found
mkroot "$i/main_runpath" --enable-new-dtags  '$ORIGIN:$ORIGIN/deep' "$i/liba.so.0"

# --- origin: dependency in a $ORIGIN-relative subdir ------------------------
o=$out/origin; mkdir -p "$o/plug"
mklib "$o/plug/libplug.so.0" libplug.so.0 - ''
mkroot "$o/main" - '$ORIGIN/plug' "$o/plug/libplug.so.0"

# --- cache-only: dependency in a directory reached by neither rpath nor
# --- LD_LIBRARY_PATH, so only an ldconfig cache can resolve it ---------------
c=$out/cacheonly; mkdir -p "$c/hidden"
mklib "$c/hidden/libcache.so.0" libcache.so.0 - ''
mkroot "$c/main" - '' "$c/hidden/libcache.so.0"   # no rpath, no runpath

# --- missing: root needs a present lib and a ghost, then the ghost is removed
m=$out/missing; mkdir -p "$m"
mklib "$m/libpresent.so.0" libpresent.so.0 - '$ORIGIN'
mklib "$m/libghost.so.0"   libghost.so.0   - ''
mkroot "$m/main" - '$ORIGIN' "$m/libpresent.so.0" "$m/libghost.so.0"
rm -f "$m/libghost.so.0"   # now DT_NEEDED libghost.so.0 has no provider

echo "graphs built in $out"
