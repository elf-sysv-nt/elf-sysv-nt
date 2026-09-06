#!/usr/bin/env bash
#
# Does this NTFS volume carry WSL's Linux metadata through NT's own interfaces?
#
# Proposal 0011 § 8 decides that the kernel's on-disk format is WSL's DrvFs
# format rather than one of its own, and its "Not verified" list admits the
# decision rests on documentation: FileStatLxInformation returning the four LX
# fields through NtQueryInformationByName is named there as unmeasured. This
# builds the native probe, runs it against a scratch tree on the volume, has
# WSL and Cygwin read that tree back, and writes a dated transcript with a
# verdict.
#
# The scratch tree lives under $TMPDIR and is removed at exit, including the
# LX_CHR reparse points, which Cygwin's rm refuses because it sees character
# devices; the probe's own --clean does that part.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --count=N         Files in the stat-rate tree. [default: 2000]
#   -k, --keep              Keep the built binaries beside the sources.
#   -q, --quiet             Errors only.
#   -v, --verbose           Pass --verbose to the probe.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_LXFS_<OPTION>.

set -u

prog=measure-lxfs
release='measure-lxfs 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${MEASURE_LXFS_OUTPUT:--}
count=${MEASURE_LXFS_COUNT:-2000}
keep=${MEASURE_LXFS_KEEP:-0}
quiet=${MEASURE_LXFS_QUIET:-0}
verbose=${MEASURE_LXFS_VERBOSE:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-n|--count)   count=${2:-}; shift 2 ;;
		--count=*)    count=${1#*=}; shift ;;
		-k|--keep)    keep=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		-v|--verbose) verbose=1; shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
case $count in
	''|*[!0-9]*) printf '%s: --count wants a number\n' "$prog" >&2; exit 2 ;;
esac
[ "$count" -ge 1 ] || { printf '%s: --count wants a number\n' "$prog" >&2; exit 2; }

cross=x86_64-w64-mingw32-gcc
command -v "$cross" >/dev/null 2>&1 || die "no $cross on PATH"
command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'
command -v cygpath >/dev/null 2>&1 || die 'no cygpath; this wants a Cygwin shell'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
scratch=$work/scratch
mkdir -p "$scratch" || die 'cannot create the scratch tree'
scratch_w=$(cygpath -w "$scratch")

# The probe's own cleaner runs first because two of the specimens are LX_CHR
# reparse points and rm will not remove them.
cleanup() {
	[ -n "${bin:-}" ] && [ -x "$bin" ] && [ -d "$scratch" ] &&
		"$bin" --clean "$scratch_w" >/dev/null 2>&1
	rm -rf "$work"
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

if [ "$keep" = 1 ]; then
	bin=$here/lxfs-probe.exe
	rate=$here/stat-rate.exe
else
	bin=$work/lxfs-probe.exe
	rate=$work/stat-rate.exe
fi

note 'building the native probe'
"$cross" -std=gnu11 -O1 -Wall -Wextra -o "$bin" \
	"$here/lxfs-probe.c" "$here/lxfs-nt.c" > "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the native probe did not build'; }

note 'building the Cygwin stat comparison'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$rate" "$here/stat-rate.c" \
	> "$work/build-rate.log" 2>&1 ||
	{ cat "$work/build-rate.log" >&2; die 'stat-rate did not build'; }

probe_args=
[ "$verbose" = 1 ] && probe_args=--verbose

note 'running the native probe'
# shellcheck disable=SC2086
"$bin" $probe_args --count="$count" "$scratch_w" \
	> "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the native probe did not run'; }

note 'running the Cygwin stat comparison'
"$rate" -n "$count" "$scratch/bench" >> "$work/probe.out" 2>/dev/null ||
	note 'the Cygwin stat comparison did not run'

# q7. The same reader script is handed to both readers, so a difference in
# what they report is a difference in what they see rather than in what they
# were asked. An absent WSL is recorded as not checked; it is never a pass.
cat > "$work/read-tree.sh" <<'READER'
cd "$1" || exit 9
p=$2
printf '%s_file644=%s\n' "$p" "$(stat -c '%a %u %g' file644 2>/dev/null || echo -)"
printf '%s_file755=%s\n' "$p" "$(stat -c '%a %u %g' file755 2>/dev/null || echo -)"
printf '%s_link=%s\n' "$p" "$(readlink link 2>/dev/null || echo -)"
printf '%s_link_type=%s\n' "$p" "$(stat -c '%F' link 2>/dev/null || echo -)"
printf '%s_nulldev=%s\n' "$p" "$(stat -c '%a %t %T' nulldev 2>/dev/null || echo -)"
printf '%s_nulldev_type=%s\n' "$p" "$(stat -c '%F' nulldev 2>/dev/null || echo -)"
READER

wsl_exe=$(command -v wsl.exe || printf '%s' /c/Windows/System32/wsl.exe)
distro=
if [ -x "$wsl_exe" ]; then
	"$wsl_exe" -l -q > "$work/distros.raw" 2>/dev/null
	tr -d '\0\r' < "$work/distros.raw" | sed '/^[[:space:]]*$/d' > "$work/distros"
	# rocky8 by preference, since it is the distro this project already
	# uses; otherwise whichever is listed first.
	if grep -qx 'rocky8' "$work/distros"; then
		distro=rocky8
	else
		distro=$(head -1 "$work/distros")
	fi
fi

if [ -n "$distro" ]; then
	note "reading the tree back from WSL ($distro)"
	m=$(cygpath -m "$work")
	drive=$(printf '%s' "${m%%:*}" | tr 'A-Z' 'a-z')
	wsl_work=/mnt/$drive${m#*:}
	"$wsl_exe" -d "$distro" -- sh "$wsl_work/read-tree.sh" \
		"$wsl_work/scratch/tree" q7_wsl > "$work/wsl.raw" 2>/dev/null
	tr -d '\0\r' < "$work/wsl.raw" > "$work/wsl.out"
	printf 'q7_wsl_present=1\n' >> "$work/probe.out"
	printf 'q7_wsl_distro=%s\n' "$distro" >> "$work/probe.out"
	cat "$work/wsl.out" >> "$work/probe.out"
else
	printf 'q7_wsl_present=0\n' >> "$work/probe.out"
fi

note 'reading the tree back from Cygwin'
sh "$work/read-tree.sh" "$scratch/tree" q7_cyg >> "$work/probe.out" 2>/dev/null
printf 'q7_cygwin_version=%s\n' "$(uname -r)" >> "$work/probe.out"

# The native probe writes through a Windows stdio in text mode, so its lines
# arrive with carriage returns; every reader below wants them gone.
tr -d '' < "$work/probe.out" > "$work/probe.clean" &&
	mv "$work/probe.clean" "$work/probe.out"

val() { sed -n "s/^$1=//p" "$work/probe.out" | tail -1; }

# What the probe wrote, quoted back from the probe's own output rather than
# repeated here, so the two cannot drift apart.
want644=$(val q7_want_file644)
want755=$(val q7_want_file755)
wantlink=$(val q7_want_link)

agree() { [ "$1" = "$2" ] && printf 1 || printf 0; }

if [ -n "$distro" ]; then
	w_modes=$(agree "$(val q7_wsl_file644)" "$want644")
	w_modes755=$(agree "$(val q7_wsl_file755)" "$want755")
	w_link=$(agree "$(val q7_wsl_link)" "$wantlink")
	w_dev=$(agree "$(val q7_wsl_nulldev)" "666 1 3")
	w_devtype=$(agree "$(val q7_wsl_nulldev_type)" "character special file")
	q7_wsl_agrees=0
	[ "$w_modes$w_modes755$w_link$w_dev$w_devtype" = 11111 ] &&
		q7_wsl_agrees=1
	printf 'q7_wsl_agrees=%s\n' "$q7_wsl_agrees" >> "$work/probe.out"
else
	q7_wsl_agrees=notchecked
fi

c_modes=$(agree "$(val q7_cyg_file644)" "$want644")
c_link=$(agree "$(val q7_cyg_link)" "$wantlink")
c_dev=$(agree "$(val q7_cyg_nulldev_type)" "character special file")
printf 'q7_cygwin_modes_agree=%s\n' "$c_modes" >> "$work/probe.out"
printf 'q7_cygwin_symlink_agrees=%s\n' "$c_link" >> "$work/probe.out"
printf 'q7_cygwin_special_agrees=%s\n' "$c_dev" >> "$work/probe.out"
q7_cygwin_agrees=0
[ "$c_modes$c_link$c_dev" = 111 ] && q7_cygwin_agrees=1
printf 'q7_cygwin_agrees=%s\n' "$q7_cygwin_agrees" >> "$work/probe.out"

q1=$(val q1_stat_lx_by_handle)
q1f=$(val q1_lx_fields_present)
q2=$(val q2_stat_lx_by_name)
q2a=$(val q2_agrees_with_handle)
q3ea=$(val q3_lx_eas_written)
q3st=$(val q3_stat_reflects_eas)
q3dev=$(val q3_dev_eas_written)
q3devst=$(val q3_dev_stat_reflects_eas)
q4=$(val q4_lx_symlink_created)
q4t=$(val q4_target_survives)
q4p=$(val q4_needed_privilege)
q5=$(val q5_case_sensitive_set)
q5c=$(val q5_both_names_coexist)
q6=$(val q6_posix_delete)
q6g=$(val q6_name_gone)
q6r=$(val q6_name_reusable)
q6h=$(val q6_handle_usable)
q9r=$(val q9_posix_rename_over_open)
q9rw=$(val q9_win32_replace_over_open)
q9rh=$(val q9_old_target_handle_usable)
q9d=$(val q9_posix_delete_mapped)
q9dv=$(val q9_view_still_reads)
q9t=$(val q9_truncate_mapped)
q9tu=$(val q9_truncate_after_unmap)
q9dir=$(val q9_dir_rename_with_open_child)
q9dirc=$(val q9_dir_rename_after_close)

# The verdict, as a ladder from the load-bearing end of proposal 0011's
# on-disk decision down. Each rung names the first thing the volume would not
# do, because that is what the design would have to work around; the rungs
# below it are not reported as passing, they are simply not what stopped it.
finding=lxfs-inconclusive
if [ "$q1" != 1 ] || [ "$q1f" != 1 ]; then
	finding=lxfs-no-stat-lx
elif [ "$q2" != 1 ] || [ "$q2a" != 1 ]; then
	finding=lxfs-handle-only
elif [ "$q3ea" != 1 ] || [ "$q3dev" != 1 ]; then
	finding=lxfs-no-metadata-eas
elif [ "$q3st" != 1 ] || [ "$q3devst" != 1 ]; then
	finding=lxfs-eas-opaque-to-stat
elif [ "$q4" != 1 ] || [ "$q4t" != 1 ]; then
	finding=lxfs-no-lx-symlink
elif [ "$q5" != 1 ] || [ "$q5c" != 1 ]; then
	finding=lxfs-no-case-sensitivity
elif [ "$q6" != 1 ] || [ "$q6g" != 1 ] || [ "$q6r" != 1 ] || [ "$q6h" != 1 ]; then
	finding=lxfs-no-posix-delete
elif [ "$q7_wsl_agrees" = notchecked ]; then
	finding=lxfs-nt-complete-wsl-unchecked
elif [ "$q7_wsl_agrees" != 1 ]; then
	finding=lxfs-wsl-disagrees
elif [ "$q7_cygwin_agrees" != 1 ]; then
	finding=lxfs-complete-cygwin-blind
else
	finding=lxfs-complete
fi

# q9's words ride beside the ladder rather than on it: each is a divergence
# the VFS records or a semantics it gets for free, and neither changes what
# the on-disk format can do.
w_ren=$([ "$q9r" = 1 ] && [ "$q9rh" = 1 ] && printf 'posix-rename-replaces-open-target' || printf 'rename-over-open-refused')
w_del=$([ "$q9d" = 1 ] && [ "$q9dv" = 1 ] && printf 'posix-delete-of-mapped-file-works' || printf 'delete-of-mapped-file-refused')
w_tr=$([ "$q9t" = 1 ] && printf 'truncate-of-mapped-file-works' || { [ "$q9tu" = 1 ] && printf 'truncate-of-mapped-file-refused-until-unmapped' || printf 'truncate-refused'; })
w_dir=$([ "$q9dir" = 1 ] && printf 'directory-rename-over-open-child-works' || { [ "$q9dirc" = 1 ] && printf 'directory-rename-refused-while-child-open' || printf 'directory-rename-refused'; })
finding="$finding,$w_ren,$w_del,$w_tr,$w_dir"

yesno() { [ "$1" = 1 ] && printf '%s' "$2" || printf '%s' "$3"; }

winver=$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')
[ -n "$winver" ] || winver=unknown

{
	printf 'WSL DrvFs metadata through NT interfaces on an NTFS volume\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$winver"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cross" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  FileStatLxInformation on a handle: %s, LX fields %s\n' \
		"$(yesno "$q1" 'returns success' 'REFUSED')" \
		"$(yesno "$q1f" 'present' 'ABSENT')"
	printf '  q2  the same class without opening the file: %s%s\n' \
		"$(yesno "$q2" 'returns success' 'REFUSED')" \
		"$(yesno "$q2a" ', agreeing field for field with q1' '')"
	printf '  q3  the four metadata EAs: %s; the stat class %s\n' \
		"$(yesno "$q3ea" 'round-trip' 'DO NOT round-trip')" \
		"$(yesno "$q3st" 'reports what was written' 'IGNORES THEM')"
	printf '  q4  an LX symlink reparse point: %s%s\n' \
		"$(yesno "$q4t" 'created and read back with its target' 'DID NOT SURVIVE')" \
		"$(yesno "$q4p" ', and it wanted a privilege' ', with no privilege asked for')"
	printf '  q5  a case-sensitive directory: %s%s\n' \
		"$(yesno "$q5" 'set' 'REFUSED')" \
		"$(yesno "$q5c" ', two names differing only in case coexist' ', names DO NOT coexist')"
	printf '  q6  POSIX delete of an open file: %s%s\n' \
		"$(yesno "$q6g" 'the name leaves the directory' 'THE NAME STAYS')" \
		"$(yesno "$q6h" ', the handle keeps working, the name is reusable at once' ', BUT THE HANDLE DIED')"
	if [ -n "$distro" ]; then
		printf '  q7  WSL reads the tree back: %s; Cygwin %s\n' \
			"$(yesno "$q7_wsl_agrees" 'modes, owners, symlink and device all agree' 'DISAGREES')" \
			"$(yesno "$q7_cygwin_agrees" 'agrees too' 'reads the symlink and nothing else')"
	else
		printf '  q7  WSL is not installed here, so interop is not checked\n'
	fi
	printf '  q8  stat rate, context only: %s ns per NtQueryInformationByName, %s ns per Cygwin stat()\n\n' \
		"$(val q8_byname_ns_per_stat)" "$(val q8_cygwin_ns_per_stat)"
	printf '  q9  rename over an open target: Win32 replace %s, POSIX rename %s, the old handle still reads %s
' \
		"$(yesno "$q9rw" 'works' 'refused')" "$(yesno "$q9r" 'works' 'REFUSED')" "$(yesno "$q9rh" 'yes' 'NO')"
	printf '      a file with a live section view: POSIX delete %s (the view still reads %s); truncate %s, after unmapping %s
' \
		"$(yesno "$q9d" 'works' 'REFUSED')" "$(yesno "$q9dv" 'yes' 'no')" "$(yesno "$q9t" 'works' 'refused')" "$(yesno "$q9tu" 'works' 'refused')"
	printf '      a directory renamed with a child handle open: %s; after the child closes %s

' \
		"$(yesno "$q9dir" 'works' 'refused')" "$(yesno "$q9dirc" 'works' 'refused')"

	printf 'raw\n\n'
	sed -e 's/^/    /' "$work/probe.out"

	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi
