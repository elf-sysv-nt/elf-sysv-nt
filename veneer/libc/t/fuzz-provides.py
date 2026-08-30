#!/usr/bin/env python3
"""Mutate a real libc.so.6 and check provides.py refuses rather than crashes.

WP-53. provides.py reads section headers, a dynamic array, a string table and
a verdef chain out of a file it did not write. Every offset in that walk comes
from the file, so every one of them can point anywhere, and the failure mode
that matters is not a wrong answer but an unhandled exception or a walk that
does not terminate.

Two families of input are tried. Truncations cut the file at each of a spread
of lengths. Byte flips overwrite a single byte with each of several values, at
offsets drawn from the header, the section header table, and the verdef and
dynamic sections, which is where the fields a walk trusts actually live.

The bar: every run either prints a provides set or exits with an ElfError. A
traceback, a hang, or a MemoryError is a failure. A mutant that still parses is
not a failure; a mutated file can be a valid file.
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import provides as P  # noqa: E402


def attempt(buf):
    """(outcome, detail). Outcome is 'parsed', 'refused', or 'crashed'."""
    try:
        elf = P.Elf64(buf)
        P.provides(elf)
        return "parsed", ""
    except P.ElfError as exc:
        return "refused", str(exc)
    except RecursionError as exc:
        return "crashed", "RecursionError: %s" % exc
    except MemoryError:
        return "crashed", "MemoryError"
    except Exception as exc:  # noqa: BLE001 - the point is to catch everything
        return "crashed", "%s: %s" % (type(exc).__name__, exc)


def interesting_offsets(buf, rng, count):
    """Offsets in the structures a walk trusts, plus a scatter of others."""
    offsets = set(range(0, 64))  # the ELF header
    try:
        elf = P.Elf64(buf)
    except P.ElfError:
        elf = None
    if elf is not None:
        for sec in elf.sections:
            if sec["type"] in (P.SHT_DYNAMIC, P.SHT_GNU_VERDEF):
                lo = sec["offset"]
                hi = min(len(buf), lo + min(sec["size"], 4096))
                offsets.update(range(lo, hi))
        e_shoff = P.u("<Q", buf, 0x28)[0]
        offsets.update(range(e_shoff, min(len(buf), e_shoff + 64 * 8)))
    offsets = sorted(o for o in offsets if o < len(buf))
    if len(offsets) > count:
        offsets = rng.sample(offsets, count)
    return offsets


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: fuzz-provides.py <libc.so.6>")
    with open(sys.argv[1], "rb") as fh:
        original = fh.read()
    outcome, detail = attempt(original)
    if outcome != "parsed":
        sys.exit("the unmutated library does not parse: %s" % detail)

    rng = random.Random(20260830)
    runs = {"parsed": 0, "refused": 0}
    crashes = []

    for cut in range(0, len(original), max(1, len(original) // 400)):
        outcome, detail = attempt(original[:cut])
        if outcome == "crashed":
            crashes.append(("truncate to %d" % cut, detail))
        else:
            runs[outcome] += 1

    for off in interesting_offsets(original, rng, 600):
        for value in (0x00, 0x01, 0x7F, 0x80, 0xFF):
            mutant = bytearray(original)
            if mutant[off] == value:
                continue
            mutant[off] = value
            outcome, detail = attempt(bytes(mutant))
            if outcome == "crashed":
                crashes.append(("byte %d := 0x%02x" % (off, value), detail))
            else:
                runs[outcome] += 1

    total = runs["parsed"] + runs["refused"] + len(crashes)
    print("fuzz-provides: %d mutants: %d parsed, %d refused, %d crashed"
          % (total, runs["parsed"], runs["refused"], len(crashes)))
    if crashes:
        for what, detail in crashes[:20]:
            print("  %s -> %s" % (what, detail))
        sys.exit(1)


if __name__ == "__main__":
    main()
