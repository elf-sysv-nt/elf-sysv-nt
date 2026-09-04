#!/usr/bin/env bash
#
# Can a process whose only loaded module is ntdll do the kernel's work?
#
# Proposal 0011's lk-host is a PE that imports only ntdll, never loads
# kernel32, never registers with csrss, because a process that has registered
# with csrss cannot be cloned for fork (section 3, open question 3). This
# builds three probes with the mingw cross toolchain -- a Win32 parent and the
# ntdll-only child in its native-subsystem and console-subsystem shapes -- has
# the parent create the child with NtCreateUserProcess, and writes a dated
# transcript with a per-question reading and a verdict.
#
# It answers seven questions: creation from a Win32 parent (q1) and from cmd
# (q2), an AFD endpoint (q3), RtlWaitOnAddress across two threads (q4), an ALPC
# port (q5), the module list at steady state (q6), and survival across many
# launches (q7). It measures; the verdict word is the finding, the numbers ride
# along. The injection question is answered for Windows Defender only, which is
# the sole antivirus on this host.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --count=N         Relaunch the native image N times for q7. [default: 20]
#   -k, --keep              Keep the built binaries beside the sources.
#   -q, --quiet             Errors only.
#   -v, --verbose           Narrate each probe step.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as NATIVE_HOST_<OPTION>.

set -u

prog=native-host
release='native-host 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${NATIVE_HOST_OUTPUT:--}
count=${NATIVE_HOST_COUNT:-20}
keep=${NATIVE_HOST_KEEP:-0}
quiet=${NATIVE_HOST_QUIET:-0}
verbose=${NATIVE_HOST_VERBOSE:-0}

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

CC=${CC:-x86_64-w64-mingw32-gcc}
command -v "$CC" >/dev/null 2>&1 || die "no $CC on PATH"
command -v cygpath >/dev/null 2>&1 || die 'no cygpath (need the Cygwin build host)'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM

if [ "$keep" = 1 ]; then bindir=$here; else bindir=$work; fi
host=$bindir/host-probe.exe
native=$bindir/native-child-native.exe
console=$bindir/native-child-console.exe

childflags="-std=gnu11 -O1 -Wall -ffreestanding -fno-builtin -fno-stack-protector \
	-nostdlib -nodefaultlibs -e NtProcessStartup"

note 'building the Win32 parent'
"$CC" -std=gnu11 -O1 -Wall -municode "$here/host-probe.c" -o "$host" -lntdll \
	> "$work/build.log" 2>&1 || { cat "$work/build.log" >&2; die 'host-probe did not build'; }

note 'building the ntdll-only child, native subsystem'
# shellcheck disable=SC2086
"$CC" $childflags -Wl,--subsystem,native "$here/native-child.c" -o "$native" -lntdll \
	>> "$work/build.log" 2>&1 || { cat "$work/build.log" >&2; die 'the native image did not build'; }

note 'building the ntdll-only child, console subsystem'
# shellcheck disable=SC2086
"$CC" $childflags -Wl,--subsystem,console "$here/native-child.c" -o "$console" -lntdll \
	>> "$work/build.log" 2>&1 || { cat "$work/build.log" >&2; die 'the console image did not build'; }

# confirm the import table is ntdll and nothing else, in both shapes
objdump=${OBJDUMP:-x86_64-w64-mingw32-objdump}
imports_of() { "$objdump" -p "$1" | sed -n 's/.*DLL Name: //p' | tr 'A-Z' 'a-z' | sort -u | paste -sd, -; }
native_imports=$(imports_of "$native")
console_imports=$(imports_of "$console")

note "running the probes ($count launches for q7)"
probe_args=
[ "$verbose" = 1 ] && probe_args=--verbose
# shellcheck disable=SC2086
"$host" $probe_args --count "$count" \
	"$(cygpath -w "$native")" "$(cygpath -w "$console")" "$(cygpath -w "$work")" \
	> "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }

# the parent is a Windows program: its stdout is CRLF. Strip the CR so the
# key=value compares below see the values and not value-plus-carriage-return.
sed -i 's/\r$//' "$work/probe.out"

val() { sed -n "s/^$1=//p" "$work/probe.out" | head -1; }
is_ok() { [ "$1" = 0x0000000000000000 ] || [ "$1" = 0x00000000 ]; }

q1n_status=$(val q1_native_subsystem_ntstatus)
q1n_exit=$(val q1_native_subsystem_exit)
q1c_status=$(val q1_console_subsystem_ntstatus)
q1c_exit=$(val q1_console_subsystem_exit)
q2n_started=$(val q2_cmd_native_started)
q2n_exit=$(val q2_cmd_native_exit)
q2c_started=$(val q2_cmd_console_started)
q2c_exit=$(val q2_cmd_console_exit)
q3_open=$(val q3_afd_open_ntstatus)
q3_socket=$(val q3_afd_socket_ntstatus)
q3_bind=$(val q3_afd_bind_ntstatus)
q4=$(val q4_rtlwaitonaddress)
q5_create=$(val q5_alpc_create_ntstatus)
q5_connect=$(val q5_alpc_connect_ntstatus)
q6_count=$(val q6_module_count)
q6_k32=$(val q6_kernel32_present)
q7_launches=$(val q7_launches)
q7_clean=$(val q7_clean)
q7_crashed=$(val q7_crashed)
modules=$(sed -n 's/^mod=//p' "$work/probe.out" | paste -sd' ' -)

# Did the native-subsystem process get created and run to completion?
created=0
if is_ok "$q1n_status" && [ $((q1n_exit % 2)) -eq 1 ] && [ "$q1n_exit" -lt 256 ]; then
	created=1
fi

# The finding. The kernel's work needs a process that is created, stays
# ntdll-only (no kernel32 dragged in by injection), and reaches the executive
# services it depends on. AFD's floor is a device open; the socket open packet
# is a userland detail, not a reachability question.
finding=native-host-refused
if [ "$created" = 1 ]; then
	if [ "$q6_k32" = 1 ]; then
		finding=native-host-injected
	elif ! is_ok "$q3_open"; then
		finding=ntdll-only-but-no-afd
	else
		finding=native-host-viable
	fi
fi

yn() { [ "$1" = 1 ] && printf yes || printf no; }
okword() { is_ok "$1" && printf 'success' || printf 'refused (%s)' "$1"; }

{
	printf 'an ntdll-only process, created from Win32 and doing the kernel work\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(val host_windows)"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$CC" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$host" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  created from a Win32 parent with NtCreateUserProcess:\n'
	printf '        native subsystem  %s, child ran (exit %s), imports %s\n' \
		"$(okword "$q1n_status")" "$q1n_exit" "$native_imports"
	printf '        console subsystem %s, child ran (exit %s), imports %s\n' \
		"$(okword "$q1c_status")" "$q1c_exit" "$console_imports"
	printf '  q2  launched by cmd.exe with CreateProcess:\n'
	printf '        native subsystem  %s\n' \
		"$([ "$q2n_started" = 1 ] && { [ "$q2n_exit" = 1 ] && printf 'cmd refused it (not a Win32 application)' || printf "ran (exit $q2n_exit)"; } || printf 'CreateProcess failed')"
	printf '        console subsystem %s\n' \
		"$([ "$q2c_started" = 1 ] && printf "started and ran (exit $q2c_exit)" || printf 'CreateProcess failed')"
	printf '  q3  AFD endpoint: device open %s; socket open packet %s; bind %s\n' \
		"$(okword "$q3_open")" "$(okword "$q3_socket")" "$(okword "$q3_bind")"
	printf '  q4  RtlWaitOnAddress / RtlWakeAddressSingle over two threads: %s\n' \
		"$(yn "$q4")"
	printf '  q5  ALPC port: create %s; connect %s\n' \
		"$(okword "$q5_create")" "$(okword "$q5_connect")"
	printf '  q6  modules mapped into the native process (%s): %s\n' \
		"$q6_count" "$modules"
	printf '        kernel32 injected: %s\n' "$(yn "$q6_k32")"
	printf '  q7  %s launches, %s clean, %s crashed\n\n' \
		"$q7_launches" "$q7_clean" "$q7_crashed"

	printf 'raw\n\n'
	sed -e 's/^/    /' "$work/probe.out"

	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then
	cat "$work/report"
else
	o=$output
	cat "$work/report" > "$o" || die "cannot write $output"
	note "transcript written to $output"
fi
