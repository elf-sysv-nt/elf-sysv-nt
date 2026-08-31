#!/bin/sh
# Launch or resume the demand census detached, so it survives the session
# that started it. Safe to run again at any time: census.py skips packages
# with a done marker, and a second live run is refused by the pid check.
#
# Usage: sh run-census.sh   (from spike/demand-census)

here=$(cd "$(dirname "$0")" && pwd)
root=/c/-/repo/elf-sysv-nt/a/census-work
log=/c/-/repo/elf-sysv-nt/a/build-logs/wp56-wiring-bodies.log
pidfile=$root/census.pid

mkdir -p "$root" /c/-/repo/elf-sysv-nt/a/build-logs

if [ -s "$pidfile" ] && kill -0 "$(cat "$pidfile")" 2>/dev/null; then
    echo "census already running, pid $(cat "$pidfile")"
    exit 0
fi

cd "$here"
if [ ! -s "$root/worklist.tsv" ]; then
    python3 census.py enumerate -o "$root/worklist.tsv" >> "$log" 2>&1
fi

nohup python3 census.py run --worklist "$root/worklist.tsv" \
    --root "$root" --jobs 4 >> "$log" 2>&1 &
echo $! > "$pidfile"
echo "census running, pid $(cat "$pidfile"), log $log"
