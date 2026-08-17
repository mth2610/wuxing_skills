#!/usr/bin/env python3
"""Fail the build when a shader redefines a function its own #include already exports.

GLSL has no scoping for this: two bodies for one name is a compile error. What makes it
worth a build-time gate rather than a code review note is the FAILURE MODE — raylib
answers a failed compile with the DEFAULT shader and a perfectly valid non-zero id, so
the effect does not disappear or throw, it silently renders as something else. Three
shaders (black_hole_swirl, ground_aura, plasma_shell) shipped in that state after the
2026-08-16 shared-include migration; the same collision had been found and fixed in
aura_shell.fs on the same day, and nothing caught that the other three still had it.

It also poisons measurements taken through the game: a VFX sweep ranked BLACK HOLE the
single largest in-band effect at 7.35%, which was the fallback shader's output, not the
effect's.

Fix by renaming the LOCAL function with a file-specific prefix (`bh_fbm3`, `ga_fbm3`),
not by deleting it — the local versions are usually tuned differently from the shared one
(different octave counts), so deleting silently changes the look instead.

Run standalone:  python3 scripts/validate_shader_includes.py
"""
import glob
import os
import re
import sys

DECL = r'^[ \t]*(?:float|vec2|vec3|vec4|int|bool|void|mat2|mat3|mat4)[ \t]+(\w+)[ \t]*\('
SHARED_DIRS = ("core/shaders/common", "core/uv/shaders", "core/lightning/shaders")
SHADER_GLOBS = ("core/**/*.fs", "core/**/*.vs", "skills/**/*.fs", "skills/**/*.vs")


def exported_symbols():
    """name -> set of functions each shared include defines."""
    out = {}
    for d in SHARED_DIRS:
        for path in glob.glob(os.path.join(d, "*.glsl")):
            out[os.path.basename(path)] = set(
                re.findall(DECL, open(path, encoding="utf-8").read(), re.M))
    return out


def main():
    shared = exported_symbols()
    if not shared:
        print("FAIL: no shared .glsl includes found — is the working directory the repo root?")
        return 1

    failures = 0
    checked = 0
    for pattern in SHADER_GLOBS:
        for path in glob.glob(pattern, recursive=True):
            src = open(path, encoding="utf-8").read()
            includes = re.findall(r'#include\s+"([^"]+)"', src)
            if not includes:
                continue
            checked += 1
            # Strip the #include lines so an include's own name cannot be read as a decl.
            body = re.sub(r'#include\s+"[^"]+"', "", src)
            # A definition, not a prototype: require the opening brace.
            local = set(re.findall(DECL + r'[^;{]*\)\s*\{', body, re.M | re.S))
            available = set()
            for inc in includes:
                available |= shared.get(os.path.basename(inc), set())
            clash = sorted(local & available)
            if clash:
                failures += 1
                print(f"FAIL {path}: redefines {', '.join(clash)} — already provided by its "
                      f"#include. Rename the local copy with a file prefix; do not delete it "
                      f"(the local field is usually tuned differently).")

    if failures:
        print(f"\n{failures} shader(s) would compile to the DEFAULT shader and render wrong.")
        return 1
    print(f"shader include validation: OK ({checked} shaders with includes checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
