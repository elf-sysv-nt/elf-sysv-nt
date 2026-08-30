#!/usr/bin/env bash
# WP-63's Done-when, exercised end to end: two runs with changed inputs
# leave only the new state, an injected stale value is cleared, a retired
# item is gone.  Plus the two failure shapes worth their own cases: a
# hostile manifest must not reach outside the root, and a second run with
# unchanged inputs must change nothing at all.

set -u
here=$(cd "$(dirname "$0")" && pwd)
inst=$here/../elf-install

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
pass=0; fail=0

ok()  { pass=$((pass+1)); }
no()  { fail=$((fail+1)); echo "FAIL: $*"; }
chk() { # DESC CMD...
  local d=$1; shift
  if "$@" >/dev/null 2>&1; then ok; else no "$d"; fi
}

root=$tmp/root; src=$tmp/src
mkdir -p "$root" "$src"
echo v1-loader  > "$src/loader.v1"
echo v1-libfoo  > "$src/libfoo.v1"
echo v1-data    > "$src/data.v1"
echo v2-libfoo  > "$src/libfoo.v2"
echo v2-ldd     > "$src/ldd.v2"

cat > "$tmp/payload.a" <<EOF
# revision A
755 $src/loader.v1 usr/bin/elf-exec
644 $src/libfoo.v1 usr/lib64/libfoo.so.1
644 $src/data.v1 usr/share/elfsysvnt/data
EOF

cat > "$tmp/payload.b" <<EOF
755 $src/loader.v1 usr/bin/elf-exec
644 $src/libfoo.v2 usr/lib64/libfoo.so.1
755 $src/ldd.v2 usr/bin/elf-ldd
EOF

# --- run 1: revision A ------------------------------------------------------
"$inst" -R "$root" -p "$tmp/payload.a" -r revA -c no-such-ldconfig 2>/dev/null \
  || no "run 1 exited nonzero"

chk "loader installed"      test -x "$root/usr/bin/elf-exec"
chk "libfoo installed"      grep -q v1-libfoo "$root/usr/lib64/libfoo.so.1"
chk "data installed"        grep -q v1-data "$root/usr/share/elfsysvnt/data"
chk "ld.so.conf seeded"     grep -q 'include ld.so.conf.d/\*.conf' "$root/etc/ld.so.conf"
chk "drop-in names lib64"   grep -qx /usr/lib64 "$root/etc/ld.so.conf.d/elfsysvnt.conf"
chk "marker carries revA"   grep -q revA "$root/etc/elfsysvnt-release"
chk "manifest written"      test -s "$root/etc/elfsysvnt/manifest"

# --- run 1 again, unchanged: byte-for-byte the same tree --------------------
snap() { (cd "$root" && find . -type f | sort | xargs md5sum); }
before=$(snap)
"$inst" -R "$root" -p "$tmp/payload.a" -r revA -c no-such-ldconfig 2>/dev/null \
  || no "repeat run exited nonzero"
after=$(snap)
[ "$before" = "$after" ] && ok || no "repeat run with unchanged inputs changed the tree"

# --- sabotage, then run 2: revision B ---------------------------------------
echo /opt/stale-injected >> "$root/etc/ld.so.conf.d/elfsysvnt.conf"
echo stale-marker-edit   >> "$root/etc/elfsysvnt-release"
echo half-written > "$root/usr/bin/stranded.elf-install-tmp"

"$inst" -R "$root" -p "$tmp/payload.b" -r revB -c no-such-ldconfig \
  -L usr/lib64 -L opt/elfsysvnt/lib 2>/dev/null \
  || no "run 2 exited nonzero"

chk "retired data removed"  test ! -e "$root/usr/share/elfsysvnt/data"
chk "emptied dirs pruned"   test ! -d "$root/usr/share/elfsysvnt"
chk "libfoo replaced"       grep -q v2-libfoo "$root/usr/lib64/libfoo.so.1"
chk "elf-ldd added"         test -x "$root/usr/bin/elf-ldd"
chk "stale conf line gone"  test "$(grep -c stale-injected "$root/etc/ld.so.conf.d/elfsysvnt.conf")" = 0
chk "second libdir present" grep -qx /opt/elfsysvnt/lib "$root/etc/ld.so.conf.d/elfsysvnt.conf"
chk "marker reseeded"       test "$(grep -c stale-marker-edit "$root/etc/elfsysvnt-release")" = 0
chk "marker carries revB"   grep -q revB "$root/etc/elfsysvnt-release"
chk "stranded tmp reaped"   test ! -e "$root/usr/bin/stranded.elf-install-tmp"
chk "manifest names elf-ldd" grep -qx usr/bin/elf-ldd "$root/etc/elfsysvnt/manifest"
chk "manifest dropped data"  test "$(grep -c 'usr/share/elfsysvnt/data' "$root/etc/elfsysvnt/manifest")" = 0

# --- a hostile manifest must not reach past the root ------------------------
canary=$tmp/canary; echo precious > "$canary"
root2=$tmp/root2; mkdir -p "$root2/etc/elfsysvnt"
printf '%s\n' ../canary /etc/hosts 'a/../../canary' \
  > "$root2/etc/elfsysvnt/manifest"
"$inst" -R "$root2" -r revX -c no-such-ldconfig 2>"$tmp/warns" \
  || no "hostile-manifest run exited nonzero"
chk "canary outside root survives" grep -q precious "$canary"
chk "refusal is warned about"      grep -q refused "$tmp/warns"

# --- a payload destination may not escape either ----------------------------
printf '755 %s ../escape\n' "$src/loader.v1" > "$tmp/payload.evil"
if "$inst" -R "$root2" -p "$tmp/payload.evil" -c no-such-ldconfig \
    >/dev/null 2>&1; then
  no "escaping payload destination was accepted"
else
  ok
fi
chk "no file escaped root2" test ! -e "$tmp/escape"

# --- exclusions derive from the inputs --------------------------------------
"$inst" -R "$root" -p "$tmp/payload.b" -x > "$tmp/excl" || no "-x exited nonzero"
chk "exclusions name the loader"  grep -q 'usr/bin/elf-exec' "$tmp/excl"
chk "exclusions name the libdir"  grep -q 'usr/lib64' "$tmp/excl"

echo "pass $pass fail $fail"
[ $fail = 0 ]
