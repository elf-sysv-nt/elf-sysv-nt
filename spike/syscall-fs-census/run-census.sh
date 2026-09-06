#!/bin/sh
# Launch or resume the syscall census detached, so it survives the session
# that started it. Safe to run again at any time: census.py skips packages
# with a done marker, and a second live run is refused by the pid check.
# The worklist is the demand census's (name, url, size, every x86_64 package
# in Rocky 8.10's four repositories), copied in if this root has none; the
# -devel, -doc, -static and similar names are skipped, since they carry no
# runnable text.
#
# Usage: sh run-census.sh   (from spike/syscall-fs-census)

here=$(cd "$(dirname "$0")" && pwd)
. "$here/../../bin/roots.sh"
root=$ELFSYSVNT_ROOT/a/census-work/syscall-fs
log=$ELFSYSVNT_ROOT/a/build-logs/syscall-fs-census.log
pidfile=$root/census.pid

mkdir -p "$root" "$ELFSYSVNT_ROOT/a/build-logs"
if [ -s "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
    echo "census already running, pid $(cat "$pidfile")"
    exit 0
fi
if [ ! -s "$root/worklist.tsv" ]; then
    if [ -s "$ELFSYSVNT_EL8/demand-census/worklist.tsv" ]; then
        cp "$ELFSYSVNT_EL8/demand-census/worklist.tsv" "$root/worklist.tsv"
    else
        python3 "$here/../demand-census/census.py" enumerate -o "$root/worklist.tsv" >> "$log" 2>&1
    fi
fi
cd "$here"
nohup python3 census.py run --worklist "$root/worklist.tsv" --root "$root" --jobs 8 --filter >> "$log" 2>&1 &
echo $! > "$pidfile"
echo "census started, pid $!, log $log"
