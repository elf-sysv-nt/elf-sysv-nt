#!/usr/bin/env bash
#
# WP-51's certification: the committed version map reproduces from the pinned
# vendor binaries, and it agrees with readelf on what those binaries carry.
#
# Four things are checked, and each is a pass or a fail rather than a reading.
#
#   fetch       The pinned glibc, libnsl and libxcrypt are fetched (or reused
#               from the cache) and unpacked. Offline with no cache, the test
#               skips with 77 rather than inventing an answer, the way WP-50's
#               header diff does.
#   reproduce   The extractor is rerun over that tree and its map and node
#               ladders are diffed against the committed files. A byte of
#               difference fails.
#   ladder      libc.so.6's committed ladder is exactly the 29 non-base nodes
#               GLIBC_2.2.5 through GLIBC_2.28 and GLIBC_PRIVATE, and it equals
#               what readelf -V prints for the vendor file. memcpy carries the
#               vendor's binding: memcpy@@GLIBC_2.14 default and
#               memcpy@GLIBC_2.2.5 hidden.
#   crosscheck  Every defined dynamic symbol the extractor recorded for
#               libc.so.6 matches, line for line, what readelf --dyn-syms
#               prints. readelf is a second parser, so this is a real check on
#               the first rather than a restatement of it.
#
# Usage:
#   reproduce.sh [options]
#
# Options:
#   -D DIR, --dest=DIR   Vendor cache; fetched and unpacked here, reused when
#                        the checksums still match. [default: /c/-/el8/wp51-cache]
#   --tree=DIR           Use an already-unpacked tree at DIR and do not fetch.
#   -q, --quiet          Errors only.
#   -h, --help           Print this message and exit.
#
# Exit: 0 reproduces and matches, 1 differs, 2 usage, 77 cannot obtain the
# vendor binaries offline.

set -u

prog=reproduce
here=$(cd "$(dirname "$0")" && pwd)
vm=$(cd "$here/.." && pwd)

dest=/c/-/el8/wp51-cache
tree=
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	opt=$1; shift
	val=; case $opt in --*=*) val=${opt#*=}; opt=${opt%%=*} ;; esac
	case $opt in
		-D|--dest) dest=${val:-$1}; [ -n "$val" ] || shift ;;
		--tree) tree=${val:-$1}; [ -n "$val" ] || shift ;;
		-q|--quiet) quiet=1 ;;
		-h|--help) usage; exit 0 ;;
		--) break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$opt" >&2; exit 2 ;;
		*) break ;;
	esac
done

smon=/c/-/repo/session-monitor/lib/smon.sh
[ -f "$smon" ] && . "$smon"
type smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_step_skip() { :; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

SMON_TRAP=0
smon_session build wp51-version-map
smon_plan fetch reproduce ladder crosscheck

committed_map=$vm/glibc-version-map.tsv
committed_nodes=$vm/glibc-version-nodes.tsv
extractor=$vm/extract-version-map.py
[ -f "$committed_map" ] || fail "no committed map at $committed_map"
[ -f "$committed_nodes" ] || fail "no committed node ladder at $committed_nodes"

readelf=$(command -v x86_64-elfsysvnt-linux-gnu-readelf 2>/dev/null \
	|| command -v readelf 2>/dev/null)
[ -n "$readelf" ] || fail "no readelf on PATH"

# --- fetch -------------------------------------------------------------------
smon_step_start fetch
if [ -n "$tree" ]; then
	[ -d "$tree" ] || fail "--tree $tree does not exist"
	say "using pre-unpacked tree $tree"
else
	if bash "$vm/fetch-vendor.sh" --dest "$dest" ${quiet:+-q} >/dev/null 2>&1
	then
		tree=$dest
	else
		smon_step_skip fetch
		smon_item WP-51 partial "vendor binaries unavailable offline; skipped"
		smon_end 77
		say "cannot fetch or find a cache; skipping (77)"
		exit 77
	fi
fi
libc=$tree/glibc/usr/lib64/libc-2.28.so
[ -f "$libc" ] || { smon_step_fail fetch 1; fail "no libc-2.28.so under $tree"; }
smon_step_ok fetch

tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

# --- reproduce ---------------------------------------------------------------
smon_step_start reproduce
python3 "$extractor" --tree "$tree" \
	--map "$tmp/map.tsv" --nodes "$tmp/nodes.tsv" \
	|| { smon_step_fail reproduce 1; fail "the extractor failed"; }
ok=1
if ! diff -u "$committed_map" "$tmp/map.tsv" >"$tmp/map.diff" 2>&1; then
	ok=0; printf '%s: committed map differs from a fresh extraction:\n' "$prog" >&2
	head -40 "$tmp/map.diff" >&2
fi
if ! diff -u "$committed_nodes" "$tmp/nodes.tsv" >"$tmp/nodes.diff" 2>&1; then
	ok=0; printf '%s: committed node ladder differs:\n' "$prog" >&2
	head -40 "$tmp/nodes.diff" >&2
fi
[ "$ok" = 1 ] || { smon_step_fail reproduce 1; exit 1; }
say "reproduces: $(wc -l < "$committed_map" | tr -d ' ') map rows, $(wc -l < "$committed_nodes" | tr -d ' ') node rows"
smon_step_ok reproduce

# --- ladder ------------------------------------------------------------------
smon_step_start ladder
expected="GLIBC_2.10 GLIBC_2.11 GLIBC_2.12 GLIBC_2.13 GLIBC_2.14 GLIBC_2.15 \
GLIBC_2.16 GLIBC_2.17 GLIBC_2.18 GLIBC_2.2.5 GLIBC_2.2.6 GLIBC_2.22 GLIBC_2.23 \
GLIBC_2.24 GLIBC_2.25 GLIBC_2.26 GLIBC_2.27 GLIBC_2.28 GLIBC_2.3 GLIBC_2.3.2 \
GLIBC_2.3.3 GLIBC_2.3.4 GLIBC_2.4 GLIBC_2.5 GLIBC_2.6 GLIBC_2.7 GLIBC_2.8 \
GLIBC_2.9 GLIBC_PRIVATE"
expected=$(printf '%s\n' $expected | sort)
ours=$(awk -F'\t' '$1=="libc.so.6" && $4!="base"{print $3}' "$committed_nodes" | sort)
ref=$("$readelf" -V "$libc" | awk '
	/Version definition section/{f=1;next} /Version needs section/{f=0}
	f{for(i=1;i<=NF;i++) if($i=="Name:") print $(i+1)}' \
	| grep -v '^libc.so.6$' | sort -u)
[ "$ours" = "$expected" ] || { smon_step_fail ladder 1
	fail "committed ladder is not the 29-node GLIBC_2.2.5..2.28 + PRIVATE set"; }
[ "$ours" = "$ref" ] || { smon_step_fail ladder 1
	fail "committed ladder does not equal readelf -V for the vendor libc.so.6"; }
mc=$(awk -F'\t' '$1=="libc.so.6" && $2=="memcpy"{print $3"/"$4}' "$committed_map" | sort | tr '\n' ' ')
[ "$mc" = "GLIBC_2.14/default GLIBC_2.2.5/hidden " ] || { smon_step_fail ladder 1
	fail "memcpy is not bound @@GLIBC_2.14 default and @GLIBC_2.2.5 hidden: [$mc]"; }
say "ladder: 29 nodes, equal to readelf -V; memcpy@@GLIBC_2.14 default, memcpy@GLIBC_2.2.5 hidden"
smon_step_ok ladder

# --- crosscheck --------------------------------------------------------------
smon_step_start crosscheck
awk -F'\t' '$1=="libc.so.6"{
	if($3=="-"||$2==$3) print $2
	else if($4=="default") print $2"@@"$3
	else print $2"@"$3 }' "$committed_map" | sort > "$tmp/ours.txt"
"$readelf" --dyn-syms -W "$libc" | awk '
	NR>3 && $1 ~ /:$/ { if($7=="UND"||$8=="") next; print $8 }' \
	| sort > "$tmp/ref.txt"
if diff -u "$tmp/ours.txt" "$tmp/ref.txt" >"$tmp/xc.diff" 2>&1; then
	say "crosscheck: all $(wc -l < "$tmp/ours.txt" | tr -d ' ') defined dynsyms match readelf --dyn-syms"
	smon_step_ok crosscheck
else
	smon_step_fail crosscheck 1
	printf '%s: map disagrees with readelf --dyn-syms:\n' "$prog" >&2
	head -40 "$tmp/xc.diff" >&2
	exit 1
fi

smon_item WP-51 met "version map reproduces and matches readelf on the vendor libc.so.6"
smon_end 0
say "all four checks passed"
exit 0
