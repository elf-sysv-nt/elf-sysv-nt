#!/usr/bin/env bash
#
# WP-62's certification: rpm's view of a built file repaired itself when the
# format became ELF, and this run is the evidence rather than the assertion.
#
#   build    build-libc runs clean; a consumer referencing a GLIBC_2.2.5
#            symbol by bare name is assembled and linked against it, the way
#            a fresh el8 link binds.
#   file     file(1) reports both files as ELF 64-bit, which is the string
#            everything downstream keys on.
#   magic    el8's elf.attr magic pattern -- the gate that decides whether
#            rpm runs elfdeps on a file at all -- matches that report.
#   deps     rpmdeps.py emits libc.so.6(GLIBC_2.2.5)(64bit) among the
#            provides of the built libc and the requires of the consumer,
#            with the soname lines beside them, exact strings.
#   vendor   el8's own elfdeps, run through WSL over the same two files,
#            produces byte-for-byte what rpmdeps.py produced. This is the
#            done-when: the vendor's generator judging the vendor's shape.
#            Skipped with a note where WSL or the el8 sysroot is missing;
#            the deps check above still holds the strings.
#
# Usage:
#   run-tests.sh [options]
#
# Options:
#   -P DIR, --prefix=DIR   Where the toolchain is installed.
#                          [default: $HOME/x-elfsysvnt]
#   -T TRIPLE, --target=TRIPLE
#                          [default: x86_64-elfsysvnt-linux-gnu]
#   -S DIR, --sysroot=DIR  The fetched el8 sysroot holding elfdeps.
#                          [default: /c/-/el8/versioned-libc/sysroot]
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 all pass, 1 a check failed, 2 usage, 77 no toolchain or no python3.

set -u

prog=run-tests
here=$(cd "$(dirname "$0")" && pwd)
surface=$(cd "$here/.." && pwd)
root=$(cd "$surface/../../.." && pwd)
prefix=${RUN_TESTS_PREFIX:-$HOME/x-elfsysvnt}
target=${RUN_TESTS_TARGET:-x86_64-elfsysvnt-linux-gnu}
sysroot=${RUN_TESTS_EL8_SYSROOT:-/c/-/el8/versioned-libc/sysroot}
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	opt=$1; shift
	val=
	case $opt in --*=*) val=${opt#*=}; opt=${opt%%=*} ;; esac
	case $opt in
		-P|--prefix|-T|--target|-S|--sysroot)
			[ -n "$val" ] || { [ $# -gt 0 ] || { printf '%s: %s wants a value\n' "$prog" "$opt" >&2; exit 2; }; val=$1; shift; } ;;
	esac
	case $opt in
		-P|--prefix) prefix=$val ;;
		-T|--target) target=$val ;;
		-S|--sysroot) sysroot=$val ;;
		-q|--quiet) quiet=1 ;;
		-h|--help) usage; exit 0 ;;
		--) break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$opt" >&2; exit 2 ;;
		*) break ;;
	esac
done

smon=/c/-/repo/session-monitor/lib/smon.sh
[ -f "$smon" ] && . "$smon"
type smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_step_skip() { :; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

smon_session build wp62-rpm-surface
smon_plan build file magic deps vendor

as=$prefix/bin/$target-as
ld=$prefix/bin/$target-ld
if ! command -v python3 >/dev/null 2>&1 || [ ! -x "$as" ]; then
	smon_step_skip build
	smon_item WP-62 partial "no python3 or no binutils under $prefix; skipped"
	smon_end 77
	say "no python3 or no $as; skipping (77)"
	exit 77
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

# --- build -------------------------------------------------------------------
smon_step_start build
"$root/veneer/libc/build-libc" -P "$prefix" -T "$target" -B "$tmp/libc" -q \
	|| { smon_step_fail build 1; fail 'build-libc failed'; }
libc=$tmp/libc/libc.so.6

# The consumer references one GLIBC_2.2.5 default-binding function by bare
# name, the way a fresh link binds, so its verneed carries exactly the node
# the done-when names. The pick comes off the forward map so no symbol name
# is written down here to rot.
sym=$(awk -F'\t' '$2=="GLIBC_2.2.5" && $3=="default" && $4 ~ /^func/ \
	&& $7!="scaffold" { print $1; exit }' "$tmp/libc/libc-forward.tsv")
[ -n "$sym" ] || { smon_step_fail build 1
	fail 'no GLIBC_2.2.5 default function in the forward map'; }
{
	printf '\t.text\n\t.globl _start\n_start:\n'
	printf '\tcall %s\n\tret\n' "$sym"
} > "$tmp/consumer.s"
"$as" --64 -o "$tmp/consumer.o" "$tmp/consumer.s" \
	|| { smon_step_fail build 1; fail 'consumer assembly failed'; }
"$ld" -o "$tmp/consumer" --no-as-needed \
	-dynamic-linker /lib64/ld-linux-x86-64.so.2 \
	"$tmp/consumer.o" "$libc" \
	|| { smon_step_fail build 1; fail 'consumer link failed'; }
chmod 755 "$tmp/consumer"
say "build: libc.so.6 and a consumer of $sym@GLIBC_2.2.5"
smon_step_ok build

# --- file --------------------------------------------------------------------
smon_step_start file
command -v file >/dev/null 2>&1 || { smon_step_fail file 1
	fail 'no file(1) on PATH'; }
flibc=$(file -b "$libc") || { smon_step_fail file 1; fail 'file refused libc'; }
fcons=$(file -b "$tmp/consumer") || { smon_step_fail file 1
	fail 'file refused the consumer'; }
case $flibc in "ELF 64-bit"*"shared object"*) ;; *)
	smon_step_fail file 1; fail "libc.so.6 reads as: $flibc" ;; esac
case $fcons in "ELF 64-bit"*) ;; *)
	smon_step_fail file 1; fail "the consumer reads as: $fcons" ;; esac
say "file: $flibc"
smon_step_ok file

# --- magic -------------------------------------------------------------------
# The pattern is %__elf_magic from el8's /usr/lib/rpm/fileattrs/elf.attr,
# rpm-build-4.14.3-32.el8_10, quoted verbatim. When the fetched sysroot is
# present the pattern is read from the vendor's own file and required to be
# this string, so a drift between the quote and the vendor is itself caught.
elf_magic='^(setuid,? )?(setgid,? )?(sticky )?ELF (32|64)-bit.*$'
smon_step_start magic
attr=$sysroot/usr/lib/rpm/fileattrs/elf.attr
if [ -f "$attr" ]; then
	vendor_magic=$(sed -n 's/^%__elf_magic[\t ]*//p' "$attr")
	[ "$vendor_magic" = "$elf_magic" ] || { smon_step_fail magic 1
		fail "elf.attr carries a different magic: $vendor_magic"; }
	say 'magic: pattern read off the vendor elf.attr'
else
	say 'magic: no el8 sysroot; pattern is the recorded quote'
fi
printf '%s\n' "$flibc" | grep -Eq "$elf_magic" || { smon_step_fail magic 1
	fail "elf.attr magic does not match: $flibc"; }
printf '%s\n' "$fcons" | grep -Eq "$elf_magic" || { smon_step_fail magic 1
	fail "elf.attr magic does not match: $fcons"; }
say 'magic: both files pass the rpm gate'
smon_step_ok magic

# --- deps --------------------------------------------------------------------
smon_step_start deps
python3 "$surface/rpmdeps.py" --provides "$libc" > "$tmp/provides.txt" \
	|| { smon_step_fail deps 1; fail 'rpmdeps.py refused libc'; }
python3 "$surface/rpmdeps.py" --requires --assume-executable "$tmp/consumer" \
	> "$tmp/requires.txt" \
	|| { smon_step_fail deps 1; fail 'rpmdeps.py refused the consumer'; }
for want in 'libc.so.6(GLIBC_2.2.5)(64bit)' 'libc.so.6()(64bit)'; do
	grep -Fxq "$want" "$tmp/provides.txt" || { smon_step_fail deps 1
		fail "provides lack the exact string $want"; }
	grep -Fxq "$want" "$tmp/requires.txt" || { smon_step_fail deps 1
		fail "requires lack the exact string $want"; }
done
say "deps: $(wc -l < "$tmp/provides.txt" | tr -d ' ') provides, $(wc -l \
	< "$tmp/requires.txt" | tr -d ' ') requires, the named strings exact"
smon_step_ok deps

# --- vendor ------------------------------------------------------------------
# el8's own generator judges the shape. It is a Linux binary, so it runs
# through WSL with the el8 sysroot's libraries in front of it, over the same
# two files; /mnt sees the same disk this shell calls /c. rpm reads st_mode
# through /mnt as 0777, so no --assume-executable asymmetry arises.
smon_step_start vendor
elfdeps=$sysroot/usr/lib/rpm/elfdeps
wp() { cygpath -w "$1" | sed -e 's|\\|/|g' -e 's|^\(.\):|/mnt/\L\1|'; }
if ! command -v wsl.exe >/dev/null 2>&1 || [ ! -f "$elfdeps" ]; then
	smon_step_skip vendor
	smon_item WP-62 partial 'no WSL or no el8 sysroot; vendor differential skipped'
	say 'vendor: skipped, no WSL or no el8 sysroot'
	smon_end 0
	say 'all reachable checks passed'
	exit 0
fi
wlibs=$(wp "$sysroot")/usr/lib64
vdeps() { wsl.exe -e env "LD_LIBRARY_PATH=$wlibs" "$(wp "$elfdeps")" "$@"; }
vdeps --provides "$(wp "$libc")" | tr -d '\r' > "$tmp/vendor-provides.txt" \
	|| { smon_step_fail vendor 1; fail 'vendor elfdeps refused libc'; }
vdeps --requires "$(wp "$tmp/consumer")" | tr -d '\r' \
	> "$tmp/vendor-requires.txt" \
	|| { smon_step_fail vendor 1; fail 'vendor elfdeps refused the consumer'; }
diff "$tmp/vendor-provides.txt" "$tmp/provides.txt" > "$tmp/pdiff" 2>&1 \
	|| { smon_step_fail vendor 1; head -20 "$tmp/pdiff" >&2
		fail 'provides differ from the vendor generator'; }
diff "$tmp/vendor-requires.txt" "$tmp/requires.txt" > "$tmp/rdiff" 2>&1 \
	|| { smon_step_fail vendor 1; head -20 "$tmp/rdiff" >&2
		fail 'requires differ from the vendor generator'; }
say "vendor: elfdeps agrees byte for byte, $(wc -l < "$tmp/vendor-provides.txt" \
	| tr -d ' ') provides and $(wc -l < "$tmp/vendor-requires.txt" \
	| tr -d ' ') requires"
smon_step_ok vendor

smon_item WP-62 ok 'rpm surface certified: file, magic, deps, vendor'
smon_end 0
say 'all checks passed'
exit 0
