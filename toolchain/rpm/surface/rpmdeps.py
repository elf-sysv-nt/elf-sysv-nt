#!/usr/bin/env python3
"""Emit rpm dependencies off an ELF64 file in el8 elfdeps' exact shape.

WP-62. The done-when is that a built package's Provides and Requires carry
`libc.so.6(GLIBC_2.2.5)(64bit)` in the vendor's exact shape, and the vendor's
shape is more than a format string: it is which lines come out, in which
order, read off which structure. This emitter reproduces `tools/elfdeps.c` as
`rpm-4.14.3-32.el8_10` ships it, line for line, so that `t/run-tests.sh` can
diff it against the real generator running over the same files.

The shape, measured by spike 4 and read off the vendor source:

  --provides   One line per .gnu.version_d aux NAME on every non-base entry,
               in file order -- a node with a parent contributes two lines,
               which is why el8's libc offers 58 lines off 29 nodes. The name
               left of the parenthesis is the BASE verdef node, not
               DT_SONAME. `<soname>()(64bit)` comes last, from DT_SONAME,
               because the dynamic section follows .gnu.version_d in the file.

  --requires   One line per .gnu.version_r aux entry in file order, then
               `<name>()(64bit)` per DT_NEEDED in dynamic order, then
               `rtld(GNU_HASH)` when the file carries DT_GNU_HASH without
               DT_HASH. Requires come out only for a file with an execute
               bit: elfdeps tests st_mode, not the ELF type.

Both spellings pass through elfdeps' soname filter: a name is kept only if it
contains `.so` and begins `lib`, `ld.`, `ld-` or `ld64.`; anything else is
dropped without a diagnostic. That silence is load-bearing for this project --
a face named like a PE DLL would vanish from every generated dependency -- so
the filter is reproduced rather than skipped.

Nothing here parses ELF. The readers are WP-53's provides.py and WP-54's
elfneeds.py, certified against the format and fuzzed; this file only orders
their answers the way elfdeps orders its own.

Exit: 0 on success (an empty answer is a success, as it is for elfdeps),
2 on usage, 1 on a file the readers refuse.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
sys.path.insert(0, os.path.join(ROOT, "veneer", "libc"))
sys.path.insert(0, os.path.join(ROOT, "veneer", "companions"))
import provides as P     # noqa: E402
import elfneeds as N     # noqa: E402

DT_HASH = 4
DT_GNU_HASH = 0x6FFFFEF5
MARKER = "(64bit)"


def keep_soname(name):
    """elfdeps' skipSoname, inverted: True when the name survives.

    The string must contain `.so`, and must begin `lib` or one of the
    dynamic-linker spellings `ld.`, `ld-`, `ld64.`.
    """
    if ".so" not in name:
        return False
    if name.startswith("lib"):
        return True
    return name.startswith(("ld.", "ld-", "ld64."))


def rpm_provides(elf):
    """elfdeps --provides, one string per line, in its order."""
    lines = []
    entries = elf.verdef()
    if entries:
        base = [n for n, f, _p in entries if f & P.VER_FLG_BASE]
        if len(base) != 1:
            raise P.ElfError("%d base verdef nodes, want exactly 1"
                             % len(base))
        if keep_soname(base[0]):
            for name, flags, parents in entries:
                if flags & P.VER_FLG_BASE:
                    continue
                for aux in [name] + parents:
                    lines.append("%s(%s)%s" % (base[0], aux, MARKER))
    try:
        soname = elf.soname()
    except P.ElfError:
        return lines
    if keep_soname(soname):
        lines.append("%s()%s" % (soname, MARKER))
    return lines


def rpm_requires(elf, is_exec):
    """elfdeps --requires, one string per line, in its order."""
    if not is_exec:
        return []
    lines = []
    for lib, nodes in N.verneed(elf):
        if not keep_soname(lib):
            continue
        for node in nodes:
            lines.append("%s(%s)%s" % (lib, node, MARKER))
    for name in N.needed(elf):
        if keep_soname(name):
            lines.append("%s()%s" % (name, MARKER))
    tags = {tag for tag, _v in elf.dynamic()}
    if DT_GNU_HASH in tags and DT_HASH not in tags:
        lines.append("rtld(GNU_HASH)")
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("object")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--provides", action="store_true")
    mode.add_argument("--requires", action="store_true")
    ap.add_argument("--assume-executable", action="store_true",
                    help="treat the file as 0755 whatever the filesystem "
                         "says; elfdeps reads st_mode, and this platform's "
                         "modes are the build tree's, not a payload's")
    args = ap.parse_args()
    with open(args.object, "rb") as fh:
        buf = fh.read()
    try:
        elf = P.Elf64(buf)
        if args.provides:
            lines = rpm_provides(elf)
        else:
            x = args.assume_executable or os.access(args.object, os.X_OK)
            lines = rpm_requires(elf, x)
    except P.ElfError as exc:
        sys.exit("%s: %s" % (args.object, exc))
    for line in lines:
        print(line)


if __name__ == "__main__":
    main()
