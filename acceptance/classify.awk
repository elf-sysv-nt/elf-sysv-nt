# Report each undefined libc symbol of a built ELF as one of forward, shim,
# stub, filled, or unclassified. Reads three tab-separated inputs, in order:
#
#   filled   the wiring layer's filled-stub manifest -- symbol in column 1,
#            comment lines led by '#'. A bucket-4 stub named here has a
#            synthesized, certified body behind it (DR-0052), not nothing.
#   needs    the binary's undefined libc symbols, one bare name per line.
#   classif  veneer/classification/classification.tsv -- symbol in column 2,
#            the resolution bucket in column 4 (1/2 forward, 3 shim, 4 stub,
#            scaffold for a version node).
#
# A stub the veneer fills is not a stub that fails, so a bucket-4 symbol in the
# filled manifest reports as filled; every other bucket-4 symbol reports as
# stub. Output is one "kind symbol" line per needed symbol, unsorted.
BEGIN { FS = "\t" }
FILENAME == filled { if ($1 !~ /^#/ && $1 != "") fill[$1] = 1; next }
FILENAME == needs  { if ($1 != "")               want[$1] = 1; next }
($2 in want) && !($2 in disp) { disp[$2] = $4 }
END {
for (s in want) {
b = (s in disp) ? disp[s] : "none"
if      (b == "1" || b == "2") k = "forward"
else if (b == "3")             k = "shim"
else if (b == "4")             k = (s in fill) ? "filled" : "stub"
else                           k = "unclassified"
print k, s
}
}
