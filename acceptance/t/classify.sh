#!/usr/bin/env bash
#
# WP-56 -- unit-test the acceptance classifier (classify.awk). The bar this
# increment adds: a bucket-4 stub named in the filled manifest reports as
# filled, and a bucket-4 stub that is not reports as stub (DR-0052). Also
# checks the other dispositions and that the distinction turns on the manifest
# alone. Host only -- no network, no toolchain, no image.
#
# Exit: 0 all checks passed, 1 a divergence.
set -u
here=$(cd "$(dirname "$0")" && pwd)
awkf=$here/../classify.awk
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
fail=0

# A classification fixture: one row per disposition, tabs via printf so no
# literal tab has to survive this heredoc.
classif=$tmp/classif.tsv
{
printf 'libc.so.6\tfwd_same\tGLIBC_2.2.5\t2\tforward-same\t-\t-\tx\n'
printf 'libc.so.6\tfwd_alias\tGLIBC_2.2.5\t1\tforward-alias\t-\t-\tx\n'
printf 'libc.so.6\ta_shim\tGLIBC_2.2.5\t3\tshim\t-\t-\tx\n'
printf 'libc.so.6\tfilled_one\tGLIBC_2.3\t4\tstub\t-\t-\tabsent\n'
printf 'libc.so.6\tplain_stub\tGLIBC_2.3\t4\tstub\t-\t-\tabsent\n'
printf 'libc.so.6\ta_scaffold\tGLIBC_2.14\tscaffold\tscaffold\t-\t-\tnode\n'
} > "$classif"

# A filled manifest naming one of the two bucket-4 stubs, with comment lines.
filled=$tmp/filled.tsv
{
printf '# a filled manifest\n'
printf '# symbol\tversion\tbody\tcertified-by\n'
printf 'filled_one\tGLIBC_2.3\tbody.c\tt.sh\n'
} > "$filled"

# The binary needs every mapped name plus one the map does not know.
needs=$tmp/needs
printf '%s\n' fwd_same fwd_alias a_shim filled_one plain_stub a_scaffold not_mapped > "$needs"

got=$(awk -v filled="$filled" -v needs="$needs" -f "$awkf" "$filled" "$needs" "$classif" | sort)
want=$(printf '%s\n' \
'filled filled_one' \
'forward fwd_alias' \
'forward fwd_same' \
'shim a_shim' \
'stub plain_stub' \
'unclassified a_scaffold' \
'unclassified not_mapped' | sort)

if [ "$got" = "$want" ]; then
echo "classify: every disposition correct; a filled stub is not a failing stub"
else
echo "classify: FAIL -- dispositions diverged" >&2
diff <(printf '%s\n' "$want") <(printf '%s\n' "$got") >&2
fail=1
fi

# The fill must come from the manifest, not the bucket: with an empty manifest
# the same symbol is a plain stub.
empty=$tmp/empty; : > "$empty"
nf=$(awk -v filled="$empty" -v needs="$needs" -f "$awkf" "$empty" "$needs" "$classif" | grep -c '^filled ')
if [ "$nf" -eq 0 ]; then
echo "classify: no symbol is filled without a manifest"
else
echo "classify: FAIL -- a symbol reported filled with no manifest" >&2
fail=1
fi

[ "$fail" = 0 ] && echo "classify: ok"
exit $fail
