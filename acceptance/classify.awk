# Report each undefined libc symbol of a built ELF as one of forward, wired,
# shim, filled, stub, or unclassified. Reads four tab-separated inputs, in
# order:
#
#   filled   the wiring layer's filled-stub manifest -- symbol in column 1,
#            comment lines led by '#'. A bucket-4 stub named here has a
#            synthesized, certified body behind it (DR-0052), not nothing.
#   wired    the certified-shim manifest -- symbol in column 1, the union of
#            the wire-<slice>.shims.tsv symbols over the slices that carry a
#            live-<slice>.sh crossing. A bucket-3 shim named here has a
#            written translation the live crossing certified, not an unwritten
#            promise.
#   needs    the binary's undefined libc symbols, one bare name per line.
#   classif  veneer/classification/classification.tsv -- symbol in column 2,
#            the resolution bucket in column 4 (1/2 forward, 3 shim, 4 stub,
#            scaffold for a version node).
#
# A stub the veneer fills is not a stub that fails, so a bucket-4 symbol in the
# filled manifest reports as filled and every other bucket-4 symbol as stub.
# The same distinction runs one bucket up: a bucket-3 shim the wiring layer has
# written and the live crossing certified reports as wired, and only a bucket-3
# symbol no crossed slice has written reports as shim. Output is one
# "kind symbol" line per needed symbol, unsorted.
BEGIN { FS = "\t" }
FILENAME == filled { if ($1 !~ /^#/ && $1 != "") fill[$1] = 1; next }
FILENAME == wired  { if ($1 !~ /^#/ && $1 != "") done[$1] = 1; next }
FILENAME == needs  { if ($1 != "")               want[$1] = 1; next }
($2 in want) && !($2 in disp) { disp[$2] = $4 }
END {
for (s in want) {
b = (s in disp) ? disp[s] : "none"
if      (b == "1" || b == "2") k = "forward"
else if (b == "3")             k = (s in done) ? "wired" : "shim"
else if (b == "4")             k = (s in fill) ? "filled" : "stub"
else                           k = "unclassified"
print k, s
}
}
