#!/usr/bin/env bash
#
# Wire the merge gate into this repository, or unwire it. Sets
# core.hooksPath to ci/hooks, which git resolves against the top of
# whichever worktree a merge runs in, so one install covers the main
# checkout and every worktree at once. Idempotent: running it twice
# leaves what one run leaves, and --uninstall removes what it manages.
#
# Usage:
#   install.sh [options]
#
# Options:
#   --uninstall   Remove the hook wiring this script installed.
#   -h, --help    Print this message and exit.
#
# Exit: 0 done, 2 usage or not a git checkout.

set -u
prog=install
usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

mode=install
case ${1:-} in
	'') ;;
	--uninstall) mode=uninstall ;;
	-h|--help) usage; exit 0 ;;
	*) printf '%s: unknown argument %s\n' "$prog" "$1" >&2; exit 2 ;;
esac

git rev-parse --git-dir >/dev/null 2>&1 || { printf '%s: not inside a git checkout\n' "$prog" >&2; exit 2; }

current=$(git config --get core.hooksPath 2>/dev/null || true)

if [ "$mode" = uninstall ]; then
	if [ "$current" = ci/hooks ]; then
		git config --unset core.hooksPath
		printf '%s: removed core.hooksPath (was ci/hooks)\n' "$prog"
	elif [ -n "$current" ]; then
		printf '%s: core.hooksPath is %s, not ci/hooks; leaving it alone\n' "$prog" "$current"
	else
		printf '%s: nothing installed\n' "$prog"
	fi
	exit 0
fi

if [ "$current" = ci/hooks ]; then
	printf '%s: already installed (core.hooksPath = ci/hooks)\n' "$prog"
elif [ -n "$current" ]; then
	printf '%s: core.hooksPath is already %s; refusing to overwrite a path this script does not manage\n' "$prog" "$current" >&2
	exit 2
else
	git config core.hooksPath ci/hooks
	printf '%s: installed (core.hooksPath = ci/hooks)\n' "$prog"
fi
