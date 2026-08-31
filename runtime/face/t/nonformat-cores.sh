#!/usr/bin/env bash
# WP-27: certify the nonformat core binding.
#
# The same three properties t/cores.sh holds the format cores to.
# nonformat-cores.c compiles clean with the winsup flags; it defines exactly
# the __core_* surface nonformat.c declares, no more and no less; and every
# symbol it leaves undefined has a body in the WP-26 DLL. The last check
# needs the built DLL and reports SKIP when no build exists yet.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
face_dir=$here/..
varargs_dir=$face_dir/../varargs
dll=/c/-/repo/elf-sysv-nt/a/build/wp26/x86_64-pc-cygwin/winsup/cygwin/new-cygwin1.dll
fail=0
say() { printf '%s\n' "$*"; }
bad() { say "FAIL: $*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# 1. compiles clean
if gcc -g -O2 -mno-red-zone -Wall -Werror -c "$face_dir/nonformat-cores.c" \
       -o "$tmp/nfc.o" 2> "$tmp/cc.err"; then
  say "ok: nonformat-cores.c compiles clean"
else
  bad "nonformat-cores.c does not compile: $(head -1 "$tmp/cc.err")"
fi

# 2. exactly the surface nonformat.c declares (its cores never carry a v)
grep -o '__core_[a-z_]*(' "$varargs_dir/nonformat.c" | tr -d '(' \
  | sort -u > "$tmp/want"
nm "$tmp/nfc.o" 2>/dev/null | awk '$2=="T"{print $3}' | grep '^__core_' \
  | sort > "$tmp/have" || true
if [ -s "$tmp/have" ] && cmp -s "$tmp/want" "$tmp/have"; then
  say "ok: defines exactly nonformat.c's $(wc -l < "$tmp/want") cores"
else
  bad "core surface mismatch: $(comm -3 "$tmp/want" "$tmp/have" | head -3 | tr '\n' ' ')"
fi

# 3. every undefined reference has a body in the DLL
if [ ! -f "$dll" ]; then
  say "SKIP: no WP-26 DLL at $dll; undefined-reference check not run"
elif [ -s "$tmp/nfc.o" ]; then
  nm --defined-only "$dll" | awk '{print $3}' | sort -u > "$tmp/dllsyms"
  miss=$(nm -u "$tmp/nfc.o" | awk '{print $2}' \
         | grep -vxF -f "$tmp/dllsyms" || true)
  if [ -z "$miss" ]; then
    say "ok: every undefined reference resolves in the DLL"
  else
    bad "unresolved in the DLL: $(echo $miss | head -c 200)"
  fi
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
