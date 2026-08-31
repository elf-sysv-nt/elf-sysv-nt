#!/usr/bin/env bash
# WP-33 differential: hold elf-ldd's load order against a real glibc ld.so.
#
# For each constructed graph it runs elf-ldd and, through WSL, the host's own
# ld.so in its ldd tracing mode (LD_TRACE_LOADED_OBJECTS), then compares the
# ordered list of resolved objects. The two run in different filesystem
# namespaces -- /c/... under Cygwin, /mnt/c/... under WSL -- so both are
# normalised by stripping everything up to the shared "graphs/" directory,
# which leaves each object's path relative to the graph and lets the two
# namespaces compare equal while still showing which directory a name resolved
# in. That last point is what makes the RPATH-versus-RUNPATH cases decidable:
# the same soname sits in two directories and only the winner's path is printed.
#
# A real ld.so is required. If WSL or its Ubuntu image is absent the script
# exits 77 (skip) rather than failing, so a host without one does not block a
# build; the certification run has one.
#
# Usage: diff-ldso.sh ELF_LDD GRAPHS_DIR
# Exit: 0 every case matched, 1 a case diverged, 77 no real ld.so available.
set -u

prog=diff-ldso

# Which Linux image supplies the reference ld.so. Default Ubuntu (glibc 2.43)
# for back-compat; WP-T2 drives this with the pinned el8 image (rocky8, glibc
# 2.28) to burn down substitution row S1. Env only, since the callers are
# scripts, not a command line.
distro=${LINUX_REF_DISTRO:-Ubuntu}
elfldd=${1:?usage: diff-ldso.sh ELF_LDD GRAPHS_DIR}
G=${2:?usage: diff-ldso.sh ELF_LDD GRAPHS_DIR}

# The graph directory as WSL sees it. cygpath -m gives the Windows path with
# forward slashes (C:/...), which becomes /mnt/c/... for WSL -- this works
# wherever the tree lives, including the Cygwin root's own /tmp, which a naive
# /c-prefix rewrite would miss.
GABS=$(cd "$G" && pwd)
if command -v cygpath >/dev/null 2>&1; then
	W=$(cygpath -m "$GABS" | sed -E 's#^([A-Za-z]):#/mnt/\L\1#')
else
	W=$(printf '%s' "$GABS" | sed -E 's#^/cygdrive/([a-z])#/mnt/\1#; s#^/([a-z])/#/mnt/\1/#')
fi

ldso=/lib64/ld-linux-x86-64.so.2

# case  name             main                    ld_library_subdir(- none)
cases=(
	"diamond|diamond/main|-"
	"prec-rpath|prec/main_rpath|prec/lp"
	"prec-runpath|prec/main_runpath|prec/lp"
	"inherit-rpath|inherit/main_rpath|-"
	"inherit-runpath|inherit/main_runpath|-"
	"origin|origin/main|-"
	"missing|missing/main|-"
)

# Normalise a trace to canonical "name => relpath" (or "name => not found")
# lines: keep only the object edges (dropping the vDSO and interpreter), strip
# the address column, and remove the graph-directory prefix ($1, in whichever
# namespace produced the trace) so the two sides compare equal while the path
# below the graph -- which directory a name resolved in -- is preserved.
normalise() {  # $1 = absolute graph-dir prefix to strip
	sed -E "
		/ => / !d
		s/^[[:space:]]+//
		s/ \(0x[0-9a-fA-F]+\)\$//
		s#=> $1/#=> #
	"
}

# Confirm a usable WSL Ubuntu with the loader before doing any work.
if ! command -v wsl.exe >/dev/null 2>&1; then
	echo "$prog: wsl.exe not found; skipping the real-ld.so differential" >&2
	exit 77
fi
if ! wsl.exe -d "$distro" -- test -x "$ldso" 2>/dev/null; then
	echo "$prog: no reference ld.so at $ldso; skipping the differential" >&2
	exit 77
fi

# Build one WSL script that emits every case's trace, delimited, in a single
# invocation -- one WSL start rather than seven.
wsl_script=""
for row in "${cases[@]}"; do
	IFS='|' read -r name main sub <<<"$row"
	ld=""
	[ "$sub" != - ] && ld="LD_LIBRARY_PATH='$W/$sub' "
	wsl_script+="echo '@@ $name'; ${ld}LD_TRACE_LOADED_OBJECTS=1 LD_WARN=yes '$ldso' '$W/$main' 2>&1;"
done
golden=$(wsl.exe -d "$distro" -- bash -lc "$wsl_script" 2>&1)

rc=0
for row in "${cases[@]}"; do
	IFS='|' read -r name main sub <<<"$row"

	# The golden block for this case: lines between its marker and the next.
	gold=$(printf '%s\n' "$golden" | awk -v n="@@ $name" '
		$0==n {on=1; next} /^@@ / {on=0} on' | normalise "$W")

	# elf-ldd, with the same LD_LIBRARY_PATH in the Cygwin namespace.
	if [ "$sub" != - ]; then LP=(-L "$GABS/$sub"); else LP=(); fi
	mine=$("$elfldd" --bare "${LP[@]}" "$GABS/$main" | normalise "$GABS")

	if [ "$gold" = "$mine" ]; then
		printf '%s: %-16s ok\n' "$prog" "$name"
	else
		printf '%s: %-16s DIVERGED\n' "$prog" "$name"
		printf '  --- real ld.so ---\n%s\n  --- elf-ldd ---\n%s\n' \
			"$gold" "$mine" | sed 's/^/  /'
		rc=1
	fi
done

[ "$rc" = 0 ] && echo "$prog: all cases match a real ld.so"
exit $rc
