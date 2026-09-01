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
#            wired shims whose translation a crossed slice has written and
#            certified, shims still to write, and stubs with nothing behind
#            them, save the stubs the wiring layer fills with a synthesized,
#            certified body (DR-0052). That match is the readiness the runtime
#            has for this package.
#   verdict  builds; surface size; forwards, shims, stubs and filled bodies;
#            and the overall
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
#   -R        do not run a ready package's suite through the crossing
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
run_stage=1
cross=x86_64-elfsysvnt-linux-gnu-gcc
host=${CC:-gcc}
classification=$root/veneer/classification/classification.tsv
classifier=$here/classify.awk
shape_src=$here/t/img_shape.c
shape_bin=

die() { echo "$prog: $*" >&2; exit 1; }
say() { [ "$terse" = 1 ] || echo "$@"; }

while [ $# -gt 0 ]; do
	case $1 in
		-D) dest=${2:?}; shift 2 ;;
		-p) pins=${2:?}; shift 2 ;;
		-m) mirror=${2:?}; shift 2 ;;
		-o) out=${2:?}; shift 2 ;;
		-t) terse=1; shift ;;
		-R) run_stage=0; shift ;;
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
# A bucket-4 stub the wiring layer's filled manifest names is a filled stub --
# a synthesized, certified body stands behind it (DR-0052), so classify.awk
# reports it apart from the stubs that only fail.
classify_surface() {
	local bin=$1
	"${cross%gcc}nm" -D --undefined-only "$bin" 2>/dev/null \
	| awk '{print $NF}' | sed 's/@.*//' | sort -u > "$dest/.needs"
	awk -v filled="$filled" -v wired="$wired" -v needs="$dest/.needs" -f "$classifier" \
		"$filled" "$wired" "$dest/.needs" "$classification" | sort
}

# The loader's own reading of a built ELF: elf_parse() (WP-31) validates it and
# exec_kind_of() (WP-56) classifies it, so the harness gates a package on the
# same verdict the dynamic crossing driver stands behind (DR-0058) rather than
# on a second reading of our own. img_shape is built once, over the loader
# packages, with the host compiler. Prints three key=value lines: kind, interp,
# needed.
build_shape() {
	[ -n "$shape_bin" ] && return 0
	local w=$dest/.shape
	mkdir -p "$w"
	local hf="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
	$host $hf -c "$root/loader/elf/elf_parse.c"  -o "$w/elf_parse.o" 2>/dev/null || return 1
	$host $hf -c "$root/loader/exec/exec_kind.c" -o "$w/exec_kind.o" 2>/dev/null || return 1
	$host $hf -Werror -c "$shape_src"             -o "$w/img_shape.o" 2>/dev/null || return 1
	$host -o "$w/img_shape" "$w/img_shape.o" "$w/elf_parse.o" "$w/exec_kind.o" 2>/dev/null || return 1
	shape_bin=$w/img_shape
}

shape_of() { "$shape_bin" "$1" 2>/dev/null; }

# The loader's dynamic-exec front end and its PE host stub (WP-41), built once
# from the loader packages with the host compiler, exactly as loader/exec/t/
# run.sh builds them. ELFSYSV_STUB names the stub the front end starts; the
# 0x100000 stack reserve keeps the kernel's initial stack out of the ELF
# window (DR-0028). Sets loader_exec on success; a failure is reported by the
# caller and leaves the classify verdict untouched.
build_loader() {
	[ -n "${loader_exec:-}" ] && return 0
	local w=$dest/.loader e=$root/loader/exec
	mkdir -p "$w"
	local cf="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
	local ls="$e/reserve.c $root/loader/map/elf_map.c $root/loader/map/host_mem.c $root/loader/elf/elf_parse.c $root/loader/process/process_image.c $root/loader/reloc/elf_reloc.c $root/loader/reloc/reloc_resolve.S"
	$host $cf -Wl,--stack,0x100000 -o "$w/elfsysv-stub" \
		"$e/stub.c" "$e/exec_kind.c" "$e/dyn_exec.c" "$e/dyn_init.c" "$e/enter.S" $ls \
		> "$w/build.log" 2>&1 || return 1
	$host $cf -o "$w/elfsysv-exec" \
		"$e/exec_main.c" "$e/dispatch.c" "$e/binfmt.c" "$e/reserve.c" >> "$w/build.log" 2>&1 || return 1
	loader_exec="$w/elfsysv-exec"
	export ELFSYSV_STUB="$w/elfsysv-stub.exe"
	return 0
}

# Launch a ready package through the loader's crossing -- standing in for
# ld-linux -- rather than stopping at `ready`. A liveness probe enters the
# image; if it enters and exits without a loader diagnostic the package's own
# suite runs through the same crossing, its binary swapped for a wrapper that
# re-enters it, and a clean suite is the `passing` verdict WP-56's done-when
# names. Until the image enters, the stage names the obstacle it halts at --
# today the map layer, then reent/TLS and the syscall surface
# (acceptance/to-green.tsv) -- so the harness reports where the crossing stands,
# not merely that it waits. Every launch is under a hard timeout, so a loader
# that wedges cannot wedge the harness. Sets run_state and run_note.
run_suite() {
	local name=$1 sdir=$2 bin=$3 binary=$4
	run_state=""; run_note=""
	if ! build_loader; then
		run_state="no-loader"
		run_note="    run stage: the loader front end did not build (see $dest/.loader/build.log); cannot launch the crossing."
		return 0
	fi
	local w=$dest/$name probe=$dest/$name/run-probe.out
	timeout -k 3 30 "$loader_exec" "$bin" --help > "$probe" 2>&1
	local rc=$?
	local obstacle
	obstacle=$(grep -m1 -E 'elfsysv-stub:|_err|reloc' "$probe" 2>/dev/null | sed 's#^elfsysv-stub: [^:]*: ##; s#^elfsysv-stub: ##')
	# Translate a known loader refusal into the build-side fix, so the halt reads
	# as an action rather than a raw error code. The granule refusal (DR-0008) is
	# satisfied by linking the image granule-separable (DR-0061).
	local hint=
	case "$obstacle" in
		*granule*) hint=" -- fix build-side: link granule-separable, -Wl,-z,max-page-size=0x10000 or the toolchain default (DR-0061)";;
	esac
	if grep -qE 'elfsysv-stub:|_err|reloc' "$probe" 2>/dev/null || [ "$rc" -ge 128 ]; then
		run_state="halted"
		run_note="    run stage: launched $name through the crossing; it halts before entry -- ${obstacle:-terminated, exit $rc}$hint. The image does not yet run; acceptance/to-green.tsv names the ladder from here."
		return 0
	fi
	# Entered and left without a loader diagnostic: run the package's own suite
	# through the same crossing. Its binary is swapped for a wrapper that
	# re-enters it, and -o keeps make from rebuilding over the wrapper.
	local real=$bin.real
	cp -f "$bin" "$real" 2>/dev/null || { run_state="ran"; run_note="    run stage: $name entered the crossing and ran; could not stage its suite (binary not writable)."; return 0; }
	printf '#!/bin/sh\nexec "%s" "%s" "$@"\n' "$loader_exec" "$real" > "$bin"
	chmod +x "$bin"
	( cd "$sdir" && timeout -k 5 180 make -o "$binary" test ) > "$w/run-suite.out" 2>&1
	local trc=$?
	cp -f "$real" "$bin" 2>/dev/null; rm -f "$real"
	if [ "$trc" = 0 ]; then
		run_state="passed"
		run_note="    run stage: $name ran its own test suite through the crossing and passed -- WP-56's overall done-when is met."
	else
		run_state="ran"
		run_note="    run stage: $name entered the crossing and ran, but its suite did not pass (exit $trc; see $w/run-suite.out)."
	fi
	return 0
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
build_shape || die "cannot build the image-shape helper from $shape_src"

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

# The certified-shim manifest: a bucket-3 shim is written when its slice carries
# a wire-<slice>.shims.tsv, and certified when that slice carries a
# live-<slice>.sh crossing. The union of the shims named by the crossed slices
# is the set a package may lean on -- a written translation stands behind each,
# exactly as a synthesized body stands behind a filled stub. A shim whose slice
# has not been crossed is not in this set and still blocks.
wired=$dest/.wired
: > "$wired"
for live in "$root"/veneer/wiring/t/live-*.sh; do
	[ -e "$live" ] || continue
	slice=$(basename "$live"); slice=${slice#live-}; slice=${slice%.sh}
	shims=$root/veneer/wiring/wire-$slice.shims.tsv
	[ -e "$shims" ] || continue
	awk -F'\t' '!/^#/ && $1 != "" { print $1 }' "$shims" >> "$wired"
done
sort -u "$wired" -o "$wired"

# A fingerprint of the veneer's resolution inputs -- the classification map and
# the set of crossed slices -- stamped into each surface sidecar, so a reader can
# tell whether a stored surface's verdict still reflects today's veneer or has
# gone stale. bin/progress.py recomputes the same value the same way and compares.
_ch=$(sha256sum "$classification" | cut -d' ' -f1)
_cr=$(for _l in "$root"/veneer/wiring/t/live-*.sh; do [ -e "$_l" ] || continue; _b=$(basename "$_l"); _b=${_b#live-}; echo "${_b%.sh}"; done | sort | paste -sd, -)
vfp=$(printf '%s|%s' "$_ch" "$_cr" | sha256sum | cut -c1-12)

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

	shp=$(shape_of "$bin")
	skind=$(printf '%s\n' "$shp" | sed -n 's/^kind=//p')
	sinterp=$(printf '%s\n' "$shp" | sed -n 's/^interp=//p')
	sneeded=$(printf '%s\n' "$shp" | sed -n 's/^needed=//p')
	[ -n "$skind" ] || skind=unread

	surface=$(classify_surface "$bin")
	nf=$(printf '%s\n' "$surface" | grep -c '^forward ')
	nw=$(printf '%s\n' "$surface" | grep -c '^wired ')
	ns=$(printf '%s\n' "$surface" | grep -c '^shim ')
	nb=$(printf '%s\n' "$surface" | grep -c '^stub ')
	nfill=$(printf '%s\n' "$surface" | grep -c '^filled ')
	nu=$(printf '%s\n' "$surface" | grep -c '^unclassified ')
	total=$((nf+nw+ns+nb+nfill+nu))

	# The full classified surface as a committed sidecar, forwards included.
	# The results block names only the non-forwards; bin/progress.py reads this
	# to count coverage by symbol identity, not by totals alone.
	mkdir -p "$here/surface"
	{ echo "# veneer-fingerprint $vfp"; printf '%s\n' "$surface" | awk 'NF>=2{print $2"\t"$1}' | sort; } > "$here/surface/$name.tsv"

	# Two gates, in order. A package whose symbols do not all resolve waits on
	# wiring. One whose symbols resolve but whose image the loader's classifier
	# does not call dynamic is not the shape the crossing driver runs, and is
	# not ready however clean its surface -- the harness says so rather than
	# crediting a shape the runtime would refuse. Only a package that clears
	# both reads ready.
	if [ "$ns" != 0 ] || [ "$nb" != 0 ] || [ "$nu" != 0 ]; then
		verdict="needs-wiring"
	elif [ "$skind" != dynamic ]; then
		verdict="shape-mismatch"
	else
		verdict="ready"
	fi

	run_state=""; run_note=""
	if [ "$run_stage" = 1 ] && [ "$verdict" = ready ]; then
		run_suite "$name" "$sdir" "$bin" "$binary"
		[ "$run_state" = passed ] && verdict=passing
	fi

	printf '%-12s %-13s builds; %d libc symbols: %d forward, %d wired, %d shim, %d stub%s%s\n' \
		"$name" "$verdict" "$total" "$nf" "$nw" "$ns" "$nb" \
		"$([ "$nfill" -gt 0 ] && echo ", $nfill filled")" \
		"$([ "$nu" -gt 0 ] && echo ", $nu unclassified")"
	if [ "$terse" != 1 ]; then
		printf '    image shape: %s' "$skind"
		[ "$skind" = dynamic ] && printf ' (the shape the crossing driver runs -- DR-0058)'
		printf '; interp %s, needs %s\n' "${sinterp:--}" "${sneeded:--}"
		[ "$skind" != dynamic ] && [ "$skind" != unread ] && \
			echo "    the loader's classifier does not call this image dynamic, so the crossing driver would not run it; not ready whatever its surface."
		[ "$nw" -gt 0 ] && { echo "    wired (a written translation the live crossing certified stands behind them):"; printf '%s\n' "$surface" | awk '$1=="wired"{print "      "$2}'; }
		[ "$ns" -gt 0 ] && { echo "    shims still to write (a runtime export exists; the ABI differs; no crossed slice covers them):"; printf '%s\n' "$surface" | awk '$1=="shim"{print "      "$2}'; }
		[ "$nb" -gt 0 ] && { echo "    stubs (nothing behind them yet):"; printf '%s\n' "$surface" | awk '$1=="stub"{print "      "$2}'; }
		[ "$nfill" -gt 0 ] && { echo "    filled (a synthesized, certified body stands behind them -- DR-0052):"; printf '%s\n' "$surface" | awk '$1=="filled"{print "      "$2}'; }
		[ "$nu" -gt 0 ] && { echo "    unclassified (not in the veneer map):"; printf '%s\n' "$surface" | awk '$1=="unclassified"{print "      "$2}'; }
		if [ "$verdict" = ready ] || [ "$verdict" = passing ]; then
			if [ -n "$run_note" ]; then printf '%s\n' "$run_note"; else
			echo "    every symbol resolves -- forwards, certified shims, filled stubs; running its test suite through the crossing is the next step (accept.sh -R skips it)."; fi
		else
			echo "    waits on the named shims to be written and stubs filled; it links and loads against the runtime as it stands."
		fi
	fi
	printf '%s=surface:%d,forward:%d,wired:%d,shim:%d,stub:%d,filled:%d,unclassified:%d,shape:%s,verdict:%s,run:%s\n' \
		"$name" "$total" "$nf" "$nw" "$ns" "$nb" "$nfill" "$nu" "$skind" "$verdict" "${run_state:-skipped}"
	pass=$((pass+1))
done 3< "$pins"

say ""
say "$prog: $pass classified, $fail did not build"
[ "$fail" = 0 ]
