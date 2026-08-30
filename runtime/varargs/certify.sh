#!/usr/bin/env bash
#
# WP-24 certification under session-monitor.
#
# Sources smon.sh and runs the four certification gates as monitored steps,
# then attests the done-condition as a roadmap item. With no .smon directory
# above the tree every smon call is a no-op, so this also just runs the gates.
#
# Usage: certify.sh [--cc=CC]

set -u
here=$(cd "$(dirname "$0")" && pwd)
smon=/c/-/repo/session-monitor/lib/smon.sh
cc=${CC:-gcc}
case ${1:-} in --cc) cc=${2:-gcc} ;; --cc=*) cc=${1#*=} ;; esac

# shellcheck disable=SC1090
[ -f "$smon" ] && . "$smon" || { smon_session() { :; }; smon_plan() { :; }
	smon_step_start() { :; }; smon_step_ok() { :; }; smon_step_fail() { :; }
	smon_cmd() { shift 0; "$@"; }; smon_item() { :; }; smon_doc() { :; }
	smon_note() { :; }; smon_end() { :; }; }

export SMON_REF=${SMON_REF:-$(git -C "$here" rev-parse --short HEAD 2>/dev/null)}

smon_session build wp24-varargs
smon_plan derive-set reproduce-veneer compile done-condition
smon_doc "runtime/varargs/README.md" "WP-24 varargs README"

fail=0
run() {
	step=$1; shift
	smon_step_start "$step"
	if smon_cmd "$@"; then
		smon_step_ok "$step"
	else
		rc=$?
		smon_step_fail "$step" "$rc"
		fail=1
	fi
}

run derive-set       bash "$here/derive-variadic.sh" -q
run reproduce-veneer bash "$here/gen-veneer.sh" --c /tmp/wp24-vc.$$ --h /tmp/wp24-vh.$$
# gate: the regeneration matches the committed artifacts
smon_step_start compare
if diff -q "$here/veneer.gen.c" /tmp/wp24-vc.$$ >/dev/null 2>&1 && \
   diff -q "$here/veneer.gen.h" /tmp/wp24-vh.$$ >/dev/null 2>&1; then
	smon_step_ok compare
else
	smon_step_fail compare 1; fail=1
fi
rm -f /tmp/wp24-vc.$$ /tmp/wp24-vh.$$
run done-condition   bash "$here/t/reproduce.sh" --cc "$cc" -q

if [ "$fail" = 0 ]; then
	smon_item WP-24 met "variadic surface: 68 exports enumerated, veneer reproduces, sixteen-argument printf matches glibc, vfprintf crosses a System V va_list"
else
	smon_item WP-24 partial "a certification gate failed; see the failed step"
fi
smon_end "$fail"
exit "$fail"
