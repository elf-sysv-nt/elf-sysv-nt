#!/bin/sh
# Network-free tests for cut-slices.py: map over fixture headers with
# the native compiler, order over a synthetic ranking.
set -e
cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 ../cut-slices.py map --slices fixture-slices.tsv \
    --include fixtures --cc "${CC:-gcc}" -o "$tmp/map.tsv"
awk -F'\t' '$1=="frob_alloc"{print $2}' "$tmp/map.tsv" | grep -qx memory
awk -F'\t' '$1=="frob_open"{print $2}' "$tmp/map.tsv" | grep -qx files
# frob_shared is declared by both headers; the earlier row wins
awk -F'\t' '$1=="frob_shared"{print $2}' "$tmp/map.tsv" | grep -qx memory

python3 ../cut-slices.py order --map "$tmp/map.tsv" \
    --ranking fixture-ranking.tsv --out "$tmp/out"
# files leads (demand 10), then memory (5), then unassigned (2);
# the libother row must not appear anywhere
head -1 "$tmp/out/slice-order.tsv" | grep -q '^files'
awk -F'\t' '$1=="memory"{print $3}' "$tmp/out/slice-order.tsv" | grep -qx 5
grep -q unknown_sym "$tmp/out/slice-unassigned.tsv"
! grep -rq nope "$tmp/out"

echo ok
