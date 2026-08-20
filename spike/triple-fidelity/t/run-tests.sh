#!/usr/bin/env bash
#
# Classify t/sample-dump.txt and compare the summary against counts worked out
# by hand. Run this after touching the classification.
#
# A classifier that quietly stops recognizing one record shape does not fail.
# It reports a smaller number, and a smaller number is the one outcome this
# whole measurement exists to avoid believing.

set -u

here=$(cd "$(dirname "$0")" && pwd)
script=$here/../count-vendor-misses.sh

[ -x "$script" ] || { echo "not executable: $script" >&2; exit 1; }

got=$(mktemp "${TMPDIR:-/tmp}/run-tests.XXXXXX")
trap 'rm -f "$got"' EXIT

"$script" --dump "$here/sample-dump.txt" --terse --quiet > "$got" || {
	echo 'the classifier exited nonzero' >&2
	exit 1
}

if diff -u "$here/expected-summary.txt" "$got"; then
	echo 'ok, summary matches the hand-worked counts'
else
	echo 'FAIL, summary drifted from the hand-worked counts' >&2
	exit 1
fi
