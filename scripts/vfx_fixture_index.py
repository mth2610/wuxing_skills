#!/usr/bin/env python3
"""Resolve a NEWFX fixture name to its --render-vfx index (or list them all).

NEWFX indices are POSITIONAL: pruning one entry from `s_newFxNames[]` renumbers every
entry after it. Any doc, script or comment citing `--render-vfx 38` therefore starts
pointing at a different effect the next time the manifest is pruned, silently. Names do
not move, so tooling and documentation should say the name and resolve it here.

    python3 scripts/vfx_fixture_index.py "FLAME VOLUME"   -> prints the index
    python3 scripts/vfx_fixture_index.py --list           -> prints "index<TAB>name"
"""
import re
import sys

SRC = "sandbox/vfx_test.c"


def names():
    src = open(SRC, encoding="utf-8").read()
    m = re.search(r"gen:newfx_names begin.*?\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit(f"could not find the newfx_names block in {SRC}")
    return re.findall(r'"([^"]+)"', m.group(1))


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    table = names()
    if sys.argv[1] in ("--list", "-l"):
        for i, n in enumerate(table):
            print(f"{i}\t{n}")
        return 0
    want = sys.argv[1].strip().upper()
    if want not in table:
        print(f"unknown fixture '{sys.argv[1]}'", file=sys.stderr)
        print("known: " + ", ".join(table), file=sys.stderr)
        return 1
    print(table.index(want))
    return 0


if __name__ == "__main__":
    sys.exit(main())
