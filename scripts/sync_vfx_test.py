#!/usr/bin/env python3
"""
sync_vfx_test.py — Fully automatic VFX sync.

Scans core/composition/vc_*.inl for VFX_* function definitions, then:
  1. Auto-updates scripts/vfx_test_manifest.json  (adds new / removes deleted entries)
  2. Regenerates sandbox/vfx_test.c               (NEWFX tab)
  3. Auto-updates visual_composer.c               (new #include "vc_*.inl")
  4. Auto-updates visual_composer.h               (new function declarations)

No JSON editing needed for new simple functions.
For complex functions (pointer arrays, custom colors, etc.) add to:
  "excluded" — skip entirely
  "overrides" — provide a hand-crafted draw_call / trigger_call

Usage:
  python3 scripts/sync_vfx_test.py          # apply sync
  python3 scripts/sync_vfx_test.py --check  # dry-run: report only, no writes
"""

import argparse, json, os, re, sys

REPO_ROOT     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMP_DIR      = os.path.join(REPO_ROOT, "core/composition")
HEADER_PATH   = os.path.join(REPO_ROOT, "core/composition/visual_composer.h")
VC_C_PATH     = os.path.join(REPO_ROOT, "core/composition/visual_composer.c")
MANIFEST_PATH = os.path.join(REPO_ROOT, "scripts/vfx_test_manifest.json")
VFX_TEST_PATH = os.path.join(REPO_ROOT, "sandbox/vfx_test.c")

PLACEHOLDERS = {
    "$POS":  "s_prefabStartPos",
    "$TIME": "s_meshTime",
    "$PROG": "progress",
    "$SEED": "posSeed",
}
INDENT  = "          "   # 10 spaces — matches surrounding code in vfx_test.c
CAT_IDS = {"fire": 0, "water": 1, "wood": 2, "metal": 3, "earth": 4, "taiji": 5, "util": 6}

ELEM_SUFFIXES = ("_FIRE", "_WATER", "_WOOD", "_METAL", "_EARTH", "_TAIJI")

# ── Element / category inference ──────────────────────────────────────────────

_ELEM_KW = {
    "fire":  ["fire", "flame", "burn", "ember", "blaze", "flare", "scorch", "whirl"],
    "water": ["water", "wave", "bubble", "mist", "puddle", "ice", "splash", "stream", "frost"],
    "wood":  ["wood", "leaf", "vine", "thorn", "root", "sprout", "petal", "bloom"],
    "metal": ["metal", "thunder", "steel", "shard", "blade", "plasma", "shrapnel", "ricochet"],
    "earth": ["earth", "rock", "fissure", "stone", "soil", "crack", "quake", "boulder", "pillar"],
    "taiji": ["taiji", "yin", "yang", "cyclone", "gust", "static", "qi", "summon"],
}
_PRESET_MAP = {
    "fire":  "EFFECT_PRESET_FIRE_EXPLOSION",
    "water": "EFFECT_PRESET_WATER_SPLASH",
    "wood":  "EFFECT_PRESET_WOOD_BLOOM",
    "metal": "EFFECT_PRESET_METAL_SHARD",
    "earth": "EFFECT_PRESET_EARTH_CRACK",
    "taiji": "EFFECT_PRESET_TAIJI_BURST",
    "util":  "EFFECT_PRESET_FIRE_EXPLOSION",
}

def infer_category(fn_name):
    low = fn_name.lower()
    for cat, kws in _ELEM_KW.items():
        if any(k in low for k in kws):
            return cat
    return "util"

def infer_mat_id(fn_name):
    cat = infer_category(fn_name)
    if cat == "util":
        return "VC_MAT_FIRE"
    return f"VC_MAT_{cat.upper()}"

def camel_to_label(fn_name):
    name = fn_name
    for pfx in ("VFX_Compose", "VFX_Trigger", "VFX_Spawn", "VFX_"):
        if name.startswith(pfx):
            name = name[len(pfx):]
            break
    words = re.sub(r'([A-Z])', r' \1', name).strip().split()
    return " ".join(w.upper() for w in words)


# ── Param parsing + arg inference ─────────────────────────────────────────────

def parse_params(params_str):
    """'VC_MaterialId matId, Vector3 pos, float time' → [('VC_MaterialId','matId'), ...]"""
    params_str = re.sub(r'\s+', ' ', params_str).strip()
    if not params_str or params_str == "void":
        return []
    result = []
    for p in params_str.split(','):
        p = p.strip()
        if not p:
            continue
        tokens = p.split()
        if len(tokens) < 2:
            continue
        raw_name = tokens[-1].lstrip('*').rstrip('[]')
        raw_type = ' '.join(tokens[:-1])
        result.append((raw_type.strip(), raw_name.strip()))
    return result

def _is_complex_param(type_str, name):
    """Return True for params that can't be reasonably auto-generated."""
    return '*' in type_str or '[' in name or 'const ' in type_str and '*' in name

def infer_arg(type_str, name, fn_name):
    t, n = type_str.strip(), name.lower()

    if 'Vector3' in t and '*' not in t:
        if any(k in n for k in ('pos', 'origin', 'center', 'location', 'spawn', 'base')):
            return '$POS'
        if any(k in n for k in ('dir', 'direction', 'forward', 'normal')):
            return '(Vector3){1.0f, 0.0f, 0.0f}'
        if any(k in n for k in ('end', 'target', 'dest')):
            return 'Vector3Add($POS, (Vector3){3.0f, 0, 0})'
        return '$POS'

    if t == 'float':
        if n in ('time', 't', 'elapsed', 'elapsedtime'):
            return '$TIME'
        if n in ('progress', 'prog', 'phase', 'normalized'):
            # Clamp to avoid >= 1.0 guards that halt rendering
            return 'fminf($PROG, 0.99f)'
        if any(k in n for k in ('radius', 'size', 'scale')):
            return '1.5f'
        if any(k in n for k in ('width', 'thick')):
            return '0.1f'
        if 'height' in n:
            return '2.0f'
        if any(k in n for k in ('speed', 'velocity')):
            return '2.0f'
        if any(k in n for k in ('angle', 'rot', 'deg', 'arc')):
            return '90.0f'
        if any(k in n for k in ('strength', 'intensity', 'power', 'force', 'amp')):
            return '1.0f'
        if any(k in n for k in ('duration', 'life', 'lifetime', 'ttl', 'delay')):
            return '2.0f'
        if n in ('dt', 'delta'):
            return 'GetFrameTime()'
        return '1.0f'

    if t == 'int':
        if 'seed' in n:
            return '$SEED'
        if any(k in n for k in ('count', 'num')):
            return '5'
        return '0'

    if t == 'bool':
        return 'false'
    if t == 'Color':
        return 'WHITE'
    if 'VC_MaterialId' in t:
        return infer_mat_id(fn_name)
    if 'EffectPresetType' in t:
        return _PRESET_MAP.get(infer_category(fn_name), "EFFECT_PRESET_FIRE_EXPLOSION")
    if 'GroundPatternStyle' in t:
        return 'GROUND_CRACK_RADIAL'
    if 'PathStyle' in t:
        return 'PATH_STONE_PILLAR'

    # Pointer / array → can't auto-gen
    return None

def infer_entry(fn_name, params_str):
    """
    Auto-generate a manifest entry dict from a function signature.
    Returns (entry_dict, is_complex) where is_complex=True means some params
    couldn't be inferred and the call may need a manual override.
    """
    params = parse_params(params_str)

    has_time     = any(n.lower() in ('time', 't', 'elapsed') for _, n in params)
    has_progress = any(n.lower() in ('progress', 'prog', 'phase') for _, n in params)
    is_continuous = has_time or has_progress

    args, is_complex = [], False
    for type_str, name in params:
        arg = infer_arg(type_str, name, fn_name)
        if arg is None:
            args.append(f"/* TODO:{type_str} */")
            is_complex = True
        else:
            args.append(arg)

    call = f"{fn_name}({', '.join(args)})"

    entry = {
        "fn":       fn_name,
        "label":    camel_to_label(fn_name),
        "category": infer_category(fn_name),
        "_auto":    True,
    }
    if is_continuous:
        entry["type"]      = "continuous"
        entry["draw_call"] = call
    else:
        entry["type"]         = "oneshot"
        entry["trigger_call"] = call

    return entry, is_complex


# ── .inl scanner ──────────────────────────────────────────────────────────────

def scan_inl_functions(comp_dir):
    """
    Scan all .inl files recursively for VFX_* function definitions.
    Returns dict: fn_name → (params_str, return_type, inl_filename)
    First definition wins (in alphabetical relative-path order).
    """
    sig_re = re.compile(
        r'\b(void|int)\s+(VFX_\w+)\s*\(([^)]*)\)\s*\{',
        re.DOTALL,
    )
    result = {}
    
    all_files = []
    for root, dirs, files in os.walk(comp_dir):
        for f in files:
            if f.endswith('.inl'):
                rel_path = os.path.relpath(os.path.join(root, f), comp_dir)
                all_files.append(rel_path)
                
    for rel_path in sorted(all_files):
        with open(os.path.join(comp_dir, rel_path), errors='ignore') as f:
            text = f.read()
        for m in sig_re.finditer(text):
            fn = m.group(2)
            if fn not in result:
                result[fn] = (
                    re.sub(r'\s+', ' ', m.group(3)).strip(),  # params
                    m.group(1),                                # return type
                    rel_path,                                  # source file
                )
    return result


# ── visual_composer.c / .h helpers ───────────────────────────────────────────

def replace_section(content, key, new_body):
    begin = f"// @gen:{key} begin"
    end   = f"// @gen:{key} end"
    pat   = re.compile(re.escape(begin) + r".*?" + re.escape(end), re.DOTALL)
    if not pat.search(content):
        return content, False
    return pat.sub(new_body, content), True

def gen_vc_includes_block(new_inl_files):
    lines = ["// @gen:vc_includes begin"]
    lines += [f'#include "{f}"' for f in sorted(new_inl_files)]
    lines.append("// @gen:vc_includes end")
    return "\n".join(lines)

def gen_vc_declarations_block(decl_lines):
    lines = ["// @gen:vc_declarations begin"]
    lines += decl_lines
    lines.append("// @gen:vc_declarations end")
    return "\n".join(lines)

def update_vc_c(comp_dir, dry_run=False):
    """
    Sync the @gen:vc_includes section in visual_composer.c.
    Adds includes for new vc_*.inl files; removes includes for deleted ones.
    Manual (non-@gen) includes are never touched.
    """
    all_inl = sorted(f for f in os.listdir(comp_dir)
                     if f.startswith('vc_') and f.endswith('.inl'))
    with open(VC_C_PATH) as f:
        src = f.read()

    # Files already covered by hand-written includes (outside @gen block)
    gen_pat = re.compile(
        r'// @gen:vc_includes begin.*?// @gen:vc_includes end', re.DOTALL)
    src_without_gen = gen_pat.sub('', src)
    manual_inl = set(re.findall(r'#include\s+"(vc_[^"]+\.inl)"', src_without_gen))

    # New files: exist on disk but not in any manual include
    gen_inl = sorted(f for f in all_inl if f not in manual_inl)

    # Current @gen block contents
    m = gen_pat.search(src)
    cur_gen_inl = set()
    if m:
        cur_gen_inl = set(re.findall(r'#include\s+"(vc_[^"]+\.inl)"', m.group(0)))

    added   = [f for f in gen_inl if f not in cur_gen_inl]
    removed = [f for f in cur_gen_inl if f not in set(gen_inl)]

    if not added and not removed:
        return False

    if added:
        print(f"[sync_vfx_test] visual_composer.c: +{len(added)} include(s): {', '.join(added)}")
    if removed:
        print(f"[sync_vfx_test] visual_composer.c: -{len(removed)} include(s): {', '.join(removed)}")
    if dry_run:
        return True

    new_block = gen_vc_includes_block(gen_inl)
    result, found = replace_section(src, "vc_includes", new_block)
    if not found:
        # First time: insert after last manual vc_*.inl include
        matches = list(re.finditer(r'#include "vc_[^"]+\.inl"[^\n]*\n', src))
        if matches:
            ins = matches[-1].end()
            result = src[:ins] + '\n' + new_block + '\n' + src[ins:]
        else:
            result = src + '\n' + new_block + '\n'

    with open(VC_C_PATH, "w") as f:
        f.write(result)
    return True


SUB_DIRS = ['fire', 'water', 'wood', 'metal', 'earth', 'taiji', 'common']

def update_subdir_includes(comp_dir, manifest=None, dry_run=False):
    """
    Auto-sync @gen:fire_includes / @gen:water_includes / etc. blocks in each
    element master .inl file. New .inl files detected on disk but not yet
    manually included are added; files removed from disk are cleaned up.
    Files listed in manifest's 'exclude_from_auto_include' (<subdir> → [files])
    are never auto-included (use for common/ files that belong in vc_archetype.inl).
    """
    exclude_map = {}
    if manifest and "exclude_from_auto_include" in manifest:
        exclude_map = manifest["exclude_from_auto_include"]

    changed = False
    for subdir in SUB_DIRS:
        master_path = os.path.join(comp_dir, subdir, f"{subdir}.inl")
        subdir_path = os.path.join(comp_dir, subdir)
        if not os.path.exists(master_path) or not os.path.isdir(subdir_path):
            continue

        files = sorted(f for f in os.listdir(subdir_path)
                       if f.endswith('.inl') and f != f"{subdir}.inl")
        exclude = set(exclude_map.get(subdir, []))

        with open(master_path) as f:
            src = f.read()

        gen_key = f"{subdir}_includes"
        gen_pat = re.compile(
            r'// @gen:' + gen_key + r' begin.*?// @gen:' + gen_key + r' end',
            re.DOTALL)

        src_without_gen = gen_pat.sub('', src) if gen_pat.search(src) else src
        manual_inl = set(re.findall(r'#include\s+"([^"]+\.inl)"', src_without_gen))

        gen_inl = sorted(f for f in files if f not in manual_inl and f not in exclude)

        m = gen_pat.search(src)
        cur_gen_inl = set()
        if m:
            cur_gen_inl = set(re.findall(r'#include\s+"([^"]+\.inl)"', m.group(0)))

        added   = [f for f in gen_inl if f not in cur_gen_inl]
        removed = [f for f in cur_gen_inl if f not in set(gen_inl)]

        if not added and not removed:
            continue
        changed = True
        if dry_run:
            if added:
                print(f"[sync_vfx_test] {subdir}/{subdir}.inl: would add {added}")
            if removed:
                print(f"[sync_vfx_test] {subdir}/{subdir}.inl: would remove {removed}")
            continue

        new_block = (f"// @gen:{gen_key} begin\n" +
                     "\n".join(f'#include "{f}"' for f in gen_inl) +
                     f"\n// @gen:{gen_key} end")

        if m:
            result = src[:m.start()] + new_block + src[m.end():]
        else:
            result = src.rstrip() + "\n" + new_block + "\n"

        with open(master_path, "w") as f:
            f.write(result)

        if added:
            print(f"[sync_vfx_test] {subdir}/{subdir}.inl: +{len(added)} include(s): {', '.join(added)}")
        if removed:
            print(f"[sync_vfx_test] {subdir}/{subdir}.inl: -{len(removed)} include(s): {', '.join(removed)}")

    return changed


def update_vc_h(inl_fns, excluded, dry_run=False):
    """
    Sync the @gen:vc_declarations section in visual_composer.h.
    Adds declarations for new VFX_* functions; removes stale ones from the @gen block.
    Hand-written declarations (outside @gen) are never touched.
    """
    with open(HEADER_PATH) as f:
        src = f.read()

    # Functions in the @gen block that are still valid (in inl_fns, not excluded)
    gen_pat = re.compile(
        r'// @gen:vc_declarations begin.*?// @gen:vc_declarations end', re.DOTALL)
    src_without_gen = gen_pat.sub('', src)
    manually_declared = set(re.findall(r'\b(VFX_\w+)\s*\(', src_without_gen))

    # All functions that should be in the @gen block
    want_gen_fns = sorted(
        fn for fn in inl_fns
        if fn not in manually_declared and fn not in excluded
    )

    # Current @gen block
    m = gen_pat.search(src)
    cur_gen_fns = set()
    if m:
        cur_gen_fns = set(re.findall(r'\b(VFX_\w+)\s*\(', m.group(0)))

    added   = [fn for fn in want_gen_fns if fn not in cur_gen_fns]
    removed = [fn for fn in cur_gen_fns  if fn not in set(want_gen_fns)]

    if not added and not removed:
        return False

    if added:
        print(f"[sync_vfx_test] visual_composer.h: +{len(added)} declaration(s): {', '.join(added)}")
    if removed:
        print(f"[sync_vfx_test] visual_composer.h: -{len(removed)} declaration(s): {', '.join(removed)}")
    if dry_run:
        return True

    decl_lines = [
        f"{inl_fns[fn][1]} {fn}({inl_fns[fn][0]});"
        for fn in want_gen_fns
    ]
    new_block = gen_vc_declarations_block(decl_lines)
    result, found = replace_section(src, "vc_declarations", new_block)
    if not found:
        result = src.replace(
            "#endif // VISUAL_COMPOSER_H",
            new_block + "\n#endif // VISUAL_COMPOSER_H"
        )

    with open(HEADER_PATH, "w") as f:
        f.write(result)
    return True


# ── vfx_test.c generators ─────────────────────────────────────────────────────

def expand(template):
    for k, v in PLACEHOLDERS.items():
        template = template.replace(k, v)
    return template

def gen_names_array(entries):
    labels = [e["label"] for e in entries]
    rows = []
    for i in range(0, len(labels), 6):
        rows.append("    " + ", ".join(f'"{l}"' for l in labels[i:i+6]) + ",")
    return "\n".join([
        "// @gen:newfx_names begin",
        f"// {len(entries)} entries — auto-managed by sync_vfx_test.py",
        "static const char* s_newFxNames[] = {",
    ] + rows + ["};", "// @gen:newfx_names end"])

def gen_categories_array(entries):
    ids = [CAT_IDS.get(e.get("category", "util"), 6) for e in entries]
    rows = []
    for i in range(0, len(ids), 10):
        rows.append("    " + ", ".join(str(x) for x in ids[i:i+10]) + ",")
    return "\n".join([
        "// @gen:newfx_categories begin",
        "// NEWFX_CAT_FIRE=0 WATER=1 WOOD=2 METAL=3 EARTH=4 TAIJI=5 UTIL=6",
        "static const int s_newFxCategories[] = {",
    ] + rows + ["};", "// @gen:newfx_categories end"])

def gen_trigger_block(entries):
    oneshots = [(i, e) for i, e in enumerate(entries) if e["type"] == "oneshot"]
    lines = ["// @gen:newfx_trigger begin"]
    if not oneshots:
        lines += [f"{INDENT}{{", f"{INDENT}    s_isPlayingMesh = true;",
                  f"{INDENT}    s_meshTime = 0.0f;", f"{INDENT}}}"]
    else:
        # $SEED expands to the `posSeed` local — only declare it here (this
        # block is a standalone if/else chain, not inside gen_draw_block's
        # switch scope where posSeed is already declared) when at least one
        # oneshot trigger_call actually references it, else it's an unused
        # local. Bug found 2026-07: previously always expanded $SEED but
        # never declared posSeed here, producing an undeclared-identifier
        # compile error the moment any oneshot entry used $SEED.
        if any("$SEED" in e["trigger_call"] for _, e in oneshots):
            lines.append(f"{INDENT}int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;")
        first = True
        for idx, e in oneshots:
            kw = "if" if first else "} else if"
            first = False
            lines.append(f"{INDENT}{kw} (s_testIndex == {idx}) {{ /* {e['label']} */")
            lines.append(f"{INDENT}    {expand(e['trigger_call'])};")
        lines += [f"{INDENT}}} else {{",
                  f"{INDENT}    /* continuous — handled per-frame in VFXTest_Draw3D */",
                  f"{INDENT}    s_isPlayingMesh = true;",
                  f"{INDENT}    s_meshTime = 0.0f;",
                  f"{INDENT}}}"]
    lines.append("// @gen:newfx_trigger end")
    return "\n".join(lines)

def gen_render_trigger_block(entries):
    rmap = {"$POS": "pos", "$TIME": "0.0f", "$PROG": "0.0f", "$SEED": "0"}
    def rexpand(t):
        for k, v in rmap.items(): t = t.replace(k, v)
        return t
    RINDENT = "    "
    lines = ["// @gen:newfx_render_trigger begin",
             f"{RINDENT}switch (newfxIndex) {{"]
    for idx, e in enumerate(entries):
        if e["type"] != "oneshot": continue
        lines.append(f"{RINDENT}case {idx}: {rexpand(e['trigger_call'])}; break;")
    lines += [f"{RINDENT}default: break;", f"{RINDENT}}}",
              "// @gen:newfx_render_trigger end"]
    return "\n".join(lines)

def gen_draw_block(entries):
    lines = [
        "// @gen:newfx_draw begin",
        f"{INDENT}float progress = fminf(s_meshTime / 1.0f, 1.0f);",
        f"{INDENT}int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;",
        f"{INDENT}switch (s_testIndex) {{",
    ]
    for idx, e in enumerate(entries):
        if e["type"] != "continuous": continue
        if "draw_block" in e:
            blines = e["draw_block"]
            lines.append(f"{INDENT}    case {idx}: {expand(blines[0])}")
            for bl in blines[1:-1]:
                lines.append(f"{INDENT}        {expand(bl)}")
            lines.append(f"{INDENT}        break;")
            lines.append(f"{INDENT}    {expand(blines[-1])}")
        else:
            lines.append(f"{INDENT}    case {idx}: {expand(e['draw_call'])}; break;")
    lines += [f"{INDENT}}}", "// @gen:newfx_draw end"]
    return "\n".join(lines)

def update_count(content, count):
    def replacer(m): return m.group(0).replace(m.group(1), str(count))
    pat = re.compile(r'maxIdx\s*=\s*(\d+);\s*names\s*=\s*s_newFxNames;[^\n]*// @gen:newfx_count')
    return pat.sub(replacer, content)


# ── manifest helpers ───────────────────────────────────────────────────────────

def all_excluded(manifest):
    result = set()
    for v in manifest.get("excluded", {}).values():
        if isinstance(v, list):
            result.update(v)
        elif isinstance(v, dict):
            result.update(k for k in v if not k.startswith("_"))
    return result

def base_fn(fn):
    for s in ELEM_SUFFIXES:
        if fn.endswith(s): return fn[:-len(s)]
    return fn

def is_synthetic(fn):
    return any(fn.endswith(s) for s in ELEM_SUFFIXES)


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="Dry-run: report changes but write nothing")
    args = ap.parse_args()
    dry = args.check

    # ── 1. Scan .inl files ────────────────────────────────────────────────────
    inl_fns = scan_inl_functions(COMP_DIR)   # fn_name → (params, ret, fname)

    # ── 2. Load manifest ──────────────────────────────────────────────────────
    with open(MANIFEST_PATH) as f:
        manifest = json.load(f)
    entries   = manifest.get("entries", [])
    excluded  = all_excluded(manifest)
    overrides = manifest.get("overrides", {})

    # ── 3. Compute entry diff ─────────────────────────────────────────────────
    entry_base_fns = {base_fn(e["fn"]) for e in entries}
    testable_fns   = {fn for fn in inl_fns if fn not in excluded}

    # New real functions not covered by any existing entry (incl. synthetic aliases)
    new_fns = sorted(testable_fns - entry_base_fns - {base_fn(fn) for fn in excluded})

    # Entries to remove: function deleted from .inl OR moved to excluded
    removed = [e for e in entries
               if not is_synthetic(e["fn"])
               and (base_fn(e["fn"]) not in inl_fns
                    or base_fn(e["fn"]) in excluded)]

    # ── 4. Report ─────────────────────────────────────────────────────────────
    complex_fns = []
    if new_fns:
        print(f"[sync_vfx_test] Auto-adding {len(new_fns)} new function(s):")
        for fn in new_fns:
            _, _, is_cplx = (lambda e, c: (e, e, c))(*infer_entry(fn, inl_fns[fn][0]))
            tag = "  (complex — check call)" if is_cplx else ""
            print(f"  + {fn}{tag}")
            if is_cplx:
                complex_fns.append(fn)
    if removed:
        print(f"[sync_vfx_test] Removing {len(removed)} deleted function(s):")
        for e in removed:
            print(f"  - {e['fn']}")
    if not new_fns and not removed:
        print("[sync_vfx_test] manifest entries in sync with .inl files.")

    if dry:
        # Still check visual_composer files
        update_vc_c(COMP_DIR, dry_run=True)
        update_vc_h(inl_fns, excluded, dry_run=True)
        update_subdir_includes(COMP_DIR, manifest=manifest, dry_run=True)
        sys.exit(0 if (not new_fns and not removed) else 1)

    # ── 5. Update entries ─────────────────────────────────────────────────────
    removed_fns  = {e["fn"] for e in removed}
    new_entries  = [e for e in entries if e["fn"] not in removed_fns]

    # Apply overrides to existing entries
    for e in new_entries:
        if e["fn"] in overrides:
            e.update({k: v for k, v in overrides[e["fn"]].items() if k != "fn"})

    # Auto-generate and append new entries
    for fn in new_fns:
        if fn in overrides:
            e = {"fn": fn, **overrides[fn]}
        else:
            e, _ = infer_entry(fn, inl_fns[fn][0])
        new_entries.append(e)

    if complex_fns:
        print(f"\n[sync_vfx_test] TIP — these auto-generated calls may need tuning:")
        print(f"  Add to \"overrides\" in {os.path.basename(MANIFEST_PATH)} to customize.")

    # ── 6. Save manifest ──────────────────────────────────────────────────────
    if new_fns or removed:
        manifest["entries"] = new_entries
        if "overrides" not in manifest:
            manifest["overrides"] = {}
        with open(MANIFEST_PATH, "w") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
            f.write("\n")

    # ── 7. Update visual_composer.c and .h ───────────────────────────────────
    update_vc_c(COMP_DIR)
    update_vc_h(inl_fns, excluded)

    # ── 7b. Update element master .inl includes ──────────────────────────────
    update_subdir_includes(COMP_DIR, manifest=manifest)

    # ── 8. Regenerate vfx_test.c ─────────────────────────────────────────────
    with open(VFX_TEST_PATH) as f:
        vfx = f.read()

    for key, gen in [
        ("newfx_names",          gen_names_array(new_entries)),
        ("newfx_categories",     gen_categories_array(new_entries)),
        ("newfx_trigger",        gen_trigger_block(new_entries)),
        ("newfx_render_trigger", gen_render_trigger_block(new_entries)),
        ("newfx_draw",           gen_draw_block(new_entries)),
    ]:
        vfx, found = replace_section(vfx, key, gen)
        if not found:
            print(f"  WARNING: @gen:{key} marker not found in vfx_test.c — skipped",
                  file=sys.stderr)

    vfx = update_count(vfx, len(new_entries))

    with open(VFX_TEST_PATH, "w") as f:
        f.write(vfx)

    print(f"[sync_vfx_test] vfx_test.c updated ({len(new_entries)} NEWFX entries).")


if __name__ == "__main__":
    main()
