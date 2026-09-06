#!/usr/bin/env bash
#
# Assemble the source-only probe bundle for a host that is not this one: a
# zip of the native probes' sources, a build.cmd that compiles them with a
# mingw-w64 gcc the client unpacks in their profile, a run.cmd that runs each
# and writes one key=value file per probe, and the read-only WHP policy
# probe. Nothing executable goes in: the client builds from source, which is
# the only thing the project ships them.
#
# Usage:
#   make-bundle.sh [options]
#
# Options:
#   -o FILE, --output=FILE  The zip to write. [default: $ELFSYSVNT_ROOT/a/bundle/elfsysvnt-probes-<date>.zip]
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as BUNDLE_<OPTION>.

set -u
prog=make-bundle
release='make-bundle 1.0'
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../../bin/roots.sh"
today=$(date -u +%Y-%m-%d)
output=${BUNDLE_OUTPUT:-$ELFSYSVNT_ROOT/a/bundle/elfsysvnt-probes-$today.zip}
quiet=${BUNDLE_QUIET:-0}
usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output) output=${2:-}; shift 2 ;;
		--output=*) output=${1#*=}; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
command -v zip >/dev/null 2>&1 || die 'no zip on PATH'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
name=elfsysvnt-probes-$today
stage=$work/$name
mkdir -p "$stage/src" "$stage/out"

# CRLF for the files a Windows shell reads; the sources stay as they are.
crlf() { sed 's/$/\r/' > "$1"; }

# ---- the sources, from the manifest ---------------------------------------
manifest=$here/manifest.tsv
rows=$(grep -v '^#' "$manifest" | grep -v '^[[:space:]]*$')
[ -n "$rows" ] || die "empty manifest $manifest"

build_lines=
run_lines=
while IFS=$'\t' read -r dir sources libs args collect; do
	sdir=$ELFSYSVNT_ROOT/spike/$dir
	[ -d "$sdir" ] || die "no spike directory $sdir"
	mkdir -p "$stage/src/$dir"
	objs=
	for f in $sources; do
		[ -f "$sdir/$f" ] || die "$dir: no source $f"
		cp "$sdir/$f" "$stage/src/$dir/"
		objs="$objs src\\$dir\\$f"
	done
	# headers beside the sources travel too
	for h in "$sdir"/*.h; do [ -f "$h" ] && cp "$h" "$stage/src/$dir/"; done
	[ "$libs" = - ] && libs=
	[ "$args" = - ] && args=
	build_lines="$build_lines
echo building $dir
%CC% -std=gnu11 -O1 -Wall -Wextra -Isrc\\$dir -o bin\\$dir.exe$objs $libs || set FAILED=%FAILED% $dir"
	run_lines="$run_lines
call :one $dir $args"
done <<EOF
$rows
EOF

# ---- build.cmd --------------------------------------------------------------
crlf "$stage/build.cmd" <<EOF
@echo off
rem Build every probe from source with a mingw-w64 gcc. Put the toolchain's
rem bin directory on PATH first, or set CC to the full path of its gcc:
rem   set PATH=%USERPROFILE%\\mingw64\\bin;%PATH%
rem   build.cmd
rem Nothing is installed and nothing outside this directory is written.
setlocal
cd /d "%~dp0"
if "%CC%"=="" set CC=gcc
%CC% --version >nul 2>&1 || (
	echo build.cmd: no compiler; put mingw-w64's bin on PATH or set CC
	exit /b 2
)
if not exist bin mkdir bin
set FAILED=
$build_lines
%CC% --version > bin\\compiler.txt
if "%FAILED%"=="" (
	echo build.cmd: every probe built into bin\\
	exit /b 0
)
echo build.cmd: did not build:%FAILED%
exit /b 1
EOF

# ---- run.cmd ----------------------------------------------------------------
crlf "$stage/run.cmd" <<EOF
@echo off
rem Run every probe that built and write out\\<probe>.txt for each, with a
rem header naming this host, so the transcripts can be rendered elsewhere.
rem Unelevated. Writes only under out\\ and the lxfs scratch directory.
rem   run.cmd [scratch-dir]
rem The scratch directory is for the lxfs probe and must be on a local NTFS
rem volume, not a redirected or roaming profile; the default is %TEMP%.
setlocal enabledelayedexpansion
cd /d "%~dp0"
if not exist bin\\compiler.txt (
	echo run.cmd: nothing built; run build.cmd first
	exit /b 2
)
if not exist out mkdir out
set SCRATCH=%~1
if "%SCRATCH%"=="" set SCRATCH=%TEMP%\\elfsysvnt-lxfs
set COMPILER=
for /f "usebackq delims=" %%c in ("bin\\compiler.txt") do if not defined COMPILER set "COMPILER=%%c"
for /f "tokens=*" %%v in ('ver') do set WINVER=%%v
set WINVER=%WINVER:Microsoft Windows [Version =%
set WINVER=%WINVER:]=%
if not defined COMPUTERNAME for /f "delims=" %%h in ('hostname') do set COMPUTERNAME=%%h
set PROCESSOR_NAME=
for /f "delims=" %%p in ('powershell.exe -NoProfile -Command "(Get-CimInstance Win32_Processor | Select-Object -First 1).Name" 2^>nul') do if not defined PROCESSOR_NAME set "PROCESSOR_NAME=%%p"
rem one echo per line, delayed expansion: a value with parentheses in it
rem (every gcc version string has them) would otherwise close a block early
echo # host: !COMPUTERNAME!> out\\host.txt
echo # windows: !WINVER!>> out\\host.txt
echo # compiler: !COMPILER!>> out\\host.txt
echo # runner: run.cmd of $name>> out\\host.txt
echo # processor: !PROCESSOR_NAME!>> out\\host.txt
echo # processor-id: !PROCESSOR_IDENTIFIER!>> out\\host.txt
echo # processors: !NUMBER_OF_PROCESSORS!>> out\\host.txt
echo # date: !DATE! !TIME!>> out\\host.txt
$run_lines
echo run.cmd: probe outputs are in out\\; the policy probe follows
powershell.exe -NoProfile -ExecutionPolicy Bypass -File whp-corp-probe.ps1 -Output out\\whp-corp.txt
echo run.cmd: done; zip the out\\ directory and send it back
exit /b 0

:one
set NAME=%1
shift
set ARGS=
:args
if "%~1"=="" goto ran
set ARGS=%ARGS% %1
shift
goto args
:ran
if not exist bin\\%NAME%.exe (
	echo   %NAME%: not built, skipped
	exit /b 0
)
echo   !NAME!!ARGS!
type out\\host.txt > out\\!NAME!.txt
for /f "tokens=*" %%v in ('bin\\!NAME!.exe --version') do echo # probe: %%v>> out\\!NAME!.txt
echo # args:!ARGS!>> out\\!NAME!.txt
bin\\!NAME!.exe!ARGS! >> out\\!NAME!.txt 2> out\\!NAME!.err
if errorlevel 1 echo   %NAME%: exit code %errorlevel% ^(see out\\%NAME%.err^)
exit /b 0
EOF

# ---- the policy probe and the client's README --------------------------------
cp "$ELFSYSVNT_ROOT/spike/whp/whp-corp-probe.ps1" "$stage/"
crlf "$stage/README.txt" < "$here/README-client.txt"

# ---- the zip, and the check that nothing executable is in it ----------------
mkdir -p "$(dirname "$output")"
rm -f "$output"
( cd "$work" && zip -q -r "$output" "$name" ) || die 'zip failed'
if unzip -Z1 "$output" | grep -Eiq '\.(exe|dll|sys|msi|com|scr)$'; then
	rm -f "$output"
	die 'the bundle would have carried a binary; refused'
fi
note "$(unzip -Z1 "$output" | wc -l | tr -d ' ') files, $(du -k "$output" | cut -f1) KB"
printf '%s\n' "$output"
