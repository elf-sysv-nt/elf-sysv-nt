#!/usr/bin/env bash
#
# Stream an el8 source repository past the vendor-field probe.
#
# The count in count-vendor-misses.sh wants every package in the el8 set, and
# the el8 set is a great deal of source. Holding it is unnecessary: each
# package is read once, by two greps and a handful of config.sub invocations,
# and is of no interest afterward. So this fetches one .src.rpm at a time,
# unpacks it, hands the result to count-vendor-misses.sh, keeps the few
# hundred bytes of raw probe that come back, and deletes the rest before
# moving to the next name.
#
# Peak disk is one unpacked package. Bandwidth is not saved by streaming,
# because an SRPM cannot be opened halfway; what resumption buys is that a
# drop at package nine hundred costs one package rather than a run.
#
# Bandwidth is saved by taking one build per source name. The four Rocky 8.10
# source repositories carry 5816 builds of 2893 names -- 73 of kernel, 35 of
# firefox, an accumulation of updates and module streams -- which is 174 GB
# against 25 GB for the newest build of each name. The probe reads config.sub
# out of an upstream tarball, and 73 kernel builds carry one upstream tarball
# between them, so the other 72 are 60 GB of transfer for a byte-identical
# answer. --all-versions is there for anyone who wants to test that claim
# rather than take it.
#
# --extract decides how much of each tarball reaches disk, and it is a real
# trade rather than a tuning knob. host-tests takes config.sub, config.guess,
# configure, configure.ac, configure.in and the m4, which is everywhere the
# probe's first half looks and most of where its second half looks. all takes
# the tarball entire, which is the only scope under which the literal-vendor
# grep sees a hand-written host test in some other file.
#
# The default is host-tests because all is not affordable: 389-ds-base alone
# unpacks 49 MB of SRPM into 521 MB across a vendored node_modules and a cargo
# cache, and the grep over that had not finished in ten minutes on this host.
# 2893 packages at anything near that is a week. The narrowing is named in
# every transcript as extract_scope, because a measurement whose scope shrank
# without saying so is worse than no measurement. To price the narrowing, run
# a few dozen packages both ways and compare the literal counts.
#
# The probe itself lives in count-vendor-misses.sh and is not reimplemented
# here. This stages each package as one directory under a root and calls that
# script with --keep-dump, which is the shape it already walks.
#
# Usage:
#   fetch-host-tests.sh [options]
#
# Options:
#   -D DIR, --dest=DIR         Fragments, markers and manifests land here.
#   -r URL, --repo=URL         Source repository base; repeatable. Defaults to
#                              the four Rocky 8.10 source trees.
#   -a FILE, --aggregate=FILE  Concatenated probe dump. [default: DEST/dump]
#   -p FILE, --probe=FILE      The probe script. [default: beside this one]
#   -L N, --limit=N            Stop after N packages; 0 is no limit. [default: 0]
#   -o FILE, --output=FILE     Report destination; - is stdout. [default: -]
#   -1, --one-per-name         Newest build of each source name. The default.
#       --all-versions         Every build, including 73 kernels.
#   -e WHAT, --extract=WHAT    all, or host-tests. [default: host-tests]
#   -n, --dry-run              Read metadata, price the transfer, fetch nothing.
#       --no-dry-run           Act. This is the default.
#   -f, --force                Re-harvest packages already done.
#       --no-force             Skip what is done. This is the default.
#   -k, --keep-srpm            Keep each .src.rpm under DEST/srpm.
#       --no-keep-srpm         Delete it after unpacking. This is the default.
#   -t, --terse                The summary block alone, one key=value per line.
#   -q, --quiet                Errors only.
#   -v, --verbose              Name every package as it is handled.
#   -d, --debug                Trace execution; implies --verbose.
#   -V, --version              Print the version and exit.
#   -h, --help                 Print this message and exit.
#
# Each option is also settable as FETCH_HOST_TESTS_<OPTION>, and the option
# wins over the variable. FETCH_HOST_TESTS_REPO holds a space-separated list.
#
# Exit codes: 0 success, 1 failure, 2 usage error, 3 some packages failed and
# the rest were harvested.

set -u

prog=fetch-host-tests
release='fetch-host-tests 1.1'

rocky=https://dl.rockylinux.org/pub/rocky/8.10
default_repos="$rocky/BaseOS/source/tree $rocky/AppStream/source/tree $rocky/PowerTools/source/tree $rocky/extras/source/tree"

tab=$(printf '\t')

dest=${FETCH_HOST_TESTS_DEST:-}
repos=${FETCH_HOST_TESTS_REPO:-}
aggregate=${FETCH_HOST_TESTS_AGGREGATE:-}
probe=${FETCH_HOST_TESTS_PROBE:-}
limit=${FETCH_HOST_TESTS_LIMIT:-0}
output=${FETCH_HOST_TESTS_OUTPUT:--}
onename=${FETCH_HOST_TESTS_ONE_PER_NAME:-1}
extract=${FETCH_HOST_TESTS_EXTRACT:-host-tests}
dryrun=${FETCH_HOST_TESTS_DRY_RUN:-0}
force=${FETCH_HOST_TESTS_FORCE:-0}
keepsrpm=${FETCH_HOST_TESTS_KEEP_SRPM:-0}
terse=${FETCH_HOST_TESTS_TERSE:-0}
quiet=${FETCH_HOST_TESTS_QUIET:-0}
verbose=${FETCH_HOST_TESTS_VERBOSE:-0}
debug=${FETCH_HOST_TESTS_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

chat() { [ "$verbose" -gt 0 ] && note "$*"; return 0; }

repo_given=0
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)        usage; exit 0 ;;
		-V|--version)     printf '%s\n' "$release"; exit 0 ;;
		-D|--dest)        dest=${2:-}; shift 2 ;;
		--dest=*)         dest=${1#*=}; shift ;;
		-r|--repo)
			[ "$repo_given" = 1 ] || repos=
			repos="$repos ${2:-}"; repo_given=1; shift 2 ;;
		--repo=*)
			[ "$repo_given" = 1 ] || repos=
			repos="$repos ${1#*=}"; repo_given=1; shift ;;
		-a|--aggregate)   aggregate=${2:-}; shift 2 ;;
		--aggregate=*)    aggregate=${1#*=}; shift ;;
		-p|--probe)       probe=${2:-}; shift 2 ;;
		--probe=*)        probe=${1#*=}; shift ;;
		-L|--limit)       limit=${2:-}; shift 2 ;;
		--limit=*)        limit=${1#*=}; shift ;;
		-o|--output)      output=${2:-}; shift 2 ;;
		--output=*)       output=${1#*=}; shift ;;
		-1|--one-per-name) onename=1; shift ;;
		--all-versions)   onename=0; shift ;;
		-e|--extract)     extract=${2:-}; shift 2 ;;
		--extract=*)      extract=${1#*=}; shift ;;
		-n|--dry-run)     dryrun=1; shift ;;
		--no-dry-run)     dryrun=0; shift ;;
		-f|--force)       force=1; shift ;;
		--no-force)       force=0; shift ;;
		-k|--keep-srpm)   keepsrpm=1; shift ;;
		--no-keep-srpm)   keepsrpm=0; shift ;;
		-t|--terse)       terse=1; shift ;;
		-q|--quiet)       quiet=1; shift ;;
		-v|--verbose)     verbose=$((verbose + 1)); shift ;;
		-d|--debug)       debug=1; verbose=$((verbose + 1)); shift ;;
		--)               shift; break ;;
		-?*)              printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)                break ;;
	esac
done

[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

case $limit in
	'' | *[!0-9]*) printf '%s: --limit wants a number, got %s\n' "$prog" "$limit" >&2; exit 2 ;;
esac

case $extract in
	all | host-tests) ;;
	*) printf '%s: --extract wants all or host-tests, got %s\n' "$prog" "$extract" >&2; exit 2 ;;
esac

[ -n "$dest" ] || {
	printf '%s: no destination. Give --dest DIR or set FETCH_HOST_TESTS_DEST.\n' "$prog" >&2
	exit 2
}

[ -n "$repos" ] || repos=$default_repos
[ -n "$aggregate" ] || aggregate=$dest/dump
[ -n "$probe" ] || probe=$(dirname "$0")/count-vendor-misses.sh

[ -x "$probe" ] || die "no probe script at $probe"

for tool in curl rpm2cpio cpio gzip tar sha256sum; do
	command -v "$tool" >/dev/null 2>&1 || die "no $tool on PATH"
done

[ "$debug" = 1 ] && set -x

mkdir -p "$dest/frag" "$dest/done" "$dest/manifest" || die "cannot prepare $dest"
[ "$keepsrpm" = 1 ] && { mkdir -p "$dest/srpm" || die "cannot prepare $dest/srpm"; }

# One run at a time against a destination. Not a nicety: two instances race
# on the markers and on the aggregate, and the reap below would delete a live
# run's working directory out from under it. A lock whose pid is gone is
# stale and gets taken; a lock whose pid answers is refused.
lock=$dest/lock
if [ -e "$lock" ]; then
	holder=$(cat "$lock" 2>/dev/null)
	case $holder in
		'' | *[!0-9]*) note "ignoring an unreadable lock at $lock" ;;
		*) kill -0 "$holder" 2>/dev/null &&
			die "another run holds $lock, pid $holder" ;;
	esac
fi
printf '%s\n' "$$" > "$lock" || die "cannot take the lock at $lock"

# A run that died leaves its working directory behind. Reaping here rather
# than trusting the trap is the difference between a resumable fetcher and one
# that fills a disk over a fortnight of interrupted runs. Safe only because
# the lock above proves no other run owns one.
rm -rf "$dest"/work.* 2>/dev/null

work=$(mktemp -d "$dest/work.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"; rm -f "$lock"' EXIT
trap 'rm -rf "$work"; rm -f "$lock"; exit 130' INT TERM

# --connect-timeout because curl's default is 300 seconds and three retries of
# that is fifteen minutes of looking like a slow mirror rather than an
# unreachable one. No --max-time: some of these packages are a gigabyte.
fetch() {
	curl -fsSL --connect-timeout 20 --retry 3 --retry-delay 2 -o "$2" "$1"
}

slug_of() {
	printf '%s\n' "$1" | sed -e 's,/source/tree/*$,,' -e 's,.*/,,'
}

# One record per build out of primary.xml, as name, epoch:version-release,
# href, size, checksum. RS on the opening tag rather than on a newline,
# because createrepo has been through several generations of pretty-printer
# and the line breaks are not a contract.
index_primary() {
	gzip -dc "$1" | awk '
		function attr(s, k,   p) {
			if (match(s, k "=\"[^\"]*\"")) {
				p = substr(s, RSTART + length(k) + 2, RLENGTH - length(k) - 3)
				return p
			}
			return ""
		}
		BEGIN { RS = "<package "; OFS = "\t" }
		NR == 1 { next }
		{
			name = ""; href = ""; size = "0"; sum = ""; evr = "0:0-0"
			if (match($0, /<name>[^<]*<\/name>/))
				name = substr($0, RSTART + 6, RLENGTH - 13)
			if (match($0, /<location href="[^"]*"/))
				href = substr($0, RSTART + 16, RLENGTH - 17)
			if (match($0, /<size package="[0-9]+"/))
				size = substr($0, RSTART + 15, RLENGTH - 16)
			if (match($0, /<checksum type="sha256"[^>]*>[0-9a-f]+/)) {
				sum = substr($0, RSTART, RLENGTH)
				sub(/^.*>/, "", sum)
			}
			if (match($0, /<version [^>]*\/>/)) {
				v = substr($0, RSTART, RLENGTH)
				evr = attr(v, "epoch") ":" attr(v, "ver") "-" attr(v, "rel")
			}
			if (name != "" && href != "")
				print name, evr, href, size, sum
		}
	' | LC_ALL=C sort -k1,1
}

index_repo() {
	url=$1 slug=$2
	md=$dest/manifest/$slug-repomd.xml
	fetch "$url/repodata/repomd.xml" "$md" || die "cannot read the metadata of $url"
	href=$(awk '
		BEGIN { RS = "<data " }
		/^type="primary"/ {
			if (match($0, /<location href="[^"]*"/))
				print substr($0, RSTART + 16, RLENGTH - 17)
		}
	' "$md" | head -n 1)
	[ -n "$href" ] || die "no primary metadata named in $md"
	fetch "$url/$href" "$work/primary.gz" || die "cannot fetch $url/$href"
	index_primary "$work/primary.gz" > "$dest/manifest/$slug-index.tsv" ||
		die "cannot index $slug"
	rm -f "$work/primary.gz"
}

# The newest build of each source name, across every repository at once,
# because a name can appear in two of them. Version ordering is sort -V over
# epoch:version-release, which is not rpm's own comparison: it disagrees on
# tildes and on a few epoch shapes. Immaterial here, since the two candidates
# it might confuse are two builds of one upstream tarball, and the tarball is
# what gets read.
build_selection() {
	: > "$work/all"
	for u in $repos; do
		s=$(slug_of "$u")
		awk -F "$tab" -v url="$u" 'BEGIN { OFS = FS } { print $1, $2, url, $3, $4, $5 }' \
			"$dest/manifest/$s-index.tsv" >> "$work/all"
	done
	if [ "$onename" = 1 ]; then
		LC_ALL=C sort -t "$tab" -k1,1 -k2,2V "$work/all" |
			awk -F "$tab" '{ keep[$1] = $0 } END { for (n in keep) print keep[n] }' |
			LC_ALL=C sort -t "$tab" -k1,1 > "$dest/manifest/selected.tsv"
	else
		LC_ALL=C sort -t "$tab" -k1,1 -k2,2V "$work/all" > "$dest/manifest/selected.tsv"
	fi
}

# Everything a package costs, taken and given back inside one call: the
# .src.rpm, whatever archives it carries, and the tree they unpack into. What
# survives is the fragment and the marker beside it. The key is the .src.rpm
# basename rather than the source name, so that --all-versions can tell two
# builds apart; the staging directory keeps the source name, because that is
# what the probe prints as the package.
harvest_one() {
	url=$1 name=$2 href=$3 sum=$4 key=$5
	pkg=$work/pkg
	rm -rf "$pkg"
	mkdir -p "$pkg/unpack" "$pkg/stage/$name" || return 1

	rpmfile=$pkg/$(basename "$href")
	fetch "$url/$href" "$rpmfile" || { note "fetch failed: $key"; return 1; }

	if [ -n "$sum" ]; then
		got=$(sha256sum < "$rpmfile" | cut -d' ' -f1)
		[ "$got" = "$sum" ] || { note "checksum mismatch: $key"; return 1; }
	fi

	( cd "$pkg/unpack" && rpm2cpio "$rpmfile" | cpio -idmu --quiet ) ||
		{ note "cannot unpack: $key"; return 1; }

	# Archives only. Patches, specs and loose sources stay where cpio put
	# them and are never read; config.sub lives inside the upstream tarball.
	# A tarball carrying none of the wanted members makes tar exit non-zero,
	# which is ordinary here rather than a failure.
	find "$pkg/unpack" -maxdepth 1 -type f -print > "$pkg/files"
	while read -r f; do
		case $f in
			*.tar|*.tar.gz|*.tgz|*.tar.bz2|*.tbz|*.tbz2|*.tar.xz|*.txz|*.tar.lz|*.tar.Z)
				if [ "$extract" = all ]; then
					tar -C "$pkg/stage/$name" -xf "$f" 2>/dev/null
				else
					tar -C "$pkg/stage/$name" -xf "$f" --wildcards --no-anchored \
						config.sub config.guess configure configure.ac \
						configure.in '*.m4' 2>/dev/null
				fi ;;
			*.zip)
				if [ "$extract" = all ]; then
					unzip -qq -o -d "$pkg/stage/$name" "$f" 2>/dev/null
				else
					unzip -qq -o -d "$pkg/stage/$name" "$f" \
						'*/config.sub' '*/config.guess' '*/configure' \
						'*/configure.ac' '*/configure.in' '*/*.m4' 2>/dev/null
				fi ;;
		esac
		true
	done < "$pkg/files"

	"$probe" --root "$pkg/stage" --keep-dump "$pkg/frag" --output /dev/null --quiet ||
		{ note "the probe failed on $key"; return 1; }
	[ -s "$pkg/frag" ] || { note "the probe said nothing about $key"; return 1; }

	cat "$pkg/frag" > "$dest/frag/$key.dump" || return 1
	[ "$keepsrpm" = 1 ] && mv -f "$rpmfile" "$dest/srpm/"
	: > "$dest/done/$key"
	rm -rf "$pkg"
	return 0
}

indexed=0
bytes=0
seen=0
did=0
skipped=0
failed=0

for url in $repos; do
	slug=$(slug_of "$url")
	note "indexing $slug"
	index_repo "$url" "$slug"
	idx=$dest/manifest/$slug-index.tsv
	n=$(wc -l < "$idx")
	b=$(awk -F "$tab" '{ s += $4 } END { printf "%.0f\n", s + 0 }' "$idx")
	indexed=$((indexed + n))
	bytes=$((bytes + b))
	chat "$slug: $n builds, $b bytes"
	printf '%s\t%s\t%s\n' "$slug" "$n" "$b" >> "$work/repos"
done

build_selection
selected=$(wc -l < "$dest/manifest/selected.tsv")
selbytes=$(awk -F "$tab" '{ s += $5 } END { printf "%.0f\n", s + 0 }' "$dest/manifest/selected.tsv")

if [ "$dryrun" = 0 ]; then
	note "harvesting $selected packages"
	while IFS="$tab" read -r name evr url href size sum; do
		[ -n "$name" ] || continue
		seen=$((seen + 1))
		key=$(basename "$href")
		if [ -e "$dest/done/$key" ] && [ "$force" = 0 ]; then
			skipped=$((skipped + 1))
			chat "skip $name"
		elif harvest_one "$url" "$name" "$href" "$sum" "$key" < /dev/null; then
			did=$((did + 1))
			chat "done $name"
		else
			failed=$((failed + 1))
		fi
		if [ "$limit" -gt 0 ] && [ $((did + skipped)) -ge "$limit" ]; then
			break
		fi
	done < "$dest/manifest/selected.tsv"

	find "$dest/frag" -name '*.dump' -type f -print > "$work/frags"
	LC_ALL=C sort "$work/frags" > "$work/frags.sorted"
	: > "$work/aggregate"
	while read -r f; do cat "$f" >> "$work/aggregate"; done < "$work/frags.sorted"
	cat "$work/aggregate" > "$aggregate" || die "cannot write $aggregate"
	note "aggregate dump in $aggregate"
fi

summary=$work/summary
{
	printf 'repositories=%s\n' "$(wc -l < "$work/repos" | tr -d ' ')"
	printf 'builds_indexed=%s\n' "$indexed"
	printf 'bytes_indexed=%s\n' "$bytes"
	printf 'packages_selected=%s\n' "$selected"
	printf 'bytes_selected=%s\n' "$selbytes"
	printf 'one_per_name=%s\n' "$onename"
	printf 'extract_scope=%s\n' "$extract"
	printf 'packages_considered=%s\n' "$seen"
	printf 'packages_harvested=%s\n' "$did"
	printf 'packages_skipped=%s\n' "$skipped"
	printf 'packages_failed=%s\n' "$failed"
	printf 'dry_run=%s\n' "$dryrun"
} > "$summary"

if [ "$terse" = 1 ]; then
	cat "$summary" > "$work/report"
else
	{
		printf 'el8 source streamed past the vendor-field probe\n\n'
		printf 'host         %s\n' "$(hostname 2>/dev/null)"
		printf 'kernel       %s\n' "$(uname -srm)"
		printf 'date         %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf 'script       %s\n' "$release"
		printf 'destination  %s\n' "$dest"
		printf 'aggregate    %s\n' "$aggregate"
		printf 'extracted    %s\n' "$extract"
		[ "$dryrun" = 1 ] && printf 'mode         dry run, nothing fetched beyond metadata\n'
		printf '\n== repositories, every build indexed\n\n'
		awk -F "$tab" '{ printf "    %-14s %8d builds %16d bytes\n", $1, $2, $3 }' "$work/repos"
		printf '\n== what a run would transfer\n\n'
		printf '    builds indexed     %8d %16d bytes\n' "$indexed" "$bytes"
		printf '    packages selected  %8d %16d bytes\n' "$selected" "$selbytes"
		if [ "$onename" = 1 ]; then
			printf '\n    One build per source name. The rest are updates and module\n'
			printf '    streams over the same upstream tarball, which is the file the\n'
			printf '    probe reads. --all-versions takes them all.\n'
		fi
		if [ "$extract" = host-tests ]; then
			printf '\n    Only config.sub, config.guess, configure, configure.ac,\n'
			printf '    configure.in and the m4 were extracted. The literal-vendor\n'
			printf '    count below is therefore narrower than a walk of whole\n'
			printf '    source trees would give. --extract all is the wider scope.\n'
		fi
		printf '\n== summary\n\n'
		sed -e 's/^/    /' "$summary"
	} > "$work/report"
fi

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "report written to $output"
fi

[ "$failed" -eq 0 ] || exit 3
exit 0
