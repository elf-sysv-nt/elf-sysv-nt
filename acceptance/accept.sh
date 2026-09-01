#!/usr/bin/env bash
#
# WP-T4 in embryo -- take one el8 vendor package, build it against this tree,
# and report how close the runtime is to running it.
#
# The full WP-T4 in doc/IMPLEMENTATION-PLAN.md is the harness that runs over the
# whole el8 set and belongs to rhelcyg-8.10. This is its embryo: the same
# pipeline over one named leaf package, which is the overall done-when WP-56
# carries -- "a named small vendor package, built by WP-T4's harness in embryo,
# compiles, links, runs its own test suite, and passes it." It runs today and
# gives a real per-package verdict; the verdict turns green as WP-56 wires the
# libc slices the package needs.
#
# The pipeline, per package named in packages.tsv:
#   fetch    curl the pinned .src.rpm from the Rocky mirror, checked by sha256,
#            reusing a local copy whose hash still matches. curl writes to a
#            redirect rather than -o, so a Windows curl with no Cygwin curl
#            beside it does not choke on a POSIX output path.
#   unpack   rpmx.py lifts the source tarball out of the .src.rpm; it is untarred.
#   build    the package's own build, with CC set to the cross compiler. A
#            package that does not compile and link stops here with does-not-build.
#   classify the built ELF's undefined libc symbols are read and matched against
#            veneer/classification -- forwards that resolve to a runtime export,
#            shims that need a written translation, stubs with nothing behind
#            them. That match is the readiness the runtime has for this package.
#   verdict  builds; surface size; forwards, shims and stubs; and the overall
#            reading -- does-not-build, needs-wiring (with the shims and stubs
#            named), or ready. A ready package is one every symbol of which
#            forwards, and only then is running it the next thing to try.
#
# Running the package's own test suite -- the green -- needs the loader's
# dynamic-exec path to stand in for ld-linux and the runtime to resolve
# libc.so.6, which is WP-56's surface and not this harness's to force. Until a
# package reads ready, the run stage reports what it waits on rather than running.
#
# Usage: accept.sh [options] PACKAGE
#   PACKAGE   a name in packages.tsv (default: every package there)
# Options:
#   -D DIR    fetch and build under DIR       [default: /c/-/el8/accept]
#   -p FILE   the package pin                 [default: beside this script]
#   -m URL    the mirror root                 [default: Rocky 8.10]
#   -o FILE   write the report here           [default: stdout]
#   -t        terse: the key=value lines only
#   -h        this message
# Exit: 0 if every package built and was classified, 1 if one did not build.

set -u
prog=accept
release='accept 1.0'
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)

dest=${ACCEPT_DEST:-/c/-/el8/accept}
pins=$here/packages.tsv
mirror=${ACCEPT_MIRROR:-https://dl.rockylinux.org/pub/rocky/8.10}
out=-
terse=0
cross=x86_64-elfsysvnt-linux-gnu-gcc
classification=$root/veneer/classification/classification.tsv
classifier=$here/classify.awk

die() { echo "$prog: $*" >&2; exit 1; }
say() { [ "$terse" = 1 ] || echo "$@"; }

while [ $# -gt 0 ]; do
	case $1 in
		-D) dest=${2:?}; shift 2 ;;
		-p) pins=${2:?}; shift 2 ;;
		-m) mirror=${2:?}; shift 2 ;;
		-o) out=${2:?}; shift 2 ;;
		-t) terse=1; shift ;;
		-h|--help) sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		-V|--version) echo "$release"; exit 0 ;;
		-*) die "unknown option $1" ;;
		*) break ;;
	esac
done
only=$*
[ "$out" = - ] || exec > "$out"

command -v "$cross" >/dev/null 2>&1 || die "cross compiler not on PATH: $cross (add /c/-/x-elfsysvnt/bin)"
[ -r "$pins" ] || die "no package pin at $pins"
[ -r "$classification" ] || die "no classification table at $classification"

sha256() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

# The disposition each libc symbol of a built ELF carries in the veneer's
# classification: forward (buckets 1 and 2) resolves to a runtime export, shim
# (bucket 3) needs a written translation, stub (bucket 4) has nothing behind it.
classify_surface() {
	local bin=$1
	"${cross%gcc}nm" -D --undefined-only "$bin" 2>/dev/null \
	| awk '{print $NF}' | sed 's/@.*//' | sort -u > "$dest/.needs"
	awk -v filled="$filled" -v needs="$dest/.needs" -f "$classifier" \
		"$filled" "$dest/.needs" "$classification" | sort
}

fetch() {
	local relpath=$1 want=$2 file=$3
	if [ -f "$file" ] && [ "$(sha256 "$file")" = "$want" ]; then return 0; fi
	curl -fsS --retry 3 --retry-delay 2 "$mirror/$relpath" > "$file" 2>/dev/null \
		|| { rm -f "$file"; return 1; }
	[ "$(sha256 "$file")" = "$want" ] || { rm -f "$file"; return 2; }
}

pass=0; fail=0
say "# WP-T4 acceptance (embryo) -- $(date +%F)"
say ""
mkdir -p "$dest"

# The wiring layer's filled stubs: bucket-4 names it answers with a
# synthesized, certified body (DR-0052). A filled stub is not a stub that
# fails, so the classifier counts it apart from the stubs that do.
filled=$dest/.filled
: > "$filled"
for f in "$root"/veneer/wiring/*-filled.tsv; do
	[ -e "$f" ] || continue
	awk -F'\t' '!/^#/ && $1 != "" { print $1 }' "$f" >> "$filled"
done
sort -u "$filled" -o "$filled"

while IFS=$'\t' read -r name relpath want build binary <&3; do
	case $name in ''|\#*) continue ;; esac
	[ -z "$only" ] || case " $only " in *" $name "*) ;; *) continue ;; esac

	work=$dest/$name
	rm -rf "$work"; mkdir -p "$work"
	srpm=$dest/$(basename "$relpath")

	if ! fetch "$relpath" "$want" "$srpm"; then
		printf '%-12s does-not-build  fetch failed or checksum mismatch\n' "$name"
		fail=$((fail+1)); continue
	fi

	python3 "$root/spike/versioned-libc/rpmx.py" "$srpm" "$work/src" >/dev/null 2>&1 \
		|| { printf '%-12s does-not-build  cannot unpack the source rpm\n' "$name"; fail=$((fail+1)); continue; }
	tb=$(ls "$work"/src/*.tar.* 2>/dev/null | head -1)
	[ -n "$tb" ] || { printf '%-12s does-not-build  no source tarball in the rpm\n' "$name"; fail=$((fail+1)); continue; }
	tar -C "$work" -xf "$tb" 2>/dev/null || { printf '%-12s does-not-build  cannot untar the source\n' "$name"; fail=$((fail+1)); continue; }
	sdir=$(ls -d "$work"/*/ 2>/dev/null | grep -v '/src/$' | head -1)

	blog=$work/build.log
	if ! ( cd "$sdir" && eval "${build//%CC%/$cross}" ) >"$blog" 2>&1; then
		printf '%-12s does-not-build  the cross build failed (see %s)\n' "$name" "$blog"
		fail=$((fail+1)); continue
	fi
	bin=$sdir/$binary
	[ -x "$bin" ] || bin=$(find "$sdir" -type f -name "$binary" | head -1)
	[ -n "$bin" ] && [ -f "$bin" ] || { printf '%-12s does-not-build  built, but no %s produced\n' "$name" "$binary"; fail=$((fail+1)); continue; }

	surface=$(classify_surface "$bin")
	nf=$(printf '%s\n' "$surface" | grep -c '^forward ')
	ns=$(printf '%s\n' "$surface" | grep -c '^shim ')
	nb=$(printf '%s\n' "$surface" | grep -c '^stub ')
	nfill=$(printf '%s\n' "$surface" | grep -c '^filled ')
	nu=$(printf '%s\n' "$surface" | grep -c '^unclassified ')
	total=$((nf+ns+nb+nfill+nu))

	if [ "$ns" = 0 ] && [ "$nb" = 0 ] && [ "$nu" = 0 ]; then
		verdict="ready"
	else
		verdict="needs-wiring"
	fi

	printf '%-12s %-13s builds; %d libc symbols: %d forward, %d shim, %d stub%s%s\n' \
		"$name" "$verdict" "$total" "$nf" "$ns" "$nb" \
		"$([ "$nfill" -gt 0 ] && echo ", $nfill filled")" \
		"$([ "$nu" -gt 0 ] && echo ", $nu unclassified")"
	if [ "$terse" != 1 ]; then
		[ "$ns" -gt 0 ] && { echo "    shims needed (a runtime export exists; the ABI differs):"; printf '%s\n' "$surface" | awk '$1=="shim"{print "      "$2}'; }
		[ "$nb" -gt 0 ] && { echo "    stubs (nothing behind them yet):"; printf '%s\n' "$surface" | awk '$1=="stub"{print "      "$2}'; }
		[ "$nfill" -gt 0 ] && { echo "    filled (a synthesized, certified body stands behind them -- DR-0052):"; printf '%s\n' "$surface" | awk '$1=="filled"{print "      "$2}'; }
		[ "$nu" -gt 0 ] && { echo "    unclassified (not in the veneer map):"; printf '%s\n' "$surface" | awk '$1=="unclassified"{print "      "$2}'; }
		if [ "$verdict" = ready ]; then
			echo "    every symbol forwards; running its test suite is the next step (needs the loader's dynamic-exec path)."
		else
			echo "    waits on WP-56 to wire these slices; it links and loads, and runs once the shims are written and the stubs filled."
		fi
	fi
	printf '%s=surface:%d,forward:%d,shim:%d,stub:%d,filled:%d,unclassified:%d,verdict:%s\n' \
		"$name" "$total" "$nf" "$ns" "$nb" "$nfill" "$nu" "$verdict"
	pass=$((pass+1))
done 3< "$pins"

say ""
say "$prog: $pass classified, $fail did not build"
[ "$fail" = 0 ]
