#!/usr/bin/env bash
#
# Render transcripts from a probe-output directory brought back from another
# host. For every spike the manifest marks collectable, runs that spike's
# measure.sh in collect mode (-i) on out/<spike>.txt and writes
# spike/<spike>/results-host-<label>-<date>.txt, a differential transcript
# named by the host it came from, beside the host's own results-<date>.txt
# and outside the regeneration glob. Outputs the manifest marks as read by
# hand are copied to spike/<spike>/raw-host-<label>-<date>.txt.
#
# Usage:
#   collect.sh [options] <out-dir>
#
# Options:
#   -l LABEL, --label=LABEL The host label in the file names. [default: the # host: line, lowercased]
#   -d DATE, --date=DATE    The date in the file names. [default: today, UTC]
#   -n, --dry-run           Say what would be written; write nothing.
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as COLLECT_<OPTION>.

set -u
prog=collect
release='collect 1.0'
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../../bin/roots.sh"
label=${COLLECT_LABEL:-}
date_=${COLLECT_DATE:-$(date -u +%Y-%m-%d)}
dry=${COLLECT_DRY_RUN:-0}
quiet=${COLLECT_QUIET:-0}
usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-l|--label) label=${2:-}; shift 2 ;;
		--label=*) label=${1#*=}; shift ;;
		-d|--date) date_=${2:-}; shift 2 ;;
		--date=*) date_=${1#*=}; shift ;;
		-n|--dry-run) dry=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 1 ] || { printf '%s: takes one argument, the out directory\n' "$prog" >&2; exit 2; }
out=$1
[ -d "$out" ] || die "no directory $out"
[ -f "$out/host.txt" ] || die "$out has no host.txt; is it the bundle's out directory?"
if [ -z "$label" ]; then
	label=$(sed -n 's/^# host: //p' "$out/host.txt" | head -1 | tr -d '\r' | tr 'A-Z' 'a-z' | tr -c 'a-z0-9\n' '-')
	[ -n "$label" ] || die 'no # host: line to take a label from; pass --label'
fi
case $label in *[!a-z0-9-]*) die "label must be lowercase letters, digits and dashes: $label" ;; esac

rendered=0; copied=0; missing=0; failed=0
while IFS=$'\t' read -r dir sources libs args collect; do
	src=$out/$dir.txt
	if [ ! -s "$src" ]; then
		note "$dir: no output"; missing=$((missing + 1)); continue
	fi
	sdir=$ELFSYSVNT_ROOT/spike/$dir
	if [ "$collect" = yes ]; then
		dst=$sdir/results-host-$label-$date_.txt
		# the options the probe ran with are the options the renderer needs
		opts=$(sed -n 's/^# args://p' "$src" | head -1 | tr -d '\r')
		if [ "$dry" = 1 ]; then note "would render $dst"; continue; fi
		if (cd "$sdir" && bash measure.sh -q -i "$src" -o "$dst" $(printf '%s' "$opts" | sed 's/--\([a-z-]*\) /--\1=/g')); then
			note "rendered $dst"; rendered=$((rendered + 1))
		else
			note "$dir: measure.sh could not render $src"; failed=$((failed + 1))
		fi
	else
		dst=$sdir/raw-host-$label-$date_.txt
		if [ "$dry" = 1 ]; then note "would copy to $dst"; continue; fi
		tr -d '\r' < "$src" > "$dst" && { note "copied $dst"; copied=$((copied + 1)); }
	fi
done < <(grep -v '^#' "$here/manifest.tsv" | grep -v '^[[:space:]]*$')
if [ -s "$out/whp-corp.txt" ] && [ "$dry" != 1 ]; then
	tr -d '\r' < "$out/whp-corp.txt" > "$ELFSYSVNT_ROOT/spike/whp/results-corp-$label-$date_.txt" && copied=$((copied + 1))
fi
note "$rendered rendered, $copied copied, $missing without output, $failed failed"
[ "$failed" = 0 ]
