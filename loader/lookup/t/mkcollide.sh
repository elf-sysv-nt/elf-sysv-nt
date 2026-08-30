#!/usr/bin/env bash
# WP-35: build the deliberate three-way name collision the differential turns on.
#
# Three shared objects each define the same symbol, collide(), returning a
# distinct tag: libone -> 11, libtwo -> 22, libthree -> 33. A root program
# imports collide(), calls it, and exits with its return value, so the winning
# definition's tag is the process exit status -- a channel a real ld.so and this
# loader both report unambiguously. Two roots differ only in the order they name
# libone and libtwo in DT_NEEDED, which is what makes the collision decidable by
# load order; libthree is never a DT_NEEDED and enters only through LD_PRELOAD,
# which is what makes it an interposition test.
#
# Built with the cross toolchain, -nostdlib, so they are real x86-64 Linux ELF a
# real ld.so also has an opinion about. The roots are PIE (ET_DYN) so a real
# ld.so runs them directly from its command line and the exit status carries the
# tag out. collide() is a leaf returning a constant with no relocations, so this
# loader can call the resolved pointer directly to read the same tag.
#
# Usage: mkcollide.sh [OUTDIR]
# Rerunning regenerates everything; the graph is not committed.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/collide}
cross=${CROSS:-x86_64-elfsysvnt-linux-gnu}
gcc=$cross-gcc
base=0x10000000

command -v "$gcc" >/dev/null 2>&1 || { echo "mkcollide: $gcc not on PATH" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out"

# A library exporting int collide(void){return TAG;} under a given soname.
mklib() {
	local of=$1 soname=$2 tag=$3
	local src=$out/.$soname.c
	echo "int collide(void){return $tag;}" > "$src"
	"$gcc" -ffreestanding -nostdlib -fcf-protection=none -O2 \
		-Wl,-z,max-page-size=0x10000 \
		-fpic -shared -Wl,-soname,"$soname" -o "$of" "$src"
	rm -f "$src"
}

mklib "$out/libone.so.0"   libone.so.0   11
mklib "$out/libtwo.so.0"   libtwo.so.0   22
mklib "$out/libthree.so.0" libthree.so.0 33

# A root importing collide(), calling it, and exiting with its value through a
# raw exit syscall so no libc is needed. DT_NEEDED order is the argument. rpath
# is $ORIGIN so the two libraries resolve next to the root under either
# filesystem namespace. Built PIE (ET_DYN) so a real ld.so runs it directly from
# its command line; collide() is reached through the PLT, which ld.so binds.
mkroot() {
	local of=$1; shift            # remaining args: the DT_NEEDED libraries
	local src=$out/.root.c
	cat > "$src" <<'EOF'
extern int collide(void);
void _start(void)
{
	int v = collide();
	register long rax __asm__("rax") = 60;   /* __NR_exit */
	register long rdi __asm__("rdi") = v;
	__asm__ volatile("syscall" : : "r"(rax), "r"(rdi) : "rcx", "r11", "memory");
	__builtin_unreachable();
}
EOF
	"$gcc" -ffreestanding -nostdlib -fcf-protection=none -O2 \
		-fpie -pie -fPIC \
		-Wl,-z,max-page-size=0x10000 \
		-Wl,-rpath,'$ORIGIN' -Wl,-e,_start \
		-o "$of" "$src" "$@"
	rm -f "$src"
}

# main_ab names libone before libtwo -> collide resolves to libone (tag 11).
mkroot "$out/main_ab" "$out/libone.so.0" "$out/libtwo.so.0"
# main_ba names libtwo before libone -> collide resolves to libtwo (tag 22).
mkroot "$out/main_ba" "$out/libtwo.so.0" "$out/libone.so.0"

echo "collision graph built in $out"
