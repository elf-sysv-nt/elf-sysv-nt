#!/usr/bin/env bash
# WP-35 differential: hold this loader's symbol resolution against a real glibc
# ld.so over the deliberate three-way name collision.
#
# collide() is defined by three objects returning distinct tags (libone 11,
# libtwo 22, libthree 33). For each case the script asks both loaders which
# object they bind collide() to: our own resolver, through lookup_test's collide
# mode, which prints the winning soname; and, through WSL, the host's real
# ld.so, whose LD_DEBUG=bindings output reports -- for every reference it
# resolves -- the object it bound it to, read straight from the loader. The two
# sides agree when they name the same object over every case: the plain load
# order, the reversed load order, and the LD_PRELOAD interposition.
#
# A real ld.so is required. Without WSL and its Ubuntu image the script exits 77
# (skip) rather than failing, exactly as WP-33's differential does.
#
# Usage: diff-ldso.sh LOOKUP_TEST GRAPHS_DIR
# Exit: 0 every case matched, 1 a case diverged, 77 no real ld.so available.
set -u

prog=diff-ldso
lt=${1:?usage: diff-ldso.sh LOOKUP_TEST GRAPHS_DIR}
G=${2:?usage: diff-ldso.sh LOOKUP_TEST GRAPHS_DIR}
GABS=$(cd "$G" && pwd)

# The graph directory as WSL sees it (/mnt/c/...), by the same rule WP-33 uses.
if command -v cygpath >/dev/null 2>&1; then
	W=$(cygpath -m "$GABS" | sed -E 's#^([A-Za-z]):#/mnt/\L\1#')
else
	W=$(printf '%s' "$GABS" | sed -E 's#^/cygdrive/([a-z])#/mnt/\1#; s#^/([a-z])/#/mnt/\1/#')
fi
ldso=/lib64/ld-linux-x86-64.so.2

# case  root      preload(- none)    expected-tag
cases=(
	"plain-ab|main_ab|-|11"
	"plain-ba|main_ba|-|22"
	"preload |main_ab|libthree.so.0|33"
)

if ! command -v wsl.exe >/dev/null 2>&1; then
	echo "$prog: wsl.exe not found; skipping the real-ld.so differential" >&2
	exit 77
fi
if ! wsl.exe -d Ubuntu -- test -x "$ldso" 2>/dev/null; then
	echo "$prog: no Ubuntu ld.so at $ldso; skipping the differential" >&2
	exit 77
fi

rc=0
for row in "${cases[@]}"; do
	IFS='|' read -r name root pre expect <<<"$row"
	name=$(echo "$name" | tr -d ' ')

	# --- our side: lookup_test collide -----------------------------------
	# The winner is the soname of the object our resolver bound collide() to;
	# the tag is that object's return value, shown for the record.
	ours_args=("$GABS" "$GABS/$root")
	[ "$pre" != - ] && ours_args+=(--preload "$GABS/$pre")
	ours_out=$("$lt" collide "${ours_args[@]}" 2>/dev/null)
	ours_tag=$(printf '%s\n' "$ours_out" | sed -n 's/.*tag=\(-\{0,1\}[0-9]*\).*/\1/p')
	ours_win=$(printf '%s\n' "$ours_out" | sed -n 's/.*winner=\([^ ]*\).*/\1/p')

	# --- real side: what object the host ld.so binds collide() to ---------
	# LD_DEBUG=bindings makes glibc's own loader report, for every symbol
	# reference it resolves, the object it bound it to -- the resolution
	# decision itself, read straight from the loader rather than inferred. The
	# soname on that line for collide() is the authoritative answer this
	# differential holds our resolver to. (An exit-status channel was tried and
	# dropped: a freestanding image's raw exit syscall does not propagate a
	# status under this WSL, a quirk of the environment unrelated to lookup.)
	envp=""
	[ "$pre" != - ] && envp="LD_PRELOAD='$W/$pre' "
	real_bind=$(wsl.exe -d Ubuntu -- bash -lc \
		"${envp}LD_DEBUG=bindings LD_BIND_NOW=1 '$ldso' '$W/$root' 2>&1 >/dev/null \
		 | grep \"symbol .collide\" | head -1" 2>/dev/null)
	real_win=$(printf '%s\n' "$real_bind" | sed -n 's#.*/\([^/ ]*\) \[0\]: normal.*#\1#p')

	if [ -n "$ours_win" ] && [ "$ours_win" = "$real_win" ]; then
		printf '%s: %-9s ok   binds=%s (tag %s)\n' \
			"$prog" "$name" "$real_win" "$ours_tag"
	else
		printf '%s: %-9s DIVERGED  ours=%s  real=%s\n' \
			"$prog" "$name" "${ours_win:-?}" "${real_win:-?}"
		rc=1
	fi
done

[ "$rc" = 0 ] && echo "$prog: symbol resolution matches a real ld.so on every case"
exit $rc
