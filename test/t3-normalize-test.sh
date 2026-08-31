#!/usr/bin/env bash
# Unit test for t3-regen's findings() normalize: numeric drift must not read as
# rot, but a changed verdict word or a truncated transcript must.
set -u
findings() {
	grep -avE '^[[:space:]]*($|#|Captured|Generated)|^[[:space:]]*(kernel|compiler|date|run_date|host|hostname|toolchain|uname|os|platform|ld|readelf|nm|objdump)([[:space:]]{2,}|=)|MEM_(FREE|COMMIT|RESERVE)' "$1" 2>/dev/null \
	| sed -E '
	    s@[^[:space:]]*[/\\][Tt][Mm][Pp][/\\][^[:space:]]*@TMPPATH@g;
	    s/0x[0-9a-fA-F]+/0xN/g;
	    s/[0-9]{4}-[0-9]{2}-[0-9]{2}(T[0-9:]+Z?)?/DATE/g;
	    s/[0-9]+\.[0-9]+/N/g;
	    s/[0-9]+/N/g;
	    s/[[:space:]]+/ /g;
	    s/^ //; s/ $//' \
	| sort
}
here=$(cd "$(dirname "$0")" && pwd); root=$(cd "$here/.." && pwd)
C=$(ls -t "$root"/spike/redzone-delivery/results-*.txt | head -1)
echo "baseline: $C"
rc=0

sed -E 's/words:[0-9]+/words:99999/g; s/gcc \(GCC\) 7\.4\.0/gcc (GCC) 14.4.0/g; s/[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]+Z/2099-01-01T00:00:00Z/g' "$C" > /tmp/perturbed.txt
if [ "$(findings "$C")" = "$(findings /tmp/perturbed.txt)" ]; then
	echo "PERTURB     match  (correct: numeric drift and a compiler bump are not rot)"
else
	echo "PERTURB     DIFFER (BUG)"; diff <(findings "$C") <(findings /tmp/perturbed.txt); rc=1
fi

sed -E 's/red zone intact/red zone CLOBBERED/g; s/=pass,/=fail,/g' "$C" > /tmp/rotverdict.txt
if [ "$(findings "$C")" != "$(findings /tmp/rotverdict.txt)" ]; then
	echo "VERDICT-ROT caught (correct: a changed verdict word is rot)"
else
	echo "VERDICT-ROT MISSED (BUG)"; rc=1
fi

head -8 "$C" > /tmp/trunc.txt
if [ "$(findings "$C")" != "$(findings /tmp/trunc.txt)" ]; then
	echo "TRUNCATION  caught (correct: a script that died mid-run is rot)"
else
	echo "TRUNCATION  MISSED (BUG)"; rc=1
fi

exit $rc
