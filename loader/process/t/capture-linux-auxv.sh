#!/usr/bin/env bash
#
# Capture the auxiliary vector a real Linux kernel builds, so the WP-40
# differential has a reference that is measured rather than remembered. It runs
# dump_auxv.py under a real Linux -- WSL on this host -- which reads the vector
# from /proc/self/auxv in the kernel's own order.
#
# The capture is reproducible in its findings, not byte for byte: the addresses
# and the host's hwcap move between runs and machines, but the set of a_type
# keys and their order do not, and that set is what the differential turns on.
# So a committed transcript is kept for the reader and the differential reads
# the "linuxkey=" lines out of a fresh or a kept capture alike.
#
# Usage:
#   capture-linux-auxv.sh [-o FILE]
#
# With -o the transcript is written there; otherwise to stdout. Exit 0 on a
# capture, 1 if no Linux is reachable, 2 on usage.

set -u
prog=capture-linux-auxv
here=$(cd "$(dirname "$0")" && pwd)

out=
while [ $# -gt 0 ]; do
	case $1 in
		-o) shift; out=${1:-}; [ -n "$out" ] || { echo "$prog: -o wants a file" >&2; exit 2; } ;;
		-h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "$prog: unknown argument $1" >&2; exit 2 ;;
	esac
	shift
done

command -v wsl >/dev/null 2>&1 || {
	echo "$prog: no wsl on PATH; a real Linux is needed to capture the reference" >&2
	exit 1
}

# Translate the script's Cygwin path to the /mnt form WSL mounts it under, so
# the same file is run without copying it across the boundary.
winmnt=$(printf '%s' "$here/dump_auxv.py" | sed -E 's#^/([a-zA-Z])/#/mnt/\L\1/#')

capture=$(wsl -e python3 "$winmnt" 2>/dev/null) || {
	echo "$prog: WSL could not run python3 on the dumper" >&2
	exit 1
}

emit() {
	printf '# Captured %s by %s under WSL\n' "$(date +%F)" "$prog"
	printf '# Reproducible in its keys and their order, not in the values.\n\n'
	printf '%s\n' "$capture"
}

if [ -n "$out" ]; then
	emit > "$out"
	echo "$prog: wrote $out"
else
	emit
fi
