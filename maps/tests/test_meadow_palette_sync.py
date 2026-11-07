#!/usr/bin/env python3
"""Validates that MEADOW_PALETTE constants stay identical between C and GLSL."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HEADER = ROOT / "maps/toolkit/meadow_palette.h"
SHADER = ROOT / "maps/toolkit/shaders/meadow_palette.glsl"

def parse_header():
    content = HEADER.read_text(encoding="utf-8")
    matches = re.findall(r"#define\s+(MEADOW_PALETTE_\w+)\s+([0-9.f,\s]+)", content)
    res = {}
    for k, v in matches:
        nums = [float(x.replace("f", "").strip()) for x in v.split(",") if x.strip()]
        res[k] = nums
    return res

def parse_shader():
    content = SHADER.read_text(encoding="utf-8")
    matches_vec = re.findall(r"const\s+vec3\s+(MEADOW_PALETTE_\w+)\s*=\s*vec3\(([^)]+)\);", content)
    matches_float = re.findall(r"const\s+float\s+(MEADOW_PALETTE_\w+)\s*=\s*([0-9.]+);", content)
    res = {}
    for k, v in matches_vec:
        nums = [float(x.strip()) for x in v.split(",") if x.strip()]
        res[k] = nums
    for k, v in matches_float:
        res[k] = [float(v.strip())]
    return res

def main():
    h = parse_header()
    s = parse_shader()
    mismatch = False
    for k, v in h.items():
        if k not in s:
            print(f"Missing in shader: {k}", file=sys.stderr)
            mismatch = True
            continue
        if len(v) != len(s[k]) or any(abs(a - b) > 1e-4 for a, b in zip(v, s[k])):
            print(f"Mismatch for {k}: C={v} vs GLSL={s[k]}", file=sys.stderr)
            mismatch = True
    if mismatch:
        sys.exit(1)
    print("MEADOW_PALETTE sync: PASSED (C and GLSL are identical)")

if __name__ == "__main__":
    main()
