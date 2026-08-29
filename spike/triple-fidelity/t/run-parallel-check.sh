#!/usr/bin/env bash
#
# Harvest the same packages serially and in parallel, and diff the two dumps.
#
# The parallel path is only safe because a package's handling touches nothing
# outside itself and the aggregate is assembled from sorted filenames after
# everything has finished. That is an argument, and an argument is not a
# check. This is the check for the first half of it.
#
# Measured on 2026-08-29, with each half deliberately broken in turn. Sharing
# one scratch directory between jobs takes the parallel side down to two
# packages of twelve against the serial side's twelve, and this fails. Dropping
# the sort does not fail, because at this size the order the fragments finish in
# and the order they sort in are the same. So the ordering half rests on the
# argument still, and raising n would only make the coincidence less likely
# rather than impossible.
#
# It fetches, so it is not part of run-tests.sh, which touches no network.
# A dozen packages off the head of the selection, twice, is a couple of
# minutes and a few tens of megabytes.

set -u

here=$(cd "$(dirname "$0")" && pwd)
fetcher=$here/../fetch-host-tests.sh

[ -x "$fetcher" ] || { echo "not executable: $fetcher" >&2; exit 1; }

n=${1:-12}
base=$(mktemp -d "${TMPDIR:-/tmp}/parallel-check.XXXXXX")
trap 'rm -rf "$base"' EXIT

echo "serial, $n packages"
"$fetcher" --dest "$base/serial" --limit "$n" --jobs 1 --terse --quiet \
	--output "$base/serial.report" || { echo 'the serial run failed' >&2; exit 1; }

echo "parallel, the same $n"
"$fetcher" --dest "$base/parallel" --limit "$n" --jobs 4 --terse --quiet \
	--output "$base/parallel.report" || { echo 'the parallel run failed' >&2; exit 1; }

# The reports differ by design: one says jobs=1 and the other jobs=4. The
# dumps must not differ at all.
if diff -q "$base/serial/dump" "$base/parallel/dump" > /dev/null; then
	echo "ok, both dumps agree over $n packages"
else
	echo 'FAIL, the parallel dump differs from the serial one' >&2
	diff "$base/serial/dump" "$base/parallel/dump" | head -20 >&2
	exit 1
fi

for f in packages_harvested packages_failed; do
	a=$(grep -e "^$f=" "$base/serial.report")
	b=$(grep -e "^$f=" "$base/parallel.report")
	if [ "$a" != "$b" ]; then
		printf 'FAIL, %s differs: serial %s, parallel %s\n' "$f" "$a" "$b" >&2
		exit 1
	fi
done
echo 'ok, both runs harvested the same count'
