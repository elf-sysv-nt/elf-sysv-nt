#!/bin/sh
# The real forward map against the real slice map (WP-56).
#
# Everything before this judged the wiring machinery over fixtures. This
# derives libc-forward.tsv the same way build-libc does -- generate.py
# over the committed WP-51/WP-52 inputs -- and certifies that the slice
# map accounts for the map's wired function rows, pinning the counts so
# a drift in either input is a conscious re-pin, not a silent shift.
set -e
cd "$(dirname "$0")"
root=$(cd ../../.. && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 "$root/veneer/libc/generate.py" --soname libc.so.6 \
    --map "$root/veneer/version-map/glibc-version-map.tsv" \
    --nodes "$root/veneer/version-map/glibc-version-nodes.tsv" \
    --classification "$root/veneer/classification/classification.tsv" \
    --asm "$tmp/sym.s" --version-script "$tmp/libc.map" \
    --forward-map "$tmp/fwd.tsv" --static-asm "$tmp/static.s" \
    >/dev/null

# The wired dispositions, counted from the derived map.
count() { awk -F'\t' -v d="$1" '$7==d' "$tmp/fwd.tsv" | wc -l; }
test "$(count forward-same)" = 1077
test "$(count forward-alias)" = 185
test "$(count shim)" = 136

# Wired function rows the slice map does not place. 275 today: the
# underscore internals no public header declares, gets (hidden from a
# _GNU_SOURCE scan, as el8's stdio.h is C11), and the SunRPC xdr_*
# family, whose headers el8 moved out of glibc into libtirpc.
awk -F'\t' 'NR==FNR{m[$1]=1;next}
    ($7=="forward-same"||$7=="forward-alias"||$7=="shim") &&
    ($4=="func"||$4=="ifunc") && !($1 in m){print $1}' \
    ../symbol-slice.tsv "$tmp/fwd.tsv" | sort -u > "$tmp/unassigned.txt"
test "$(wc -l < "$tmp/unassigned.txt")" = 275
# and every non-underscore one is on the two documented residues
if grep -v '^_' "$tmp/unassigned.txt" | grep -v '^xdr' | grep -qvx gets
then
    echo 'real-map: a new public symbol fell into unassigned:' >&2
    grep -v '^_' "$tmp/unassigned.txt" | grep -v '^xdr' | grep -vx gets >&2
    exit 1
fi

# The committed string-slice wiring re-derives byte-identical from the
# same inputs; a drift in either input is a conscious regeneration.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice string \
    --table "$tmp/wire-string.gen.c" --thunks "$tmp/wire-string.gen.s" \
    --shims "$tmp/wire-string.shims.tsv" >/dev/null
cmp ../wire-string.gen.c "$tmp/wire-string.gen.c"
cmp ../wire-string.gen.s "$tmp/wire-string.gen.s"
cmp ../wire-string.shims.tsv "$tmp/wire-string.shims.tsv"

# The committed stdio wiring re-derives byte-identical too, and when the
# cross toolchain is present its thunks assemble and carry their
# versioned names.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice stdio \
    --table "$tmp/wire-stdio.gen.c" --thunks "$tmp/wire-stdio.gen.s" \
    --shims "$tmp/wire-stdio.shims.tsv" >/dev/null
cmp ../wire-stdio.gen.c "$tmp/wire-stdio.gen.c"
cmp ../wire-stdio.gen.s "$tmp/wire-stdio.gen.s"
cmp ../wire-stdio.shims.tsv "$tmp/wire-stdio.shims.tsv"
grep -q '"fopen"' "$tmp/wire-stdio.gen.c"
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-stdio-table.o" "$tmp/wire-stdio.gen.c"
XCC="${XCC:-x86_64-elfsysvnt-linux-gnu-gcc}"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-stdio.o" "$tmp/wire-stdio.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-stdio.o" | grep -q 'fopen@@GLIBC_2.2.5'
fi

# The committed posix wiring re-derives byte-identical as well, and
# when the cross toolchain is present its thunks assemble and carry
# their versioned names.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice posix \
    --table "$tmp/wire-posix.gen.c" --thunks "$tmp/wire-posix.gen.s" \
    --shims "$tmp/wire-posix.shims.tsv" >/dev/null
cmp ../wire-posix.gen.c "$tmp/wire-posix.gen.c"
cmp ../wire-posix.gen.s "$tmp/wire-posix.gen.s"
cmp ../wire-posix.shims.tsv "$tmp/wire-posix.shims.tsv"
grep -q '"execvp"' "$tmp/wire-posix.gen.c"
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-posix-table.o" "$tmp/wire-posix.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-posix.o" "$tmp/wire-posix.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-posix.o" | grep -q 'execvp@@GLIBC_2.2.5'
fi

# The committed stdlib wiring re-derives byte-identical as well, and
# when the cross toolchain is present its thunks assemble and carry
# their versioned names.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice stdlib \
    --table "$tmp/wire-stdlib.gen.c" --thunks "$tmp/wire-stdlib.gen.s" \
    --shims "$tmp/wire-stdlib.shims.tsv" >/dev/null
cmp ../wire-stdlib.gen.c "$tmp/wire-stdlib.gen.c"
cmp ../wire-stdlib.gen.s "$tmp/wire-stdlib.gen.s"
cmp ../wire-stdlib.shims.tsv "$tmp/wire-stdlib.shims.tsv"
grep -q '"strtol"' "$tmp/wire-stdlib.gen.c"
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-stdlib-table.o" "$tmp/wire-stdlib.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-stdlib.o" "$tmp/wire-stdlib.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-stdlib.o" | grep -q 'strtol@@GLIBC_2.2.5'
fi

echo 'real-map: ok'

# The committed filesystem wiring re-derives byte-identical as well.
# This is the first slice with a real shim worklist: the stat family
# and friends cross with layout-bearing structs, so their rows land in
# the shims file rather than as thunks, and the counts are pinned.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice filesystem \
    --table "$tmp/wire-filesystem.gen.c" \
    --thunks "$tmp/wire-filesystem.gen.s" \
    --shims "$tmp/wire-filesystem.shims.tsv" >/dev/null
cmp ../wire-filesystem.gen.c "$tmp/wire-filesystem.gen.c"
cmp ../wire-filesystem.gen.s "$tmp/wire-filesystem.gen.s"
cmp ../wire-filesystem.shims.tsv "$tmp/wire-filesystem.shims.tsv"
grep -q '"mkdir"' "$tmp/wire-filesystem.gen.c"
grep -q '^__xstat	' "$tmp/wire-filesystem.shims.tsv"
test "$(grep -vc '^#' "$tmp/wire-filesystem.shims.tsv")" = 36
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-filesystem-table.o" "$tmp/wire-filesystem.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-filesystem.o" "$tmp/wire-filesystem.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-filesystem.o" | grep -q 'mkdir@@GLIBC_2.2.5'
fi

# The committed memory wiring re-derives byte-identical as well. The
# slice is small and all thunks: malloc and its family forward whole,
# and the mmap family's flags translate downstream of the bind, so no
# row lands in the shims file yet.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice memory \
    --table "$tmp/wire-memory.gen.c" \
    --thunks "$tmp/wire-memory.gen.s" \
    --shims "$tmp/wire-memory.shims.tsv" >/dev/null
cmp ../wire-memory.gen.c "$tmp/wire-memory.gen.c"
cmp ../wire-memory.gen.s "$tmp/wire-memory.gen.s"
cmp ../wire-memory.shims.tsv "$tmp/wire-memory.shims.tsv"
grep -q '"malloc"' "$tmp/wire-memory.gen.c"
grep -q '"mmap"' "$tmp/wire-memory.gen.c"
test "$(grep -vc '^#' "$tmp/wire-memory.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-memory-table.o" "$tmp/wire-memory.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-memory.o" "$tmp/wire-memory.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-memory.o" | grep -q 'malloc@@GLIBC_2.2.5'
fi

# The committed sockets wiring re-derives byte-identical as well. The
# slice is all thunks: the sockaddr family crosses by pointer and
# length, laid out the same on both sides, and the flag translation
# the mmap family taught us belongs downstream of the bind here too,
# so no row lands in the shims file yet.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice sockets \
    --table "$tmp/wire-sockets.gen.c" \
    --thunks "$tmp/wire-sockets.gen.s" \
    --shims "$tmp/wire-sockets.shims.tsv" >/dev/null
cmp ../wire-sockets.gen.c "$tmp/wire-sockets.gen.c"
cmp ../wire-sockets.gen.s "$tmp/wire-sockets.gen.s"
cmp ../wire-sockets.shims.tsv "$tmp/wire-sockets.shims.tsv"
grep -q '"socket"' "$tmp/wire-sockets.gen.c"
grep -q '"getaddrinfo"' "$tmp/wire-sockets.gen.c"
test "$(grep -vc '^#' "$tmp/wire-sockets.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-sockets-table.o" "$tmp/wire-sockets.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-sockets.o" "$tmp/wire-sockets.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-sockets.o" | grep -q 'socket@@GLIBC_2.2.5'
fi

# The committed locale wiring re-derives byte-identical as well. The
# slice is all thunks: the ctype classifications and their _l twins
# cross by value, locale_t is an opaque pointer on both sides, and
# setlocale's category values translate downstream of the bind, so no
# row lands in the shims file yet.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice locale \
    --table "$tmp/wire-locale.gen.c" \
    --thunks "$tmp/wire-locale.gen.s" \
    --shims "$tmp/wire-locale.shims.tsv" >/dev/null
cmp ../wire-locale.gen.c "$tmp/wire-locale.gen.c"
cmp ../wire-locale.gen.s "$tmp/wire-locale.gen.s"
cmp ../wire-locale.shims.tsv "$tmp/wire-locale.shims.tsv"
grep -q '"setlocale"' "$tmp/wire-locale.gen.c"
grep -q '"isalpha"' "$tmp/wire-locale.gen.c"
test "$(grep -vc '^#' "$tmp/wire-locale.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-locale-table.o" "$tmp/wire-locale.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-locale.o" "$tmp/wire-locale.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-locale.o" | grep -q 'setlocale@@GLIBC_2.2.5'
fi

# The committed time wiring re-derives byte-identical as well. The
# slice is all thunks: struct tm and struct timespec lay out the same
# on both sides and cross by pointer, and the clockid and itimer
# value translations belong downstream of the bind, so no row lands
# in the shims file yet.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice time \
    --table "$tmp/wire-time.gen.c" \
    --thunks "$tmp/wire-time.gen.s" \
    --shims "$tmp/wire-time.shims.tsv" >/dev/null
cmp ../wire-time.gen.c "$tmp/wire-time.gen.c"
cmp ../wire-time.gen.s "$tmp/wire-time.gen.s"
cmp ../wire-time.shims.tsv "$tmp/wire-time.shims.tsv"
grep -q '"clock_gettime"' "$tmp/wire-time.gen.c"
grep -q '"strftime"' "$tmp/wire-time.gen.c"
test "$(grep -vc '^#' "$tmp/wire-time.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-time-table.o" "$tmp/wire-time.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-time.o" "$tmp/wire-time.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-time.o" | grep -q 'strftime@@GLIBC_2.2.5'
fi

# The committed signal wiring re-derives byte-identical as well. The
# sigset_t bearers -- sigaction, the set-manipulation family,
# sigprocmask, the wait family, sigaltstack and both sysv_signal
# spellings -- cross with layout-bearing structs, so their rows land
# in the shims file rather than as thunks, and the counts are pinned.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice signal \
    --table "$tmp/wire-signal.gen.c" \
    --thunks "$tmp/wire-signal.gen.s" \
    --shims "$tmp/wire-signal.shims.tsv" >/dev/null
cmp ../wire-signal.gen.c "$tmp/wire-signal.gen.c"
cmp ../wire-signal.gen.s "$tmp/wire-signal.gen.s"
cmp ../wire-signal.shims.tsv "$tmp/wire-signal.shims.tsv"
grep -q '"kill"' "$tmp/wire-signal.gen.c"
grep -q '^sigaction	' "$tmp/wire-signal.shims.tsv"
test "$(grep -vc '^#' "$tmp/wire-signal.shims.tsv")" = 16
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-signal-table.o" "$tmp/wire-signal.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-signal.o" "$tmp/wire-signal.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-signal.o" | grep -q 'kill@@GLIBC_2.2.5'
fi

# The committed process wiring re-derives byte-identical as well. The
# rlimit family -- getrlimit and setrlimit with their 64 spellings --
# carries struct rlimit across the boundary, so its four rows land in
# the shims file; the spawn, sched, wait, priority and rusage names
# cross as thunks, and the counts are pinned.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice process \
    --table "$tmp/wire-process.gen.c" \
    --thunks "$tmp/wire-process.gen.s" \
    --shims "$tmp/wire-process.shims.tsv" >/dev/null
cmp ../wire-process.gen.c "$tmp/wire-process.gen.c"
cmp ../wire-process.gen.s "$tmp/wire-process.gen.s"
cmp ../wire-process.shims.tsv "$tmp/wire-process.shims.tsv"
grep -q '"waitpid"' "$tmp/wire-process.gen.c"
grep -q '^getrlimit	' "$tmp/wire-process.shims.tsv"
test "$(grep -vc '^#' "$tmp/wire-process.shims.tsv")" = 4
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-process-table.o" "$tmp/wire-process.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-process.o" "$tmp/wire-process.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-process.o" | grep -q 'waitpid@@GLIBC_2.2.5'
fi

# The committed identity wiring re-derives byte-identical as well. The
# pwd and grp families -- lookup, iteration, the _r variants -- and the
# group-membership trio cross as thunks; nothing in the slice carries a
# struct by translation, so the shims file is empty and pinned so.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice identity \
    --table "$tmp/wire-identity.gen.c" \
    --thunks "$tmp/wire-identity.gen.s" \
    --shims "$tmp/wire-identity.shims.tsv" >/dev/null
cmp ../wire-identity.gen.c "$tmp/wire-identity.gen.c"
cmp ../wire-identity.gen.s "$tmp/wire-identity.gen.s"
cmp ../wire-identity.shims.tsv "$tmp/wire-identity.shims.tsv"
grep -q '"getpwuid_r"' "$tmp/wire-identity.gen.c"
test "$(grep -c '^    { "' "$tmp/wire-identity.gen.c")" = 17
test "$(grep -vc '^#' "$tmp/wire-identity.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-identity-table.o" "$tmp/wire-identity.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-identity.o" "$tmp/wire-identity.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-identity.o" | grep -q 'getpwnam@@GLIBC_2.2.5'
fi

# The committed io-mux wiring re-derives byte-identical as well. The
# readiness families -- select/pselect, poll/ppoll -- and the fd-based
# event carriers signalfd and the timerfd trio cross as thunks; nothing
# translates a struct, so the shims file is empty and pinned so.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice io-mux \
    --table "$tmp/wire-io-mux.gen.c" \
    --thunks "$tmp/wire-io-mux.gen.s" \
    --shims "$tmp/wire-io-mux.shims.tsv" >/dev/null
cmp ../wire-io-mux.gen.c "$tmp/wire-io-mux.gen.c"
cmp ../wire-io-mux.gen.s "$tmp/wire-io-mux.gen.s"
cmp ../wire-io-mux.shims.tsv "$tmp/wire-io-mux.shims.tsv"
grep -q '"timerfd_settime"' "$tmp/wire-io-mux.gen.c"
test "$(grep -c '^    { "' "$tmp/wire-io-mux.gen.c")" = 8
test "$(grep -vc '^#' "$tmp/wire-io-mux.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-io-mux-table.o" "$tmp/wire-io-mux.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-io-mux.o" "$tmp/wire-io-mux.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-io-mux.o" | grep -q 'poll@@GLIBC_2.2.5'
fi

# The committed terminal wiring re-derives byte-identical as well. The
# termios surface -- cf* speeds, tc* control -- and the utmp/utmpx
# session records cross as thunks; ioctl is the one shim, its request
# codes a translation, not a jump. The pty helpers (openpty, forkpty,
# login and kin) live in libutil on el8, so the forward map never
# carries them and they are not this slice's rows.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice terminal \
    --table "$tmp/wire-terminal.gen.c" \
    --thunks "$tmp/wire-terminal.gen.s" \
    --shims "$tmp/wire-terminal.shims.tsv" >/dev/null
cmp ../wire-terminal.gen.c "$tmp/wire-terminal.gen.c"
cmp ../wire-terminal.gen.s "$tmp/wire-terminal.gen.s"
cmp ../wire-terminal.shims.tsv "$tmp/wire-terminal.shims.tsv"
grep -q '"tcsetattr"' "$tmp/wire-terminal.gen.c"
test "$(grep -c '^    { "' "$tmp/wire-terminal.gen.c")" = 30
test "$(grep -vc '^#' "$tmp/wire-terminal.shims.tsv")" = 1
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-terminal-table.o" "$tmp/wire-terminal.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-terminal.o" "$tmp/wire-terminal.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-terminal.o" | grep -q 'tcgetattr@@GLIBC_2.2.5'
fi

# The committed misc wiring re-derives byte-identical as well. The
# grab-bag headers -- getopt, the err/warn and error reporters,
# dirname, the search trees and tables, wordexp, and the random-byte
# pair -- cross as thunks with no shims. basename is the string
# slice's row by attribution (string.h declares it and outranks
# libgen.h), and __xpg_basename is not a wired disposition, so
# neither is counted here.
python3 ../gen-wire.py --forward-map "$tmp/fwd.tsv" \
    --slice-map ../symbol-slice.tsv --slice misc \
    --table "$tmp/wire-misc.gen.c" \
    --thunks "$tmp/wire-misc.gen.s" \
    --shims "$tmp/wire-misc.shims.tsv" >/dev/null
cmp ../wire-misc.gen.c "$tmp/wire-misc.gen.c"
cmp ../wire-misc.gen.s "$tmp/wire-misc.gen.s"
cmp ../wire-misc.shims.tsv "$tmp/wire-misc.shims.tsv"
grep -q '"getopt_long"' "$tmp/wire-misc.gen.c"
test "$(grep -c '^    { "' "$tmp/wire-misc.gen.c")" = 33
test "$(grep -vc '^#' "$tmp/wire-misc.shims.tsv")" = 0
"${CC:-gcc}" -Wall -Wextra -Werror -c -I.. \
    -o "$tmp/wire-misc-table.o" "$tmp/wire-misc.gen.c"
if command -v "$XCC" >/dev/null 2>&1; then
    "$XCC" -c -o "$tmp/wire-misc.o" "$tmp/wire-misc.gen.s"
    "${XCC%gcc}nm" "$tmp/wire-misc.o" | grep -q 'getopt@@GLIBC_2.2.5'
fi
