#!/usr/bin/env bash
#
# What bin/check-doc-refs is worth: the defects it refuses.
#
# The checker passing tells you nothing on its own -- a check with a bug that
# makes it always pass looks exactly like a clean tree. So each case here
# injects one defect the checker is supposed to catch and asserts a non-zero
# exit, and two of them disable an EXEMPT row to prove the exemption is
# load-bearing rather than decoration.
#
# Every case runs against a throwaway clone, never the checkout you are in.
# An earlier version of this file restored its edits with `git checkout --`
# and discarded uncommitted work in the tree it ran from; the clone is the
# fix, and it is why the suite is safe to gate on.
#
# Usage:
#   check-doc-refs-test.sh [options]
#
# Options:
#   -k, --keep    Leave the clone behind and print its path.
#   -q, --quiet   Errors only.
#   -V, --version Print the version and exit.
#   -h, --help    Print this message and exit.
#
# Exit: 0 every case behaved, 1 a case did not, 2 a usage error.

set -u

prog=check-doc-refs-test
release='check-doc-refs-test 1.0'
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-k|--keep)    keep=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		-n|--count)   shift 2 ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done

command -v git >/dev/null 2>&1 || die 'no git on PATH'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
if [ "$keep" = 1 ]; then
	trap 'printf "%s: clone kept at %s\n" "$prog" "$work" >&2' EXIT
else
	trap 'rm -rf "$work"' EXIT
	trap 'rm -rf "$work"; exit 130' INT TERM
fi

# --shared borrows the object store, so this costs a few milliseconds even
# though the history is hundreds of commits. The clone is of HEAD, so a case
# that edits a tracked file is undone by checking it out again -- inside the
# clone, where there is nothing of anyone's to lose.
clone=$work/tree
git clone --quiet --shared --no-hardlinks "$repo" "$clone" 2>/dev/null ||
	die "cannot clone $repo"

pass=0
fail=0

case_run() { # name, expected-exit, setup, teardown
	name=$1; want=$2; setup=$3; teardown=$4
	( cd "$clone" && eval "$setup" ) || { fail=$((fail + 1)); note "FAIL $name (setup failed)"; return; }
	( cd "$clone" && bin/check-doc-refs -q >/dev/null 2>&1 ); got=$?
	( cd "$clone" && eval "$teardown" ) || die "teardown failed for $name"
	if [ "$got" = "$want" ]; then
		pass=$((pass + 1)); [ "$quiet" = 1 ] || printf '  ok   %s\n' "$name"
	else
		fail=$((fail + 1)); printf '  FAIL %s: wanted exit %s, got %s\n' "$name" "$want" "$got" >&2
	fi
}

case_run 'a clean tree passes' 0 ':' ':'

case_run 'a cited core/ path that does not exist fails' 1 \
	'printf "\ncited: \`core/nonesuch.c\`\n" >> doc/milestones.md' \
	'git checkout -- doc/milestones.md'

case_run 'a cited toolchain/ path that does not exist fails' 1 \
	'printf "\ncited: \`toolchain/nonesuch/README.md\`\n" >> doc/milestones.md' \
	'git checkout -- doc/milestones.md'

case_run 'a cited substrate/ path that does not exist fails' 1 \
	'printf "\ncited: \`substrate/nonesuch.c\`\n" >> doc/milestones.md' \
	'git checkout -- doc/milestones.md'

# The annex is untracked, so it is not a top-level directory of HEAD and a
# citation into it is not a citation this checker resolves. Without this the
# gate's verdict would differ between a fresh clone and a session worktree.
case_run 'a citation into the untracked annex is not checked' 0 \
	'printf "\ncited: \`a/nonesuch.txt\`\n" >> doc/milestones.md' \
	'git checkout -- doc/milestones.md'

case_run 'a met criterion naming a missing harness fails' 1 \
	'sed -i "s|criterion 2 is met (\`test/t/vfs-diff.sh\`)|criterion 2 is met (\`test/t/gone.sh\`)|" doc/design/Verification-Plan.md' \
	'git checkout -- doc/design/Verification-Plan.md'

case_run 'a met criterion whose harness is unregistered fails' 1 \
	'grep -v "^test/t/vfs-diff.sh" test/suites.tsv > test/suites.new && mv test/suites.new test/suites.tsv' \
	'git checkout -- test/suites.tsv'

case_run 'the DR-0010 exemption is load-bearing' 1 \
	"sed -i \"s|^    'toolchain/sysroot/include':|    'toolchain/sysroot/include-OFF':|\" bin/check-doc-refs" \
	'git checkout -- bin/check-doc-refs'

case_run 'the DR-0034 exemption is load-bearing' 1 \
	"sed -i \"s|^    'etc/elfsysvnt/manifest':|    'etc/elfsysvnt/manifest-OFF':|\" bin/check-doc-refs" \
	'git checkout -- bin/check-doc-refs'

note "$pass case(s) behaved, $fail did not"
[ "$fail" = 0 ] || exit 1
exit 0
