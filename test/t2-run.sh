#!/usr/bin/env bash
#
# WP-T2 -- run every differential that has a specified Linux answer against a
# real glibc, and record the verdict. The per-package differentials already
# know how to compare themselves to a real ld.so or a real auxv (WP-33 graph
# load order, WP-35 symbol resolution, WP-40 auxv); each reads LINUX_REF_DISTRO
# for which WSL image supplies the reference. This drives all three against the
# pinned el8 image (rocky8, glibc 2.28) rather than whatever glibc is on the
# machine, which is the burn-down of substitution row S1 in
# doc/substitutions.md. It does not re-implement the comparisons; it points them
# at el8 and collects their verdicts.
#
# Usage: t2-run.sh [-d DISTRO] [-o FILE]
#   -d DISTRO  the WSL image supplying the reference glibc [default: rocky8]
#   -o FILE    transcript destination [default: stdout]
# Exit: 0 every differential matched el8's glibc, 1 a real divergence, 77 the
#       reference image was unavailable so nothing was compared.

set -u
prog=t2-run
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)

distro=${LINUX_REF_DISTRO:-rocky8}
out=-
while [ $# -gt 0 ]; do
	case $1 in
		-d) shift; distro=${1:?-d wants a distro} ;;
		-o) shift; out=${1:?-o wants a file} ;;
		-h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "$prog: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done
export LINUX_REF_DISTRO=$distro
[ "$out" = - ] || exec > "$out" 2>&1

export PATH=/c/-/x-elfsysvnt/bin:$PATH

printf '# WP-T2 differentials against a real glibc\n'
printf '# %s, reference distro=%s\n' "$(date +%F)" "$distro"
printf '# %s\n\n' "$(wsl.exe -d "$distro" -- bash -lc 'ldd --version 2>&1 | head -1' 2>/dev/null | tr -d '\r')"

# Package -> the line its differential prints on a clean match.
declare -A match=(
	[graph]='all cases match a real ld.so'
	[lookup]='matches a real ld.so on every case'
	[process]='^verdict=pass'
)

rc=0
skipped=0
for pkg in graph lookup process; do
	printf '== %s\n' "$pkg"
	log=$( cd "$root/loader/$pkg" && ./t/run.sh -q 2>/dev/null )
	printf '%s\n' "$log" | grep -aiE 'diff-ldso:|differential|diverged|match a real|matches a real|^verdict=|auxv differs' | sed 's/^/   /'
	if printf '%s\n' "$log" | grep -qiE "${match[$pkg]}"; then
		:
	elif printf '%s\n' "$log" | grep -qi 'skipped: no real ld.so\|reference unavailable\|no wsl'; then
		skipped=$((skipped + 1))
	else
		rc=1
	fi
	printf '\n'
done

if [ "$rc" != 0 ]; then
	printf '# WP-T2: a differential diverged from el8 glibc 2.28\n'
	exit 1
fi
if [ "$skipped" = 3 ]; then
	printf '# WP-T2: every differential skipped; %s was not reachable\n' "$distro"
	exit 77
fi
printf '# WP-T2: the differentials match el8 glibc 2.28; substitution S1 is burned down\n'
exit 0
