#!/usr/bin/env bash
#
# What can be checked without el8, a mirror or a network.
#
# The probe's own answer needs el8's elfdeps and cannot be had offline. What
# can be had is the half this spike wrote: the emitter. Its output is meant to
# be reproducible byte for byte, so a recorded checksum is a real test -- a
# change in layout, padding or field order breaks it, and a change that was
# intended is one line to re-record.
#
# The second check reads the emitted files back with a small parser of its
# own rather than with the emitter's structures, so a field written to the
# wrong offset is caught rather than round-tripped. It asserts the handful of
# things elfdeps actually reads: the object type, DT_SONAME, DT_NEEDED, the
# verdef base node and its children, and the verneed file and its auxiliaries.
#
# The third checks what the two scripts refuse: a missing --dest, an unknown
# option, an empty --versions.
#
# Usage: run-tests.sh [-v]

set -u

here=$(cd "$(dirname "$0")" && pwd)
spike=$(dirname "$here")
verbose=0
[ "${1:-}" = "-v" ] && verbose=1

tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT

pass=0
fail=0

ok() { pass=$((pass + 1)); [ "$verbose" = 1 ] && printf 'ok   %s\n' "$1"; return 0; }
no() { fail=$((fail + 1)); printf 'FAIL %s: %s\n' "$1" "$2"; }

check() {
    if [ "$2" = "$3" ]; then ok "$1"; else no "$1" "expected [$3], got [$2]"; fi
}

rc_of() { "$@" >/dev/null 2>&1; printf '%d' "$?"; }

python3 "$spike/synth-libc.py" -q -o "$tmp/libc.so.6" || exit 1
python3 "$spike/synth-libc.py" -q -c -o "$tmp/consumer" || exit 1
python3 "$spike/synth-libc.py" -q -n GLIBC_2.2.5,GLIBC_2.14 \
    -o "$tmp/libc-two.so.6" || exit 1

# 1. The emitter is reproducible. Re-record expected-sha256.txt on purpose,
#    never to make a red check go green.
(cd "$tmp" && sha256sum libc.so.6 consumer libc-two.so.6) \
    > "$tmp/sha256.txt" 2>/dev/null
if [ -s "$tmp/sha256.txt" ]; then
    if diff -u "$here/expected-sha256.txt" "$tmp/sha256.txt" > "$tmp/sha.diff"
    then ok "emitter is byte-reproducible"
    else no "emitter is byte-reproducible" "$(cat "$tmp/sha.diff")"
    fi
else
    ok "emitter reproducibility skipped, no sha256sum"
fi

# 2. The fields elfdeps reads are where the format says they are.
python3 "$here/readback.py" "$tmp/libc.so.6" > "$tmp/lib.kv" || exit 1
python3 "$here/readback.py" "$tmp/consumer" > "$tmp/con.kv" || exit 1
python3 "$here/readback.py" "$tmp/libc-two.so.6" > "$tmp/two.kv" || exit 1

kv() { sed -n "s/^$2=//p" "$1"; }

check "library is a DSO" "$(kv "$tmp/lib.kv" type)" "ET_DYN"
check "library carries DT_SONAME" "$(kv "$tmp/lib.kv" soname)" "libc.so.6"
check "verdef base names the library" \
    "$(kv "$tmp/lib.kv" verdef_base)" "libc.so.6"
check "one verdef node" "$(kv "$tmp/lib.kv" verdef_nodes)" "GLIBC_2.2.5"
check "verdef count includes the base" \
    "$(kv "$tmp/lib.kv" verdef_count)" "2"
check "versym binds the symbol to the node" \
    "$(kv "$tmp/lib.kv" versym)" "0,2"
check "two nodes chain" "$(kv "$tmp/two.kv" verdef_nodes)" \
    "GLIBC_2.2.5,GLIBC_2.14"
check "two nodes, two symbols" "$(kv "$tmp/two.kv" versym)" "0,2,3"

check "consumer is an executable" "$(kv "$tmp/con.kv" type)" "ET_EXEC"
check "consumer needs the library" "$(kv "$tmp/con.kv" needed)" "libc.so.6"
check "consumer has no soname" "$(kv "$tmp/con.kv" soname)" ""
check "verneed names the library" \
    "$(kv "$tmp/con.kv" verneed_file)" "libc.so.6"
check "verneed asks for the node" \
    "$(kv "$tmp/con.kv" verneed_nodes)" "GLIBC_2.2.5"

# 3. What the scripts refuse. A tool that accepts a typo silently is worse
#    than one that stops.
check "emitter refuses an empty --versions" \
    "$(rc_of python3 "$spike/synth-libc.py" -n , -o "$tmp/x")" "2"
check "emitter refuses an unknown option" \
    "$(rc_of python3 "$spike/synth-libc.py" --nonesuch)" "2"
check "fetch refuses a missing --dest" \
    "$(rc_of bash "$spike/fetch-elfdeps.sh")" "2"
check "fetch refuses an unknown option" \
    "$(rc_of bash "$spike/fetch-elfdeps.sh" --dest "$tmp" --nonesuch)" "2"
check "probe refuses a missing --dest" \
    "$(rc_of bash "$spike/probe-elfdeps.sh")" "2"
check "probe refuses a dest with no elfdeps in it" \
    "$(rc_of bash "$spike/probe-elfdeps.sh" --dest "$tmp")" "1"

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" = 0 ] || exit 1
exit 0
