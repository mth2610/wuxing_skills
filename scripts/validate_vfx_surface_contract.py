#!/usr/bin/env python3
"""Fail the build when a VFX draw binds a blend surface its shader's resolver contradicts.

`core/shaders/common/vfx_composite.glsl` states the contract in its own header: the resolver
a shader returns through determines the blend state it must be drawn with —

    VFX_ResolveBody          -> VFX_SURFACE_ALPHA
    VFX_ResolvePremultiplied -> VFX_SURFACE_PREMULTIPLIED
    VFX_ResolveEmission      -> VFX_SURFACE_ADDITIVE

Break it and nothing errors. Bound ADDITIVE, a `ResolveBody` output has its alpha consumed as
a brightness multiplier and its COVERAGE TERM SILENTLY DISCARDED, so the effect can never
attenuate anything behind it. It looks fine on the night arena — additive light over black is
exactly what you wanted — and it is invisible in daylight, which is the entire failure this
project has been chasing.

Measured on ENERGY ORB, which had this bug: 0.07% body area against a white background versus
10.04% against a dark one. After binding the surface its shader actually asks for, plus the
authoring fixes that exposed, 9.89%. See BRIGHT_BACKGROUND_VFX_SPEC.md §11b.

Shaders that use more than one resolver are dual-pass: they branch on a uniform and the correct
surface depends on which pass the caller wants, so they cannot be decided statically. They are
reported under `unchecked` rather than guessed at.

SCOPE: composition-layer draws, where the call site names both the surface and the shader.
The particle, trail and decal systems choose blend state from DATA (the appearance table's
surface/unlit pairing, and the decal pass structure), so their contract is dynamic and cannot
be read statically — it is covered by `core/tests/vfx_appearance_test.c` instead, which
asserts that a lit appearance is only ever drawn ALPHA and that the particle system's
premultiplied guard keys on the surface rather than on one appearance by name.

Run standalone:  python3 scripts/validate_vfx_surface_contract.py [-v]
"""
import glob
import os
import re
import sys

REQUIRED = {"Body": "VFX_SURFACE_ALPHA",
            "Premultiplied": "VFX_SURFACE_PREMULTIPLIED",
            "Emission": "VFX_SURFACE_ADDITIVE"}


def shader_resolvers():
    """basename.fs -> set of resolver names it returns through."""
    out = {}
    for f in glob.glob("core/**/*.fs", recursive=True) + glob.glob("skills/**/*.fs", recursive=True):
        r = set(re.findall(r"VFX_Resolve(Body|Premultiplied|Emission)\s*\(",
                           open(f, encoding="utf-8").read()))
        if r:
            out[os.path.basename(f)] = r
    return out


def strip_comments(src):
    """Comments are not code. An earlier hand-rolled version of this check matched a
    shader filename inside a COMMENT and reported a violation it had not actually found —
    the same mistake, twice in one session, as reading `grep` hits in prose as call
    sites."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def file_shader_vars(src):
    """`Shader` variables -> the .fs they were loaded from.

    Two reasons this cannot be scoped to one file. The common pattern is a static loaded
    once at init and bound later by name, so the draw scope never mentions the path; and
    the .inl files are textually included into ONE translation unit, so a static declared
    in `water.inl` is bound from `water_stream.inl`. Scanning per-file missed exactly that
    case and reported the draw as unresolvable."""
    out = {}
    for m in re.finditer(r"(\w+)\s*=\s*ResourceManager_LoadShader\s*\([^;]*?\"([\w/]+\.fs)\"", src, re.S):
        out[m.group(1)] = os.path.basename(m.group(2))
    for m in re.finditer(r"Material_LoadCustomShader\s*\(\s*&?(\w+)[^;]*?\"([\w/]+\.fs)\"", src, re.S):
        out[m.group(1)] = os.path.basename(m.group(2))
    return out


def material_shaders():
    """`FooMaterial` -> shader basename, read from each FooMaterial_Load's body."""
    out = {}
    for f in glob.glob("core/**/*.c", recursive=True):
        src = open(f, encoding="utf-8").read()
        src = strip_comments(src)
        for m in re.finditer(r"void\s+(\w+)Material_Load\s*\([^)]*\)\s*\{", src):
            body = src[m.end():m.end() + 1200]
            fs = re.search(r'"([\w/]+\.fs)"', body)
            if fs:
                out[m.group(1) + "Material"] = os.path.basename(fs.group(1))
    return out


def main():
    verbose = "-v" in sys.argv
    res = shader_resolvers()
    mats = material_shaders()
    if not res or not mats:
        print("FAIL: found no shaders or no materials — wrong working directory?")
        return 1

    # One TU: gather every shader variable across all composition sources at once.
    allsrc = ""
    for f in (glob.glob("core/composition/**/*.inl", recursive=True)
              + glob.glob("core/composition/**/*.c", recursive=True)):
        allsrc += strip_comments(open(f, encoding="utf-8").read()) + "\n"
    globalvars = file_shader_vars(allsrc)

    failures, unchecked, blind, checked, sites = [], [], [], 0, 0
    for f in sorted(glob.glob("core/composition/**/*.inl", recursive=True)):
        raw = strip_comments(open(f, encoding="utf-8").read())
        localvars = dict(globalvars); localvars.update(file_shader_vars(raw))
        lines = raw.split("\n")
        for i, line in enumerate(lines):
            if "VFXRender_BeginDraw" not in line:
                continue
            # the surface argument may sit on the next line or two
            head = "\n".join(lines[i:i + 4])
            surfaces = re.findall(r"VFX_SURFACE_(\w+)", head)
            if len(set(surfaces)) != 1:
                continue                      # conditional surface — caller decides at run time
            surface = "VFX_SURFACE_" + surfaces[0]
            sites += 1
            # the scope: until EndDraw, or 60 lines, whichever comes first
            scope = "\n".join(lines[i:i + 60]).split("VFXRender_EndDraw")[0]
            shaders = set()
            for name in re.findall(r"(\w+Material)_Begin", scope):
                if name in mats:
                    shaders.add(mats[name])
            for path in re.findall(r'"([\w/]+\.fs)"', scope):
                shaders.add(os.path.basename(path))
            # a shader bound by variable name, loaded elsewhere in the same file
            for var in re.findall(r"(?:SkillManager_BeginShader|BeginShaderMode)\s*\(\s*&?(\w+)", scope):
                if var in localvars:
                    shaders.add(localvars[var])
            # the base EffectMaterial, unless this file overrode its shader
            if re.search(r"\bMaterial_Begin\s*\(", scope) and not file_shader_vars(raw):
                shaders.add("effect_material.fs")
            known = [sh for sh in sorted(shaders) if sh in res]
            if not known:
                # A draw whose shader this checker could not resolve. Reported, not
                # ignored: a validator that hides its blind spots is worse than none,
                # because it converts "unknown" into "verified".
                # No VFX_Resolve* shader at this site. The ribbon/trail primitives draw
                # immediate-mode with no shader of their own (rlBegin(RL_QUADS) in
                # core/ribbon_strip.c), so vertex colours meet the blend state directly
                # and the resolver contract simply does not apply — this is out of scope,
                # not unverified.
                blind.append(f"{f}:{i+1} ({surface})")
            for sh in known:
                r = res[sh]
                if len(r) != 1:
                    unchecked.append(f"{f}:{i+1} {sh} (dual-pass: {'+'.join(sorted(r))})")
                    continue
                checked += 1
                want = REQUIRED[next(iter(r))]
                if want != surface:
                    failures.append(
                        f"FAIL {f}:{i+1}\n"
                        f"     binds {surface} but {sh} returns VFX_Resolve{next(iter(r))},\n"
                        f"     which vfx_composite.glsl's contract requires be drawn {want}.\n"
                        f"     Bound this way the coverage term is discarded and the effect\n"
                        f"     cannot attenuate the background — invisible on bright scenery.")

    for x in failures:
        print(x)
    if verbose:
        for u in unchecked:
            print(f"  unchecked (dual-pass) {u}")
        for b in blind:
            print(f"  no-resolver (immediate-mode / vertex-colour draw) {b}")
    cov = (f"{sites} draw sites: {checked} checked, {len(unchecked)} dual-pass, "
           f"{len(blind)} with no resolver shader (run with -v to list)")
    if failures:
        print(f"\n{len(failures)} surface/resolver contract violation(s).  [{cov}]")
        return 1
    print(f"VFX surface contract: OK  [{cov}]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
