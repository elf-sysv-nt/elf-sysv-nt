#!/usr/bin/env bash
#
# Which layer can carry a target-mandated link default?
#
# DR-0061 requires every image to be linked granule-separable, and the
# mechanism carrying that today is a specs file the gcc driver reads. Two
# other layers could carry it instead, and the choice between them is the
# operator's (DR-0108 parks it at tier 8). What is not the operator's is
# whether each candidate does what its advocate says, so this measures them.
#
#   A  the compiler: LINK_SPEC in gcc/config/i386/elfsysvnt.h, beside the
#      subtarget default that header already mandates. A gcc rebuild.
#   D  the linker: ELF_MAXPAGESIZE for this target's vector in bfd. A
#      binutils rebuild, and it holds for a direct ld invocation as well as
#      a gcc-driven one -- which is the claim under test, since DR-0061's own
#      worked example is bzip2, whose Makefile passes no max-page-size.
#
# Five probes per prefix, each a verdict rather than a number:
#
#   driver      the gcc driver puts max-page-size on the link line
#   direct-ld   a link that never runs the driver is still granule-aligned
#   no-specs    the default survives with no specs file installed at all,
#               which is what either candidate is for
#   override    -z max-page-size=0x1000 still wins per link, because DR-0008's
#               own test has to be able to build a sub-granule image
#   unwinder    --eh-frame-hdr and the shared libgcc are still passed
#
# A prefix that is not built is reported as not-built, never failed: two of
# these three do not exist until somebody spends the rebuild.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE   Transcript destination; - is stdout. [default: -]
#   -b DIR, --baseline=DIR   The toolchain as it stands.
#                            [default: the prefix bin/roots.sh resolves]
#   -a DIR, --candidate-a=DIR  A gcc built with the default in LINK_SPEC.
#   -d DIR, --candidate-d=DIR  A binutils built with ELF_MAXPAGESIZE raised.
#   -T TRIPLE, --target=TRIPLE  [default: x86_64-elfsysvnt-linux-gnu]
#   -q, --quiet              Errors only.
#   -V, --version            Print the version and exit.
#   -h, --help               Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.
#
# Exit codes: 0 the run completed, 1 failure, 2 usage error. A candidate that
# fails a probe is a finding, not an error.

set -u

prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../../bin/roots.sh"

baseline=${MEASURE_BASELINE:-$ELFSYSVNT_PREFIX}
cand_a=${MEASURE_CANDIDATE_A:-}
cand_d=${MEASURE_CANDIDATE_D:-}
target=${MEASURE_TARGET:-x86_64-elfsysvnt-linux-gnu}
output=${MEASURE_OUTPUT:--}
quiet=${MEASURE_QUIET:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)      usage; exit 0 ;;
		-V|--version)   printf '%s\n' "$release"; exit 0 ;;
		-o|--output)    output=${2:-}; shift 2 ;;
		--output=*)     output=${1#*=}; shift ;;
		-b|--baseline)  baseline=${2:-}; shift 2 ;;
		--baseline=*)   baseline=${1#*=}; shift ;;
		-a|--candidate-a) cand_a=${2:-}; shift 2 ;;
		--candidate-a=*)  cand_a=${1#*=}; shift ;;
		-d|--candidate-d) cand_d=${2:-}; shift 2 ;;
		--candidate-d=*)  cand_d=${1#*=}; shift ;;
		-T|--target)    target=${2:-}; shift 2 ;;
		--target=*)     target=${1#*=}; shift ;;
		-q|--quiet)     quiet=1; shift ;;
		--)             shift; break ;;
		-?*)            printf '%s: unknown option %s\n' "$prog" "$1" >&2; usage >&2; exit 2 ;;
		*)              break ;;
	esac
done

granule=$((0x10000))
work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM

printf 'int main(void) { return 0; }\n' > "$work/t.c"
cat > "$work/u.cc" <<'EOF'
#include <stdexcept>
void poke (int v) { if (v > 0) throw std::runtime_error ("crossed"); }
EOF

# The smallest granule a PT_LOAD boundary falls on. This reads separation
# between consecutive PT_LOAD p_vaddr values, NOT p_align, and the distinction
# is the whole reason the first run of this spike reported candidate D as
# changing nothing:
#
#   p_align comes from ELF_COMMONPAGESIZE, which is 0x1000 for this vector and
#   which neither candidate touches. max-page-size moves where the linker
#   *places* the next segment, so a raised max-page-size shows up as 0x10000
#   between segments while p_align still reads 0x1000. A probe that reads
#   p_align sees a linker-side default as absent however large it is.
#
# DR-0008 refuses two segments of unlike protection that share a granule, so
# separation is also the quantity the record is written in terms of. An image
# with one PT_LOAD has no separation to measure and falls back to p_align,
# since for a single segment the two say the same thing. The arithmetic is
# done in the shell rather than in awk, because strtonum is gawk's and this
# has to run wherever the toolchain does. Prints nothing when there are no
# LOAD segments, which the caller reads as a failed link.
min_granule() {
	m= prev=
	for v in $("$1" -lW "$2" 2>/dev/null | awk '$1 == "LOAD" { print $3 }'); do
		vv=$((v))
		if [ -n "$prev" ]; then
			d=$((vv - prev))
			if [ "$d" -gt 0 ] && { [ -z "$m" ] || [ "$d" -lt "$m" ]; }; then m=$d; fi
		fi
		prev=$vv
	done
	if [ -z "$m" ]; then
		for a in $("$1" -lW "$2" 2>/dev/null | awk '$1 == "LOAD" { print $NF }'); do
			av=$((a))
			if [ -z "$m" ] || [ "$av" -lt "$m" ]; then m=$av; fi
		done
	fi
	[ -n "$m" ] && printf '%s\n' "$m"
}

# One prefix, five verdicts, tab separated. Every verdict is a word: the
# alignments behind them move with the link and the words do not.
probe() { # label, prefix
	label=$1 prefix=$2
	cc=$prefix/bin/$target-gcc
	cxx=$prefix/bin/$target-g++
	ld=$prefix/bin/$target-ld
	readelf=$prefix/bin/$target-readelf
	bflag=

	# Candidate D is a binutils and nothing else: raising ELF_MAXPAGESIZE
	# changes the linker, and rebuilding a compiler to measure a linker would
	# be measuring the wrong thing. So a prefix holding an ld and no gcc is
	# driven by the baseline compiler, and the tools that read the result come
	# from the same prefix as the linker that wrote it.
	#
	# -B$prefix/bin/ is the obvious way to do that and it is wrong. The driver
	# looks the linker up under the bare name `ld`, not under the triple, and
	# $prefix/bin holds only $target-ld; so -B contributes no candidate `ld`
	# and the driver falls through to $baseline/$target/bin/ld. The probes
	# then report the baseline linker's behaviour under the candidate's name,
	# which is how this spike first recorded candidate D as changing nothing.
	# Verified with -print-prog-name=ld, which names the baseline copy under
	# -B and the candidate only when asked for $target-ld.
	#
	# So the shim: a directory holding an `ld` under the name the driver asks
	# for, pointing at the candidate's. -print-prog-name=ld is then asserted
	# below rather than assumed, because this is exactly the mistake that a
	# comment alone does not prevent a second time.
	if ! { [ -x "$cc" ] || [ -x "$cc.exe" ]; }; then
		if [ -x "$ld" ] || [ -x "$ld.exe" ]; then
			cc=$baseline/bin/$target-gcc
			cxx=$baseline/bin/$target-g++
			shim=$work/shim.$label
			mkdir -p "$shim"
			real=$ld
			[ -x "$ld.exe" ] && real=$ld.exe
			cp -p "$real" "$shim/ld" 2>/dev/null ||
				ln -sf "$real" "$shim/ld" 2>/dev/null
			cp -p "$real" "$shim/ld.exe" 2>/dev/null
			bflag=-B$shim/
			got=$("$cc" $bflag -print-prog-name=ld 2>/dev/null)
			case $got in
				"$shim"/*) : ;;
				*) printf '%s\tdriver=shim-failed\tdirect-ld=shim-failed\tno-specs=shim-failed\toverride=shim-failed\tunwinder=shim-failed\n' "$label"
				   note "$label: -B did not displace the linker; got $got"
				   return 0 ;;
			esac
		else
			printf '%s\tdriver=not-built\tdirect-ld=not-built\tno-specs=not-built\toverride=not-built\tunwinder=not-built\n' "$label"
			return 0
		fi
	fi

	"$cc" $bflag -### -o "$work/probe" "$work/t.c" > "$work/dash3" 2>&1
	if grep -q 'max-page-size=0x10000' "$work/dash3"; then driver=carries; else driver=absent; fi

	# The link nobody asked to be granule-separable: compile to an object with
	# the driver, then link it with ld directly, the way a hand-written
	# Makefile does. Only a linker-side default reaches this.
	direct=unlinkable
	if "$cc" $bflag -c -o "$work/t.o" "$work/t.c" 2>/dev/null &&
	   "$ld" -o "$work/direct" "$work/t.o" -e main 2>/dev/null; then
		a=$(min_granule "$readelf" "$work/direct")
		if [ -n "$a" ]; then
			if [ "$a" -ge "$granule" ]; then direct=granule-aligned; else direct=sub-granule; fi
		fi
	fi

	# What the candidate exists to make unnecessary. The specs file moves for
	# the duration and comes back; -B cannot displace it, since a -B prefix is
	# searched in addition to the installed path.
	nospecs=not-measured
	version=$("$cc" -dumpversion 2>/dev/null)
	# The specs file belongs to whichever prefix the compiler came from, which
	# is the candidate's for A and the baseline's for a linker-only candidate.
	specsroot=$prefix
	[ -n "$bflag" ] && specsroot=$baseline
	specs=$specsroot/lib/gcc/$target/$version/specs
	saved=$work/specs.saved.$label
	had=0
	[ -f "$specs" ] && cp -p "$specs" "$saved" && had=1
	rm -f "$specs"
	# Ask the image, not the command line. A compiler-side default (candidate
	# A) puts max-page-size on the link line and would answer either way; a
	# linker-side one (candidate D) never appears on a command line at all,
	# so grepping the driver's -### output reports every linker candidate as
	# having lost a default it is in fact still applying. What both candidates
	# claim is a granule-separable image with no specs file installed, so that
	# is what gets measured.
	if "$cc" $bflag -o "$work/probe2" "$work/t.c" 2>/dev/null; then
		a=$(min_granule "$readelf" "$work/probe2")
		if [ -n "$a" ]; then
			if [ "$a" -ge "$granule" ]; then nospecs=default-holds; else nospecs=default-lost; fi
		fi
	else
		nospecs=unlinkable
	fi
	[ "$had" = 1 ] && cp -p "$saved" "$specs"

	# DR-0008's own test builds a sub-granule image on purpose, so the default
	# must stay overridable. A candidate that cannot be overridden disarms the
	# test of the layer that is the actual guarantee.
	override=not-measured
	if "$cc" $bflag -o "$work/small" "$work/t.c" -Wl,-z,max-page-size=0x1000 2>/dev/null; then
		a=$(min_granule "$readelf" "$work/small")
		if [ -n "$a" ]; then
			if [ "$a" -le 4096 ]; then override=honored; else override=ignored; fi
		fi
	fi

	# DR-0108's property, restated here because a candidate that reintroduces
	# a hand-written specs file would cost these two again.
	unwinder=no-cxx
	if { [ -x "$cxx" ] || [ -x "$cxx.exe" ]; }; then
		"$cxx" $bflag -O2 -fPIC -shared -o "$work/u.so" "$work/u.cc" -### > "$work/dash3c" 2>&1
		line=$(grep collect2 "$work/dash3c" | tr ' ' '\n' | tr -d '"')
		eh=$(printf '%s\n' "$line" | grep -c -- '--eh-frame-hdr')
		gs=$(printf '%s\n' "$line" | grep -cx -- '-lgcc_s')
		if [ "$eh" -ge 1 ] && [ "$gs" -ge 1 ]; then unwinder=both-passed
		elif [ "$eh" -ge 1 ]; then unwinder=lost-shared-libgcc
		elif [ "$gs" -ge 1 ]; then unwinder=lost-eh-frame-hdr
		else unwinder=lost-both; fi
	fi

	printf '%s\tdriver=%s\tdirect-ld=%s\tno-specs=%s\toverride=%s\tunwinder=%s\n' \
		"$label" "$driver" "$direct" "$nospecs" "$override" "$unwinder"
}

note 'probing every prefix that exists'
: > "$work/rows"
probe baseline "$baseline" >> "$work/rows"
[ -n "$cand_a" ] && probe candidate-a "$cand_a" >> "$work/rows"
[ -n "$cand_d" ] && probe candidate-d "$cand_d" >> "$work/rows"

val() { awk -F'\t' -v s="$1" -v k="$2" '$1==s { for (i=2;i<=NF;i++) { split($i,p,"="); if (p[1]==k) print p[2] } }' "$work/rows"; }

# A candidate carries the default when it holds with no specs file and reaches
# a link the driver never ran, while leaving the per-link override alone.
verdict_for() {
	ns=$(val "$1" no-specs); dl=$(val "$1" direct-ld); ov=$(val "$1" override)
	[ -z "$ns" ] && { printf 'absent'; return; }
	case $ns in not-built) printf 'not-built'; return ;; esac
	if [ "$ns" = default-holds ] && [ "$ov" = honored ]; then
		if [ "$dl" = granule-aligned ]; then printf 'carries-including-direct-ld'
		else printf 'carries-driver-only'; fi
	else
		printf 'does-not-carry'
	fi
}

finding="baseline=$(verdict_for baseline)"
[ -n "$cand_a" ] && finding="$finding a=$(verdict_for candidate-a)"
[ -n "$cand_d" ] && finding="$finding d=$(verdict_for candidate-d)"

{
	printf 'which layer can carry a target-mandated link default\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$baseline/bin/$target-gcc" --version 2>/dev/null | head -1)"
	printf 'linker      %s\n' "$("$baseline/bin/$target-ld" --version 2>/dev/null | head -1)"
	printf 'target      %s\n' "$target"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n\n' "$release"
	printf 'reading\n\n'
	printf '  a candidate carries the default when it survives with no specs\n'
	printf '  file and still lets one link override it. Whether it also reaches\n'
	printf '  a direct ld invocation is what separates the linker from the\n'
	printf '  compiler, and it is the whole of DR-0061 bzip2 argument.\n\n'
	printf 'per prefix\n\n'
	printf '    %-13s %-9s %-16s %-14s %-10s %s\n' prefix driver direct-ld no-specs override unwinder
	awk -F'\t' '{ for (i=2;i<=NF;i++) { split($i,p,"="); v[p[1]]=p[2] }
	              printf "    %-13s %-9s %-16s %-14s %-10s %s\n", $1, v["driver"], v["direct-ld"], v["no-specs"], v["override"], v["unwinder"] }' "$work/rows"
	printf '\nraw\n\n'
	sed -e 's/^/    /' "$work/rows"
	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then cat "$work/report"
else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
