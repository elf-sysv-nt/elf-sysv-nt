#!/usr/bin/env python3
"""Insert one decision-record row after the last existing row in
doc/decisions/index.md. One-shot registration helper for the build worker;
hand-edits and awk one-liners have corrupted this file before."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INDEX = os.path.join(ROOT, "doc", "decisions", "index.md")


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: register-dr.py '<row>'")
    row = sys.argv[1].rstrip("\n")
    if not re.match(r"^\| \[00\d\d\]\(00", row):
        sys.exit("row does not look like a DR index row: %r" % row)
    with open(INDEX, encoding="utf-8") as fh:
        lines = fh.readlines()
    last = None
    for i, line in enumerate(lines):
        if re.match(r"^\| \[00\d\d\]\(", line):
            last = i
    if last is None:
        sys.exit("no existing DR rows in %s" % INDEX)
    if any(row.split("]")[0] in line for line in lines):
        sys.exit("row already registered")
    lines.insert(last + 1, row + "\n")
    with open(INDEX, "w", encoding="utf-8", newline="\n") as fh:
        fh.writelines(lines)
    print("registered after line %d" % (last + 1))


if __name__ == "__main__":
    main()
