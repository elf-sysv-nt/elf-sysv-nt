#!/usr/bin/env bash
# WP-27: certify the core binding.
#
# Three properties. cores.c compiles clean with the winsup flags; it
# defines exactly the __core_* surface core.h declares, no more and no
# less; and every symbol it leaves undefined is defined in the WP-26 DLL,
# so the veneer's repass has a body behind it for all 25 cores. The last
# check needs the built DLL and reports SKIP when no build exists yet.
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
if gcc -g -O2 -mno-red-zone -Wall -Werror -c "$face_dir/cores.c" \
       -o "$tmp/cores.o" 2> "$tmp/cc.err"; then
  say "ok: cores.c compiles clean"
else
  bad "cores.c does not compile: $(head -1 "$tmp/cc.err")"
fi

# 2. exactly the surface core.h declares
grep -o '__core_v[a-z_]*(' "$varargs_dir/core.h" | tr -d '(' | sort -u > "$tmp/want"
nm "$tmp/cores.o" 2>/dev/null | awk '$2=="T"{print $3}' | grep '^__core_' \
  | sort > "$tmp/have" || true
if [ -s "$tmp/have" ] && cmp -s "$tmp/want" "$tmp/have"; then
  say "ok: defines exactly core.h's $(wc -l < "$tmp/want") cores"
else
  bad "core surface mismatch: $(comm -3 "$tmp/want" "$tmp/have" | head -3 | tr '\n' ' ')"
fi

# 3. every undefined reference has a body in the DLL
if [ ! -f "$dll" ]; then
  say "SKIP: no WP-26 DLL at $dll; undefined-reference check not run"
elif [ -s "$tmp/cores.o" ]; then
  nm --defined-only "$dll" | awk '{print $3}' | sort -u > "$tmp/dllsyms"
  miss=$(nm -u "$tmp/cores.o" | awk '{print $2}' \
         | grep -vxF -f "$tmp/dllsyms" || true)
  if [ -z "$miss" ]; then
    say "ok: every undefined reference resolves in the DLL"
  else
    bad "unresolved in the DLL: $(echo $miss | head -c 200)"
  fi
fi

if [ "$fail" = 0 ]; then say "verdict: yes"; else say "verdict: no"; exit 1; fi
