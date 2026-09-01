#!/usr/bin/env bash
#
# Certify the acceptance run stage: accept.sh launches a `ready` package
# through the loader's crossing rather than stopping at `ready`, and it never
# reports `passing` unless the package's own suite ran through the crossing and
# passed. The run-stage-wired rung of acceptance/to-green.tsv rests on this.
#
#   syntax    accept.sh parses
#   wired     the run stage exists and -R can turn it off
#   fires     a default run of a ready package carries a run: field that is not
#             `skipped` -- the stage launched the image through the crossing
#   honest    the verdict is `passing` only when the run field is `passed`; an
#             image that halts or merely runs is not green
#
# Exit: 0 all checks passed, 1 a check failed.

set -u
here=$(cd "$(dirname "$0")" && pwd)
acc=$here/../accept.sh
export PATH="$HOME/x-elfsysvnt/bin:$PATH"
rc=0
say() { printf '%s\n' "$*"; }
check() { if [ "$2" = "$3" ]; then say "    ok        $1"; else say "    FAILED    $1: got [$2], wanted [$3]"; rc=1; fi; }

# syntax
if bash -n "$acc"; then say "    ok        syntax"; else say "    FAILED    syntax"; rc=1; fi

# wired: the stage is present and switchable
grep -q 'run_suite' "$acc"; check "wired: run_suite present" "$?" 0
grep -q -- '-R) run_stage=0' "$acc"; check "wired: -R disables the stage" "$?" 0

# fires + honest: one real run of the pinned leaf through the crossing
line=$(timeout -k 5 300 bash "$acc" -t bzip2 2>/dev/null | grep '^bzip2=')
say "    line      ${line:-<none>}"
run=$(printf '%s\n' "$line" | sed -n 's/.*,run:\([a-z-]*\).*/\1/p')
verdict=$(printf '%s\n' "$line" | sed -n 's/.*,verdict:\([a-z-]*\).*/\1/p')

case $run in
	halted|ran|passed|no-loader) say "    ok        fires: run field is '$run' (the stage launched the crossing)" ;;
	skipped|"")                  say "    FAILED    fires: run field is '${run:-<none>}'; the stage did not launch"; rc=1 ;;
	*)                           say "    FAILED    fires: unknown run state '$run'"; rc=1 ;;
esac

if [ "$verdict" = passing ] && [ "$run" != passed ]; then
	say "    FAILED    honest: verdict is passing but the suite did not pass (run=$run)"; rc=1
else
	say "    ok        honest: passing only when the suite passed (verdict=$verdict, run=$run)"
fi

[ "$rc" = 0 ] && say "run-stage: all checks passed" || say "run-stage: a check FAILED"
exit $rc
