#!/usr/bin/env bash
#
# WP-40 / WP-T2 differential: hold the auxv WP-40 builds against the auxv a
# real Linux kernel builds, field-by-field on the keys, and decide whether the
# two differ only where they are allowed to.
#
# The values in an auxv -- addresses, uids, the host's hwcap -- legitimately
# differ between any two machines and any two runs, so this does not compare
# them. What it compares is the set of a_type keys each side emits, which is
# specified rather than incidental. The bar is:
#
#   * every key that describes the image or the platform and that a loader
#     reads to initialize must be present on both sides (the CORE set below);
#   * a key present on Linux but not in ours is a failure UNLESS it is one this
#     project deliberately omits or that a newer kernel added beyond the target
#     world (the EXPLAINED set below), and each such key is reported with its
#     reason;
#   * a key present in ours but not on Linux is always a failure -- we do not
#     get to invent auxv entries a consumer has never seen.
#
# AT_SYSINFO_EHDR is the load-bearing explained absence: there is no vDSO here,
# so it cannot be present, and a consumer that treats its absence as fatal is
# the bug WP-40 exists to surface. Its appearance in this report as an expected
# difference, rather than a failure, is that guarantee made visible.
#
# Usage:
#   differential.sh BUILT_KEYS LINUX_KEYS
#
# BUILT_KEYS holds "builtkey=NAME" lines (image_test --dump-auxv); LINUX_KEYS
# holds "linuxkey=NAME" lines (dump_auxv.py under Linux). Exit 0 when the only
# differences are allowed ones, 1 otherwise, 2 on usage.

set -u
prog=differential

[ $# -eq 2 ] || { echo "$prog: usage: $prog BUILT_KEYS LINUX_KEYS" >&2; exit 2; }
built=$1 linux=$2
for f in "$built" "$linux"; do
	[ -f "$f" ] || { echo "$prog: no such file: $f" >&2; exit 2; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/wp40diff.XXXXXX")
trap 'rm -rf "$work"' EXIT

grep '^builtkey=' "$built" | sed 's/^builtkey=//' | grep -v '^AT_NULL$' | sort -u > "$work/b"
grep '^linuxkey=' "$linux" | sed 's/^linuxkey=//' | grep -v '^AT_NULL$' | sort -u > "$work/l"

# The keys that must be present on both sides: the image description and the
# platform identity a dynamic linker reads at startup.
core="AT_PHDR AT_PHENT AT_PHNUM AT_ENTRY AT_BASE AT_PAGESZ AT_FLAGS \
AT_UID AT_EUID AT_GID AT_EGID AT_SECURE AT_CLKTCK AT_RANDOM \
AT_PLATFORM AT_HWCAP AT_HWCAP2 AT_EXECFN"

# Keys Linux may carry that we are allowed not to, each with why.
explain_key() {
	case $1 in
		AT_SYSINFO_EHDR)     echo "no vDSO here; its absence must be tolerated" ;;
		AT_MINSIGSTKSZ)      echo "post-el8 kernel addition, outside the target world" ;;
		AT_RSEQ_FEATURE_SIZE)echo "restartable-sequences, a kernel facility we do not present" ;;
		AT_RSEQ_ALIGN)       echo "restartable-sequences, a kernel facility we do not present" ;;
		AT_BASE_PLATFORM)    echo "set only on platforms that differ from AT_PLATFORM; not x86-64" ;;
		*)                   echo "" ;;
	esac
}

both=$(comm -12 "$work/b" "$work/l")
ours_only=$(comm -23 "$work/b" "$work/l")
linux_only=$(comm -13 "$work/b" "$work/l")

rc=0

echo "== present on both sides (differ in value only)"
echo
for k in $both; do
	printf '    %s\n' "$k"
done

echo
echo "== the CORE set, which must be on both sides"
echo
for k in $core; do
	if grep -qx "$k" "$work/b" && grep -qx "$k" "$work/l"; then
		printf '    %-20s present both\n' "$k"
	else
		printf '    %-20s MISSING (ours:%s linux:%s)\n' "$k" \
			"$(grep -qx "$k" "$work/b" && echo yes || echo no)" \
			"$(grep -qx "$k" "$work/l" && echo yes || echo no)"
		rc=1
	fi
done

echo
echo "== on Linux but not in ours"
echo
if [ -z "$linux_only" ]; then
	echo "    (none)"
else
	for k in $linux_only; do
		why=$(explain_key "$k")
		if [ -n "$why" ]; then
			printf '    %-20s expected -- %s\n' "$k" "$why"
		else
			printf '    %-20s UNEXPLAINED difference\n' "$k"
			rc=1
		fi
	done
fi

echo
echo "== in ours but not on Linux"
echo
if [ -z "$ours_only" ]; then
	echo "    (none)"
else
	for k in $ours_only; do
		printf '    %-20s INVENTED entry, not a kernel key\n' "$k"
		rc=1
	done
fi

echo
if [ "$rc" = 0 ]; then
	echo "verdict=pass  the auxv differs from Linux only in the platform it describes"
else
	echo "verdict=fail  an unallowed difference stands above"
fi
exit $rc
