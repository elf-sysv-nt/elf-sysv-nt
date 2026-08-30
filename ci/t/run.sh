#!/usr/bin/env bash
#
# WP-T1 certification for the merge gate itself. Three claims, each
# checked in a scratch repository so the real one is never touched:
#
#   1. a merge goes through when every registered suite passes
#   2. a merge is BLOCKED when a suite fails, a fuzz crash included
#   3. the installer is idempotent and its uninstall removes the wiring
#
# The scratch repo wires core.hooksPath at a copy of ci/hooks and points
# the gate at stub registries; the failing stub exits the way the fuzz
# driver does when a case crashes. The real suites are certified by the
# gate's ordinary runs, not here.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 all checks passed, 1 a check failed, 2 usage.

set -u
prog=ci-t
here=$(cd "$(dirname "$0")" && pwd)
cidir=$(cd "$here/.." && pwd)

quiet=0
usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

while [ $# -gt 0 ]; do
	case $1 in
		-q|--quiet) quiet=1; shift ;;
		-n|--count) shift 2 ;;  # accepted for registry uniformity, unused
		--count=*)  shift ;;
		-h|--help)  usage; exit 0 ;;
		*) printf '%s: unknown argument %s\n' "$prog" "$1" >&2; exit 2 ;;
	esac
done

say() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*"; }
failures=0
bad() { printf '%s: FAIL: %s\n' "$prog" "$*" >&2; failures=$((failures+1)); }

work=$(mktemp -d "${TMPDIR:-/tmp}/cigate.XXXXXX")
trap 'rm -rf "$work"' EXIT

# A scratch repo with a topic branch to merge, and the gate wired in the
# same way install.sh wires the real one.
repo=$work/repo
mkdir -p "$repo"
git -C "$repo" init -q -b main
git -C "$repo" config user.email t@example.invalid
git -C "$repo" config user.name t
mkdir -p "$repo/ci/t"
cp "$cidir/gate.sh" "$repo/ci/gate.sh"
cp -r "$cidir/hooks" "$repo/ci/hooks"
cp "$cidir/install.sh" "$repo/ci/install.sh"
echo base > "$repo/base.txt"
git -C "$repo" add -A
git -C "$repo" commit -qm base

git -C "$repo" checkout -qb topic
echo topic > "$repo/topic.txt"
git -C "$repo" add topic.txt
git -C "$repo" commit -qm topic
git -C "$repo" checkout -q main
# Diverge main so the merge cannot fast-forward past the hook.
echo more >> "$repo/base.txt"
git -C "$repo" commit -qam diverge

(cd "$repo" && ./ci/install.sh >/dev/null)
[ "$(git -C "$repo" config --get core.hooksPath)" = ci/hooks ] \
	|| bad "install.sh did not set core.hooksPath"
(cd "$repo" && ./ci/install.sh >/dev/null) \
	|| bad "install.sh is not idempotent: second run failed"

# Stub suites: one that passes, one that fails the way the fuzz driver
# fails when a case crashes.
printf '#!/usr/bin/env bash\nexit 0\n' > "$repo/ci/t/pass.sh"
printf '#!/usr/bin/env bash\necho "fuzz: case 7 crashed (SIGSEGV)" >&2\nexit 1\n' > "$repo/ci/t/crash.sh"
chmod 755 "$repo/ci/t/pass.sh" "$repo/ci/t/crash.sh"
echo ci/t/pass.sh  > "$repo/ci/passing.txt"
echo ci/t/crash.sh > "$repo/ci/failing.txt"

# Claim 1: with every suite passing, the merge goes through.
if (cd "$repo" && GATE_ALLOW_UNPINNED=1 GATE_SUITES=$repo/ci/passing.txt \
		git merge -q --no-edit --no-ff topic >/dev/null 2>&1); then
	say "a passing gate lets the merge through"
else
	bad "the merge was blocked although every suite passed"
fi

# Claim 2: a crashing suite blocks the merge and leaves no merge commit.
# topic2 is fresh work off main, not the branch claim 1 already merged.
git -C "$repo" checkout -qb topic2
echo topic2 > "$repo/topic2.txt"
git -C "$repo" add topic2.txt
git -C "$repo" commit -qm topic2
git -C "$repo" checkout -q main
echo again >> "$repo/base.txt"
git -C "$repo" commit -qam diverge-again
before=$(git -C "$repo" rev-parse HEAD)
if (cd "$repo" && GATE_ALLOW_UNPINNED=1 GATE_SUITES=$repo/ci/failing.txt \
		git merge -q --no-edit --no-ff topic2 >/dev/null 2>&1); then
	bad "the merge went through over a crashing suite"
else
	after=$(git -C "$repo" rev-parse HEAD)
	if [ "$after" = "$before" ]; then
		say "a crashing suite blocks the merge"
	else
		bad "the merge was refused but HEAD still moved"
	fi
fi

# Claim 3: uninstall removes the wiring, and only its own.
(cd "$repo" && ./ci/install.sh --uninstall >/dev/null)
if git -C "$repo" config --get core.hooksPath >/dev/null 2>&1; then
	bad "uninstall left core.hooksPath behind"
else
	say "uninstall removes the wiring"
fi

if [ "$failures" = 0 ]; then
	say "all checks passed"
	exit 0
fi
printf '%s: %d check(s) failed\n' "$prog" "$failures" >&2
exit 1
