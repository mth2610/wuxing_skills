#!/usr/bin/env python3
"""Mechanical rule checker for Wuxing skill files.

Usage:
    python3 scripts/lint_skill.py <skill_dir>     # lint one skill directory
    python3 scripts/lint_skill.py --all           # lint all skills under skills/

Reports: file:line: [FAIL|WARN] ruleName: message
Exits non-zero if any FAIL was found.
"""

import argparse
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)

FAIL = "FAIL"
WARN = "WARN"


class Finding:
    def __init__(self, path, lineno, level, rule, msg):
        self.path = path
        self.lineno = lineno
        self.level = level
        self.rule = rule
        self.msg = msg

    def __str__(self):
        rel = os.path.relpath(self.path, REPO_ROOT)
        return f"{rel}:{self.lineno}: [{self.level}] {self.rule}: {self.msg}"


def read_lines(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.readlines()
    except OSError:
        return []


# ── Rule 1: no malloc/calloc/realloc/free ─────────────────────────────────────
def check_no_malloc(path, lines):
    findings = []
    pat = re.compile(r'\b(malloc|calloc|realloc|free)\s*\(')
    for i, line in enumerate(lines, 1):
        if pat.search(line) and "// lint: allow-malloc" not in line:
            findings.append(Finding(path, i, FAIL, "no-malloc",
                f"forbidden heap call: {pat.search(line).group(0).strip('(')}()"))
    return findings


# ── Rule 2: no raw raylib draw primitives ─────────────────────────────────────
RAYLIB_DRAW_PAT = re.compile(r'\bDraw(Cylinder|Sphere|Cube|Capsule|Plane)\w*\s*\(')
DRAW_CORE_PAT = re.compile(r'\bDraw(Core|Effect|Mesh|Procedural|Trail|Decal|Ribbon|Sprite|Afterimage)')


def check_no_raylib_prims(path, lines):
    findings = []
    for i, line in enumerate(lines, 1):
        m = RAYLIB_DRAW_PAT.search(line)
        if m and not DRAW_CORE_PAT.search(line) and "// lint: allow-primitive" not in line:
            findings.append(Finding(path, i, FAIL, "no-raylib-primitive",
                f"{m.group(0).rstrip('(')}: use DrawEffectMesh / DrawCoreCylinder from procedural_mesh_utils.h"))
    return findings


# ── Rule 3: no raw Color{} literals (warn, allow opt-out) ────────────────────
COLOR_LIT_PAT = re.compile(r'\(Color\)\s*\{')


def check_no_raw_colors(path, lines):
    findings = []
    for i, line in enumerate(lines, 1):
        if COLOR_LIT_PAT.search(line):
            if "ELEMENT_COLOR_" in line or "// lint: allow-color" in line:
                continue
            findings.append(Finding(path, i, WARN, "element-color",
                "(Color){} literal — prefer ELEMENT_COLOR_* or add // lint: allow-color"))
    return findings


# ── Rule 4: PI guard ──────────────────────────────────────────────────────────
PI_DEFINE_PAT = re.compile(r'^\s*#\s*define\s+PI\s')
IFNDEF_PI_PAT = re.compile(r'#\s*ifndef\s+PI')


def check_pi_guard(path, lines):
    findings = []
    for i, line in enumerate(lines, 1):
        if PI_DEFINE_PAT.match(line):
            prev = lines[i - 2].strip() if i >= 2 else ""
            if not IFNDEF_PI_PAT.search(prev):
                findings.append(Finding(path, i, FAIL, "pi-guard",
                    "#define PI without preceding #ifndef PI guard"))
    return findings


# ── Rule 5: no UnloadShader/UnloadTexture inside a skill file ─────────────────
UNLOAD_PAT = re.compile(r'\bUnload(Shader|Texture)\s*\(')
SKILL_UNLOAD_FN_PAT = re.compile(r'\bUnload[A-Z]\w*Skill\b')


def check_no_unload(path, lines):
    findings = []
    full_text = "".join(lines)
    if not SKILL_UNLOAD_FN_PAT.search(full_text):
        return []
    for i, line in enumerate(lines, 1):
        if UNLOAD_PAT.search(line) and "// lint: allow-unload" not in line:
            findings.append(Finding(path, i, FAIL, "no-unload",
                "UnloadShader/UnloadTexture in skill file — ResourceManager owns these"))
    return findings


# ── Rule 6: SpawnTrail without KillTrail ─────────────────────────────────────
SPAWN_TRAIL_PAT = re.compile(r'\b(SpawnProjectileTrail|SpawnLightningFollowerTrail)\s*\(')
KILL_TRAIL_PAT = re.compile(r'\bKillTrail\s*\(')


def check_kill_trail(path, lines):
    full_text = "".join(lines)
    if not SPAWN_TRAIL_PAT.search(full_text):
        return []
    if not KILL_TRAIL_PAT.search(full_text):
        return [Finding(path, 1, FAIL, "kill-trail",
            "SpawnProjectileTrail/SpawnLightningFollowerTrail called but KillTrail not found in same file")]
    return []


# ── Rule 7: rlDisableDepthMask / rlEnableDepthMask balance ───────────────────
def check_depth_balance(path, lines):
    findings = []
    full_text = "".join(lines)
    disable_mask = len(re.findall(r'\brlDisableDepthMask\s*\(', full_text))
    enable_mask = len(re.findall(r'\brlEnableDepthMask\s*\(', full_text))
    disable_test = len(re.findall(r'\brlDisableDepthTest\s*\(', full_text))
    enable_test = len(re.findall(r'\brlEnableDepthTest\s*\(', full_text))
    if disable_mask != enable_mask:
        findings.append(Finding(path, 1, WARN, "depth-balance",
            f"rlDisableDepthMask({disable_mask}) != rlEnableDepthMask({enable_mask}) — unbalanced depth state"))
    if disable_test != enable_test:
        findings.append(Finding(path, 1, WARN, "depth-balance",
            f"rlDisableDepthTest({disable_test}) != rlEnableDepthTest({enable_test}) — unbalanced depth state"))
    return findings


# ── Rule 8: shader file checks ────────────────────────────────────────────────
VERSION_PAT = re.compile(r'^\s*#\s*version\s+\d+')
INCLUDE_PAT = re.compile(r'^\s*#\s*include\s+')
TEXTURE2D_PAT = re.compile(r'\btexture2D\s*\(')


def check_shader(path, lines):
    findings = []
    has_version = any(VERSION_PAT.match(l) for l in lines)
    has_include = any(INCLUDE_PAT.match(l) for l in lines)
    if has_version and has_include:
        findings.append(Finding(path, 1, WARN, "shader-version",
            "#version and #include both present — on Android/GLES the preprocessor injects the version; remove explicit #version if targeting GLES"))
    for i, line in enumerate(lines, 1):
        if TEXTURE2D_PAT.search(line) and "// lint: allow-gles1" not in line:
            findings.append(Finding(path, i, FAIL, "shader-texture2D",
                "texture2D() is GLES1 — use texture() instead"))
    return findings


def lint_file(path):
    lines = read_lines(path)
    if not lines:
        return []

    ext = os.path.splitext(path)[1].lower()
    findings = []

    if ext in (".c", ".h"):
        findings += check_no_malloc(path, lines)
        findings += check_no_raylib_prims(path, lines)
        findings += check_no_raw_colors(path, lines)
        findings += check_pi_guard(path, lines)
        findings += check_no_unload(path, lines)
        findings += check_kill_trail(path, lines)
        findings += check_depth_balance(path, lines)
    elif ext in (".vs", ".fs", ".glsl"):
        findings += check_shader(path, lines)

    return findings


def lint_dir(skill_dir):
    findings = []
    for root, dirs, files in os.walk(skill_dir):
        dirs[:] = [d for d in dirs if d not in ("build", "_deps", "android.wuxing_skills")]
        for fname in sorted(files):
            ext = os.path.splitext(fname)[1].lower()
            if ext in (".c", ".h", ".vs", ".fs", ".glsl"):
                findings += lint_file(os.path.join(root, fname))
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", nargs="?",
                        help="Skill directory to lint (or omit with --all)")
    parser.add_argument("--all", action="store_true",
                        help="Lint all skills under skills/")
    args = parser.parse_args()

    if not args.target and not args.all:
        parser.error("Provide a skill dir or --all")

    dirs_to_lint = []
    if args.all:
        skills_root = os.path.join(REPO_ROOT, "skills")
        for element in sorted(os.listdir(skills_root)):
            elem_dir = os.path.join(skills_root, element)
            if not os.path.isdir(elem_dir):
                continue
            for skill in sorted(os.listdir(elem_dir)):
                skill_dir = os.path.join(elem_dir, skill)
                if os.path.isdir(skill_dir):
                    dirs_to_lint.append(skill_dir)
    else:
        d = args.target
        if not os.path.isdir(d):
            d = os.path.join(REPO_ROOT, d)
        if not os.path.isdir(d):
            print(f"ERROR: '{args.target}' is not a directory", file=sys.stderr)
            return 1
        dirs_to_lint.append(d)

    all_findings = []
    for d in dirs_to_lint:
        all_findings += lint_dir(d)

    fail_count = 0
    warn_count = 0
    for f in all_findings:
        print(f)
        if f.level == FAIL:
            fail_count += 1
        else:
            warn_count += 1

    if all_findings:
        print(f"\n{fail_count} FAIL(s), {warn_count} WARN(s)")
    else:
        print("All checks passed.")

    return 1 if fail_count > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
