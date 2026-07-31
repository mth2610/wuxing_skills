#!/usr/bin/env python3
"""
sync_vfx_test.py — Fully automatic VFX sync.

Scans core/composition/**.inl for VFX_* function definitions, then:
  1. Auto-updates scripts/vfx_test_manifest.json  (adds new / removes deleted entries,
                                                   drops overrides for deleted fns)
  2. Regenerates sandbox/vfx_test.c               (NEWFX tab)
  3. Auto-updates visual_composer.c               (#include "vc_*.inl")
  4. Auto-updates visual_composer.h               (function declarations)
  5. Auto-updates each <elem>/<elem>.inl master   (per-VFX #includes)
  6. Auto-updates visual_composer.c's archetype   (#include + the two per-frame
     blocks                                        dispatch calls)

A STATEFUL archetype declares itself by defining both `VC_<Name>_Update(float)`
and `VC_<Name>_Draw3D(Camera3D)`. Nothing needs registering: the pair is the
declaration, so creating or deleting the .inl is the only manual step. Missing
that wiring by hand is the expensive mistake — a forgotten dispatch call on ADD
compiles clean and the VFX simply never appears, because its pool never ticks.

ADDING and DELETING a per-VFX .inl file both propagate through all five, so the
only manual step is creating or deleting the file itself. Element folders are
discovered from disk — a new one needs no script edit.

Deleting is the case that used to break: a hand-removed .inl left a dangling
#include in its element master, which does not compile. Those are now pruned
even when the include sits outside the @gen block.

Include ORDER inside a master is preserved, never sorted — .inl bodies land in
one translation unit and some rely on statics from an earlier sibling.

Each `.inl` gets exactly ONE fixture in the NEW FX tab. The generated fixture
uses a standard scene contract: bursts fire at the clicked point, timed VFX
loop progress, line VFX span a default source→target, and follower VFX travel a
single stable 3D curve. New compositions therefore get a usable bench entry
without writing C or JSON by hand.

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
    "$POS":      "s_prefabStartPos",
    "$SOURCE":   "Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f})",
    "$TARGET":   "Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f})",
    "$CONTROL1": "Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.33f), (Vector3){0.0f, 0.9f, 0.7f})",
    "$CONTROL2": "Vector3Add(Vector3Lerp(Vector3Add(s_prefabStartPos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(s_prefabStartPos, (Vector3){2.5f, 1.8f, 0.8f}), 0.66f), (Vector3){0.0f, 0.5f, -0.7f})",
    "$VELOCITY": "(Vector3){1.4f, 2.2f, 0.5f}",
    "$TIME":     "s_meshTime",
    "$PROG":     "progress",
    "$SEED":     "posSeed",
    "$XFORM":    "&s_vfxFixtureXf[s_testIndex]",
}
MANIFEST_PLACEHOLDERS = {
    "$POS": "spawn origin / clicked point",
    "$SOURCE": "default line start, offset behind the origin",
    "$TARGET": "default line end, offset ahead of the origin",
    "$CONTROL1": "first elevated Bezier control point",
    "$CONTROL2": "second elevated Bezier control point",
    "$VELOCITY": "default launch velocity",
    "$TIME": "running fixture time",
    "$PROG": "looping normalized progress (0→1 over 2 seconds)",
    "$SEED": "position-derived deterministic seed",
    "$XFORM": "stable transform for follower effects",
}
INDENT  = "          "   # 10 spaces — matches surrounding code in vfx_test.c
CAT_IDS = {"fire": 0, "water": 1, "wood": 2, "metal": 3, "earth": 4, "taiji": 5, "common": 6}
MANIFEST_CATEGORIES = ["fire", "water", "wood", "metal", "earth", "taiji", "common"]

# P0 lifecycle catalogue.  A signature cannot say whether a function is an
# event, a per-frame draw, an emitter, or a history trail; fixture inference
# must never guess that contract.  Add every new fixture source here first.
#
# `type` is the sandbox implementation detail; `lifecycle` is the public
# contract shown in the manifest. FlameVolume is deliberately marked legacy:
# P2 replaces its frame-fed emitter with Spawn/Stop/Kill handles.
LIFECYCLE_SPECS = {
    "VFX_ComposeBeam":               ("draw",    "timed",      "continuous"),
    "VFX_ComposeCharacterAura":      ("emitter", "persistent", "persistent"),
    "VFX_ComposeChargeConverge":     ("draw",    "timed",      "continuous"),
    "VFX_ComposeConvergeMotes":      ("draw",    "timed",      "continuous"),
    "VFX_ComposeCoreGlow":           ("draw",    "timed",      "continuous"),
    "VFX_ComposeSmokeTrail":         ("trail",   "follower",   "continuous"),
    "VFX_ComposeDebrisShards":       ("event",   "burst",      "oneshot"),
    "VFX_ComposeDissolveExit":       ("draw",    "timed",      "continuous"),
    "VFX_ComposeEnergyBurst":        ("event",   "burst",      "oneshot"),
    "VFX_ComposeEnergyOrb":          ("draw",    "timed",      "continuous"),
    "VFX_ComposeGlintSparkle":       ("draw",    "timed",      "continuous"),
    "VFX_ComposeGroundWave":         ("draw",    "timed",      "continuous"),
    "VFX_ComposeImpactPackage":      ("event",   "burst",      "oneshot"),
    "VFX_ComposeImpactDust":         ("event",   "burst",      "oneshot"),
    "VFX_ComposeContactSpark":       ("event",   "burst",      "oneshot"),
    "VFX_ComposeEmberTrail":         ("emitter", "persistent", "persistent"),
    "VFX_ComposeShieldShell":        ("emitter", "persistent", "persistent"),
    "VFX_ComposeLightShaft":         ("draw",    "timed",      "continuous"),
    "VFX_ComposePortalDisc":         ("draw",    "timed",      "continuous"),
    "VFX_ComposeProjectile":         ("trail",   "follower",   "continuous"),
    "VFX_ComposeRibbonTrail":        ("trail",   "follower",   "continuous"),
    "VFX_ComposeRuneCircle":         ("draw",    "timed",      "continuous"),
    "VFX_ComposeShockRing":          ("draw",    "timed",      "continuous"),
    "VFX_ComposeSmokePuff":          ("event",   "burst",      "oneshot"),
    "VFX_ComposeSparkTrail":         ("event",   "burst",      "oneshot"),
    "VFX_ComposeSweepSlash":         ("draw",    "timed",      "continuous"),
    "VFX_ComposeVolumeTrail":        ("trail",   "follower",   "continuous"),
    "VFX_ComposeFissureStreak":      ("draw",    "timed",      "continuous"),
    "VFX_ComposeStonePillar":        ("draw",    "timed",      "continuous"),
    "VFX_ComposeFlameVolume":        ("emitter", "timed",      "continuous"),
    "VFX_ComposeBlackHole":          ("draw",    "timed",      "continuous"),
    "VFX_ComposeIceCrystal":         ("event",   "burst",      "oneshot"),
    "VFX_ComposeWaterStream":        ("draw",    "timed",      "continuous"),
}

# A fixture may supply a semantic preview surface while the public composition
# stays asset-agnostic. These are generated-call overrides, never edits inside
# sandbox/vfx_test.c's @gen block.
FIXTURE_SPAWN_OVERRIDES = {
    "VFX_ComposeShieldShell":
        "VFX_ShieldShell_SpawnEx($POS, VC_MAT_FIRE, 1.5f, 1.0f, VFXTest_ShieldFlowSurface())",
}

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
    "common": "EFFECT_PRESET_FIRE_EXPLOSION",
}

def infer_category(fn_name):
    low = fn_name.lower()
    # Source path carries stronger intent than broad words such as "wave".
    # A ground wave belongs to earth, not water, and an element folder always
    # wins over a generic common-composition name.
    for cat in ("fire", "water", "wood", "metal", "earth", "taiji"):
        if f"/{cat}/" in low or f" {cat}/" in low:
            return cat
    if any(k in low for k in ("ground", "earth", "rock", "fissure", "stone", "soil", "crack", "quake", "boulder", "pillar")):
        return "earth"
    for cat, kws in _ELEM_KW.items():
        if any(k in low for k in kws):
            return cat
    return "common"

def infer_mat_id(fn_name):
    # Contact dust describes displaced ground, not the element that hit it.
    # A generic Common fixture used FIRE here, turning the neutral dust test
    # orange and hiding the actual texture/material read.
    if fn_name == "VFX_ComposeImpactDust":
        return "VC_MAT_EARTH"
    cat = infer_category(fn_name)
    if cat == "common":
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
        is_pointer = tokens[-1].startswith('*')
        raw_name = tokens[-1].lstrip('*').rstrip('[]')
        raw_type = ' '.join(tokens[:-1])
        if is_pointer:
            raw_type += ' *'
        result.append((raw_type.strip(), raw_name.strip()))
    return result

def infer_arg(type_str, name, fn_name):
    t, n = type_str.strip(), name.lower()

    if 'Vector3' in t and '*' not in t:
        if any(k in n for k in ('velocity', 'vel', 'launch', 'impulse')):
            return '$VELOCITY'
        if n in ('p0', 'from', 'start', 'source') or any(k in n for k in ('from', 'start', 'source')):
            return '$SOURCE'
        if n in ('p1', 'control1', 'control_a'):
            return '$CONTROL1'
        if n in ('p2', 'control2', 'control_b'):
            return '$CONTROL2'
        if n in ('p3', 'to', 'end', 'target', 'dest', 'destination') or any(k in n for k in ('end', 'target', 'dest')):
            return '$TARGET'
        if 'normal' in n:
            return '(Vector3){0.0f, 1.0f, 0.0f}'
        if any(k in n for k in ('dir', 'direction', 'forward')):
            return '(Vector3){1.0f, 0.0f, 0.0f}'
        if any(k in n for k in ('pos', 'origin', 'center', 'location', 'spawn', 'base')):
            return '$POS'
        return '$POS'

    if 'Matrix' in t and '*' in t:
        return '$XFORM'
    if 'Vector3' in t and '*' in t:
        return 's_testPathPoints'
    if 'GroundHeightSampleFn' in t:
        return 'VFX_GroundHeightFromMap'
    if 'VFX_RibbonTrailKind' in t:
        return 'VFX_RIBBON_MAIN'
    if 'VFX_VolumeKind' in t:
        return 'VOL_ENERGY'
    if 'VFX_TrailStyle' in t:
        return 'VFX_TRAIL_RIBBON'
    if 'VFX_TrailSurface' in t:
        return 'NULL'

    if t == 'float':
        if fn_name == 'VFX_ComposeEmberTrail' and 'ember' in n and 'second' in n:
            # A one-per-second persistent fixture only proves that the handle
            # exists. EmberTrail needs a readable stream for visual review.
            return '14.0f'
        if fn_name == 'VFX_ComposeImpactPackage' and 'severity' in n:
            # A bench click is an actual impact event, not a timeline draw.
            # Stay below the package's hit-stop threshold (0.45).
            return '0.4f'
        if fn_name == 'VFX_ComposeLightShaft' and any(k in n for k in ('width', 'thick')):
            return '0.8f'
        if fn_name == 'VFX_ComposeLightShaft' and any(k in n for k in ('intensity', 'strength', 'power')):
            return '1.35f'
        if n in ('time', 't', 'elapsed', 'elapsedtime'):
            return '$TIME'
        if n in ('progress', 'prog', 'phase', 'normalized') or n.endswith('01'):
            # Loop rather than freeze at the end state; fixture progress is a
            # visual probe, not a gameplay clock.
            return '$PROG'
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
        if 'agent' in n:
            return 'Sandbox_GetPlayerAgentId()'
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

    # The fixture owns no external assets or user data. NULL is a valid default
    # for optional texture/data inputs and gives new VFX a compiling first bench.
    if '*' in t or '[' in name:
        return 'NULL'
    return '0'

def infer_fixture_kind(fn_name, params, return_type):
    """Choose a deterministic default scene, not a hand-written demo."""
    spec = LIFECYCLE_SPECS.get(fn_name)
    if spec:
        return spec[1]
    has_matrix = any('Matrix' in t and '*' in t for t, _ in params)
    if return_type == 'int' and has_matrix:
        return 'follower'
    if any(n.lower() in ('time', 't', 'elapsed', 'progress', 'prog', 'phase')
           or n.lower().endswith('01')
           for _, n in params):
        return 'timed'
    if any(k in fn_name.lower() for k in ('beam', 'stream', 'trail', 'orb', 'ring')):
        return 'timed'
    return 'burst'

def infer_kill_fn(fn_name, available_fns):
    """Resolve the conventional ComposeFoo → KillFoo lifecycle automatically.

    SmokeTrail is the one legacy exception: it owns a raw TrailSystem handle,
    so its public release function is KillTrail rather than VFX_KillSmokeTrail.
    """
    if fn_name == 'VFX_ComposeSmokeTrail':
        return 'KillTrail'
    if fn_name.startswith('VFX_Compose'):
        candidate = 'VFX_Kill' + fn_name[len('VFX_Compose'):]
        if candidate in available_fns:
            return candidate
    return None

def infer_entry(fn_name, info, available_fns):
    """
    Auto-generate a manifest entry dict from a function signature.
    Returns (entry_dict, is_complex) where is_complex=True means some params
    couldn't be inferred and the call may need a manual override.
    """
    params_str, return_type, source = info
    params = parse_params(params_str)
    spec = LIFECYCLE_SPECS.get(fn_name)
    if spec is None:
        raise SystemExit(f"[sync_vfx_test] missing P0 lifecycle metadata for {fn_name}; "
                         "add it to LIFECYCLE_SPECS before generating a fixture")
    lifecycle, fixture, fixture_type = spec
    args = []
    for type_str, name in params:
        arg = infer_arg(type_str, name, fn_name)
        args.append(arg)

    call = f"{fn_name}({', '.join(args)})"
    entry = {
        "source": source,
        "fn": fn_name,
        "label": camel_to_label(fn_name),
        "category": infer_category(source + ' ' + fn_name),
        "fixture": fixture,
        "lifecycle": lifecycle,
        "_auto": True,
    }
    kill_fn = infer_kill_fn(fn_name, available_fns)
    if fixture == 'follower':
        if kill_fn:
            entry["type"] = fixture_type
            entry["spawn_call"] = call
            entry["kill_fn"] = kill_fn
        else:
            entry["fixture"] = 'burst'
            entry["type"] = "oneshot"
            entry["trigger_call"] = call
    elif return_type == 'int' and kill_fn:
        # Attached/persistent compositions have no transform to drive here
        # (aura follows its agent internally), but must be explicitly released
        # when the user switches fixture. Keep the returned handle for that.
        entry["fixture"] = 'persistent'
        entry["type"] = fixture_type
        entry["spawn_call"] = call
        entry["kill_fn"] = kill_fn
    elif return_type == 'int':
        # A returned handle without a matching public Kill API is conservatively
        # treated as one-shot. Calling it once is safe; calling it every frame
        # would silently fill its pool (SparkTrail is the current example).
        entry["fixture"] = 'burst'
        entry["type"] = "oneshot"
        entry["trigger_call"] = call
    elif fixture == 'timed':
        entry["type"] = fixture_type
        entry["draw_call"] = call
    else:
        entry["type"] = "oneshot"
        entry["trigger_call"] = call
    if fn_name in FIXTURE_SPAWN_OVERRIDES:
        if "spawn_call" not in entry:
            raise SystemExit(f"[sync_vfx_test] {fn_name}: spawn override requires a persistent fixture")
        entry["spawn_call"] = FIXTURE_SPAWN_OVERRIDES[fn_name]
    return entry


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

def is_fixture_candidate(fn_name):
    """Public composition/draw API only. Setters, lifecycle and compatibility
    helpers never deserve their own button."""
    if fn_name.endswith('Ex') or 'Test' in fn_name:
        return False
    return fn_name.startswith('VFX_Compose') or fn_name.startswith('VFX_Draw')

def source_fixture_entries(inl_fns, excluded):
    """Choose one primary API per source `.inl`, preferring the function whose
    name mirrors `vc_<name>.inl`. This is the one-entry-per-VFX invariant."""
    by_source = {}
    for fn, info in inl_fns.items():
        if fn in excluded or not is_fixture_candidate(fn):
            continue
        by_source.setdefault(info[2], []).append((fn, info))

    entries = []
    for source in sorted(by_source):
        candidates = by_source[source]
        stem = os.path.basename(source)[3:-4] if os.path.basename(source).startswith('vc_') else ''
        expected = 'VFX_Compose' + ''.join(part.title() for part in stem.split('_'))
        candidates.sort(key=lambda item: (item[0] != expected,
                                          not item[0].startswith('VFX_Compose'), item[0]))
        entries.append(infer_entry(candidates[0][0], candidates[0][1], inl_fns))
    return entries


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


def discover_subdirs(comp_dir):
    """
    Every element folder that owns a master include (<name>/<name>.inl).
    Discovered from disk rather than hard-coded, so adding a new element
    directory needs no script edit. (The old hard-coded list silently omitted
    `plasma`, which meant plasma/plasma.inl was never synced at all.)
    """
    out = []
    for d in sorted(os.listdir(comp_dir)):
        p = os.path.join(comp_dir, d)
        if os.path.isdir(p) and os.path.isfile(os.path.join(p, f"{d}.inl")):
            out.append(d)
    return out


INCLUDE_LINE_RE = re.compile(r'^[ \t]*#include\s+"([^"]+\.inl)"[^\n]*\n', re.MULTILINE)

def _gen_block(gen_key, files):
    return (f"// @gen:{gen_key} begin\n"
            f"// {len(files)} include(s) — auto-managed by sync_vfx_test.py\n"
            + "".join(f'#include "{f}"\n' for f in files)
            + f"// @gen:{gen_key} end")


def update_subdir_includes(comp_dir, manifest=None, dry_run=False, archetypes=None):
    """
    Sync the @gen:<subdir>_includes block in each element master .inl so that
    ADDING and DELETING a per-VFX .inl file both propagate automatically.

    Three behaviours, in order:

    1. ADOPT — a master file with hand-written includes and no @gen block has
       them absorbed into one, in their EXISTING ORDER, at the position of the
       first one. Until this happens nothing in that folder is auto-managed:
       every include counts as "manual", so the generated block stays empty and
       the sync is a silent no-op. This is a one-time migration per folder.
    2. PRUNE — an include naming a file that is no longer on disk is removed,
       whether it sits inside the @gen block or outside it. A dangling include
       left by a hand deletion breaks the build, so these are always cleaned
       even in the manual region.
    3. APPEND — .inl files present on disk but not included anywhere are added
       to the end of the @gen block.

    Order is PRESERVED, never sorted: these are `.inl` bodies pasted into one
    translation unit, and some depend on statics defined by an earlier sibling
    (`fire.inl`'s run is deliberately not alphabetical). Re-sorting them would
    break the build in ways that look unrelated to this script.

    Files listed in the manifest's 'exclude_from_auto_include' (<subdir> → [files])
    are never auto-included — used for common/ files that belong to
    the visual_composer.c orchestrator instead.
    """
    exclude_map = (manifest or {}).get("exclude_from_auto_include", {})
    changed = False

    # Archetypes are included by visual_composer.c, so the element master must
    # NOT also pull them in — that is a double inclusion and a redefinition
    # error. Derived from the Update/Draw3D pair rather than read from the
    # manifest, so a NEW archetype is handled without anyone remembering to
    # list it. (The manifest's exclude_from_auto_include stays for the other
    # cases: include-only files with no pair.)
    arch_by_dir = {}
    for rel in (archetypes or {}):
        d, _, base = rel.rpartition('/')
        arch_by_dir.setdefault(d, set()).add(base)

    for subdir in discover_subdirs(comp_dir):
        subdir_path = os.path.join(comp_dir, subdir)
        master_path = os.path.join(subdir_path, f"{subdir}.inl")

        on_disk = {f for f in os.listdir(subdir_path)
                   if f.endswith('.inl') and f != f"{subdir}.inl"}
        exclude = set(exclude_map.get(subdir, [])) | arch_by_dir.get(subdir, set())

        with open(master_path) as f:
            src = f.read()

        gen_key = f"{subdir}_includes"
        gen_pat = re.compile(
            r'// @gen:' + gen_key + r' begin.*?// @gen:' + gen_key + r' end',
            re.DOTALL)
        m = gen_pat.search(src)

        # Includes inside the @gen block (ordered), and outside it (ordered).
        cur_gen = INCLUDE_LINE_RE.findall(m.group(0)) if m else []
        outside = src[:m.start()] + src[m.end():] if m else src
        manual  = INCLUDE_LINE_RE.findall(outside)

        adopted = []
        if not m and manual:
            # (1) ADOPT — take over the existing hand-written run, order intact.
            adopted = [f for f in manual if f not in exclude]
            cur_gen, manual = adopted, []

        # (2) PRUNE — anything naming a file that no longer exists.
        dangling_gen    = [f for f in cur_gen if f not in on_disk]
        dangling_manual = [f for f in manual  if f not in on_disk]
        kept = [f for f in cur_gen if f in on_disk and f not in exclude]
        # An include that moved into the exclude list also leaves the block.
        excluded_out = [f for f in cur_gen if f in on_disk and f in exclude]

        # (3) APPEND — on disk, referenced nowhere, not excluded.
        referenced = set(kept) | set(manual) | set(excluded_out)
        appended = sorted(f for f in on_disk
                          if f not in referenced and f not in exclude)
        want = kept + appended
        # SmokePuff exports shared sheet state consumed by EnergyBurst and
        # ImpactPackage. It must precede those .inl bodies in the common TU.
        if subdir == "common" and "vc_smoke_puff.inl" in want:
            want.remove("vc_smoke_puff.inl")
            smoke_at = 1 if want and want[0] == "vc_common.inl" else 0
            want.insert(smoke_at, "vc_smoke_puff.inl")

        if not (adopted or appended or dangling_gen or dangling_manual or excluded_out) and want == cur_gen:
            continue
        changed = True

        tag = f"{subdir}/{subdir}.inl"
        verb = "would " if dry_run else ""
        if adopted:
            print(f"[sync_vfx_test] {tag}: {verb}adopt {len(adopted)} manual include(s) "
                  f"into @gen:{gen_key} (one-time migration)")
        if appended:
            print(f"[sync_vfx_test] {tag}: {verb}add +{len(appended)}: {', '.join(appended)}")
        for f_ in dangling_gen:
            print(f"[sync_vfx_test] {tag}: {verb}remove -{f_} (file deleted from disk)")
        for f_ in dangling_manual:
            print(f"[sync_vfx_test] {tag}: {verb}remove -{f_} (dangling MANUAL include, "
                  f"file deleted from disk — would not compile)")
        for f_ in excluded_out:
            print(f"[sync_vfx_test] {tag}: {verb}remove -{f_} (now in exclude_from_auto_include)")
        if dry_run:
            continue

        new_block = _gen_block(gen_key, want)

        if m:
            head, tail = src[:m.start()], src[m.end():]
            if dangling_manual:
                # Strip on BOTH sides — a leftover include after the block is
                # just as fatal, and leaving it also makes the script
                # non-idempotent (it would re-report the same file forever).
                dead = set(dangling_manual)
                strip = lambda t: INCLUDE_LINE_RE.sub(
                    lambda mm: "" if mm.group(1) in dead else mm.group(0), t)
                head, tail = strip(head), strip(tail)
            result = head + new_block + tail
        elif adopted:
            # Replace the manual run in place: block goes where the first one was.
            first = INCLUDE_LINE_RE.search(src)
            body  = INCLUDE_LINE_RE.sub(
                lambda mm: "" if mm.group(1) in set(adopted) | set(dangling_manual)
                           else mm.group(0),
                src)
            # Re-find the insertion point in the stripped text.
            anchor = src[:first.start()]
            body = body.replace(anchor, anchor + new_block + "\n", 1)
            result = body
        else:
            result = src.rstrip() + "\n\n" + new_block + "\n"

        with open(master_path, "w") as f:
            f.write(result)

    return changed


ARCH_UPDATE_RE = re.compile(r'\bstatic\s+void\s+VC_(\w+)_Update\s*\(\s*float\b')
ARCH_DRAW_RE   = re.compile(r'\bstatic\s+void\s+VC_(\w+)_Draw3D\s*\(\s*Camera3D\b')

def scan_archetypes(comp_dir):
    """
    An archetype is a .inl defining BOTH `VC_<Name>_Update(float)` and
    `VC_<Name>_Draw3D(Camera3D)` — a stateful VFX that owns a private pool and
    therefore needs a per-frame tick. Fire-and-forget compositions spawn into
    the global particle system and define neither.

    The pair IS the declaration: nothing has to be registered anywhere, so a new
    archetype cannot be forgotten and a deleted one cannot linger.

    Returns {include_path_relative_to_comp_dir: Name}, e.g.
    {"common/vc_proc_beam.inl": "ProcBeam"}.
    """
    found = {}
    for root, _dirs, files in os.walk(comp_dir):
        for fn in sorted(files):
            if not fn.endswith('.inl'):
                continue
            path = os.path.join(root, fn)
            with open(path, errors='ignore') as f:
                text = f.read()
            names = set(ARCH_UPDATE_RE.findall(text)) & set(ARCH_DRAW_RE.findall(text))
            if not names:
                continue
            rel = os.path.relpath(path, comp_dir).replace(os.sep, '/')
            if len(names) > 1:
                print(f"[sync_vfx_test] WARNING: {rel} defines {len(names)} archetype "
                      f"pairs ({', '.join(sorted(names))}); one per file is the "
                      f"convention — dispatching only {sorted(names)[0]}",
                      file=sys.stderr)
            found[rel] = sorted(names)[0]
    return found


FN_BODY_RE = {
    "archetype_update": re.compile(
        r'(void\s+VFX_Compose_Update\s*\(\s*float\s+\w+\s*\)\s*\n\{\n)(.*?)(\n\})', re.DOTALL),
    "archetype_draw": re.compile(
        r'(void\s+VFX_Compose_Draw3D\s*\(\s*Camera3D\s+\w+\s*\)\s*\n\{\n)(.*?)(\n\})', re.DOTALL),
}

def _adopt_archetype_markers(src, arch, dry_run):
    """
    Put the three @gen markers back around content that is already in the file.
    Used when they are missing entirely. Returns (new_src, ok).
    """
    # 1. Include run: wrap the span from the first to the last archetype include.
    lines = src.split("\n")
    idx = [i for i, l in enumerate(lines)
           if (mm := re.match(r'\s*#include\s+"([^"]+\.inl)"', l)) and mm.group(1) in arch]
    if not idx:
        print("[sync_vfx_test] WARNING: cannot adopt archetype markers — no archetype "
              "#include found in visual_composer.c. Add the @gen:archetype_includes "
              "markers by hand.", file=sys.stderr)
        return src, False

    lo, hi = min(idx), max(idx)
    print(f"[sync_vfx_test] visual_composer.c: {'would restore' if dry_run else 'restore'} "
          f"missing @gen:archetype_* markers (adopting {hi - lo + 1} include line(s) "
          f"and both dispatch bodies)")
    lines[lo:hi + 1] = (["// @gen:archetype_includes begin"]
                        + lines[lo:hi + 1]
                        + ["// @gen:archetype_includes end"])
    src = "\n".join(lines)

    # 2. The two dispatch bodies: wrap whatever they currently contain. The
    #    regeneration pass immediately after replaces the contents anyway, so
    #    stale calls in there do not survive — the markers just give it a target.
    for key, pat in FN_BODY_RE.items():
        m = pat.search(src)
        if not m:
            print(f"[sync_vfx_test] WARNING: cannot adopt @gen:{key} — could not find "
                  f"the function body in visual_composer.c. Add the markers by hand.",
                  file=sys.stderr)
            return src, False
        guard = "    (void)dt;\n" if key == "archetype_update" else "    (void)cam;\n"
        if guard.strip() in m.group(2):
            guard = ""
        src = (src[:m.start()] + m.group(1) + guard
               + f"// @gen:{key} begin\n" + m.group(2).rstrip("\n") + "\n"
               + f"// @gen:{key} end" + m.group(3) + src[m.end():])
    return src, True


def update_archetype_dispatch(comp_dir, dry_run=False):
    """
    Keep visual_composer.c's three @gen blocks in step with the archetype .inl
    files on disk: the include run, and the call lists inside VFX_Compose_Update
    and VFX_Compose_Draw3D.

    Deleting an archetype .inl used to need three hand edits in this file, and a
    missed dispatch call failed with an error naming a symbol rather than the
    file that was removed. Adding one had the mirror-image problem, and a missed
    call there is worse: it compiles clean and the VFX simply never appears,
    because its pool is never ticked.

    Existing order is preserved; new archetypes are appended. Include-only files
    (no Update/Draw pair) live outside the markers and are never touched.
    """
    arch = scan_archetypes(comp_dir)          # rel path → Name
    with open(VC_C_PATH) as f:
        src = f.read()

    inc_pat = re.compile(r'// @gen:archetype_includes begin.*?// @gen:archetype_includes end',
                         re.DOTALL)
    if not inc_pat.search(src):
        # ADOPT — markers absent (fresh checkout, an editor undo, a hand revert).
        # Warning-and-skip was the wrong behaviour here: the warning goes to
        # stderr, scrolls past, and the file silently stops being managed until
        # the next build fails. Rebuild the markers from what is already there.
        src, ok = _adopt_archetype_markers(src, arch, dry_run)
        if not ok:
            return False
        if dry_run:
            return True
        with open(VC_C_PATH, "w") as f:
            f.write(src)
    m = inc_pat.search(src)
    if not m:
        return False

    current = [p for p in INCLUDE_LINE_RE.findall(m.group(0))]
    # Some stateful survivors are intentionally included by a parent/master
    # before this generated run. Dispatch them, but never include their .inl a
    # second time: all composition files share one translation unit.
    source_without_gen = inc_pat.sub('', src)
    manual_arch = set(INCLUDE_LINE_RE.findall(source_without_gen))
    kept    = [p for p in current if p in arch and p not in manual_arch]
    dropped = [p for p in current if p not in arch or p in manual_arch]
    added   = sorted(p for p in arch if p not in current and p not in manual_arch)
    order   = kept + added
    dispatch_paths = list(dict.fromkeys(order + sorted(p for p in manual_arch if p in arch)))

    # THE INCLUDE IS THE TRIGGER, so it cannot be the only thing checked.
    #
    # This returned early whenever the include list already matched, on the
    # reasonable-looking assumption that a matching include run means matching
    # dispatch. It does not: add the #include BY HAND and the include list is
    # already correct, so this bails before generating either dispatch call —
    # and the result is exactly the failure this whole function exists to
    # prevent. It compiles clean, the pool is never ticked, and the VFX is
    # invisible with nothing in the log. Cost one debugging round on
    # vc_projectile.inl, 29/07.
    #
    # So verify what actually matters: that every archetype's two dispatch calls
    # are present. Cheap, and it makes a hand-written include harmless instead
    # of silently destructive.
    dispatch_ok = True
    for key, call in (("archetype_update", "{n}_Update(dt);"),
                      ("archetype_draw", "{n}_Draw3D(cam);")):
        blk = re.search(r'// @gen:' + key + r' begin.*?// @gen:' + key + r' end',
                        src, re.DOTALL)
        if not blk:
            dispatch_ok = False
            break
        for name in arch.values():
            if call.format(n="VC_" + name) not in blk.group(0):
                print(f"[sync_vfx_test] visual_composer.c: VC_{name} is included but "
                      f"its {key.split('_')[1]} dispatch is missing — regenerating "
                      f"(a hand-written #include does not tick the pool)")
                dispatch_ok = False
                break
        if not dispatch_ok:
            break

    # A renamed/deleted archetype may leave an old generated dispatch behind.
    # Presence-only validation misses that stale call and the TU then fails.
    if dispatch_ok:
        for key, call in (("archetype_update", "_Update(dt);"),
                          ("archetype_draw", "_Draw3D(cam);")):
            blk = re.search(r'// @gen:' + key + r' begin.*?// @gen:' + key + r' end', src, re.DOTALL)
            expected = {"VC_" + arch[p] + call for p in dispatch_paths}
            actual = set(re.findall(r'VC_\w+' + re.escape(call), blk.group(0))) if blk else set()
            if actual != expected:
                dispatch_ok = False
                break

    if not dropped and not added and dispatch_ok:
        return False

    for p in dropped:
        print(f"[sync_vfx_test] visual_composer.c: {'would remove' if dry_run else 'remove'} "
              f"archetype -{p} (no Update/Draw3D pair on disk) — include + 2 dispatch calls")
    for p in added:
        print(f"[sync_vfx_test] visual_composer.c: {'would add' if dry_run else 'add'} "
              f"archetype +{p} as VC_{arch[p]}_* — include + 2 dispatch calls")
    if dry_run:
        return True

    new_inc = ("// @gen:archetype_includes begin\n"
               + "".join(f'#include "{p}"\n' for p in order)
               + "// @gen:archetype_includes end")
    src = src[:m.start()] + new_inc + src[m.end():]

    for key, call in (("archetype_update", "{n}_Update(dt);"),
                      ("archetype_draw",   "{n}_Draw3D(cam);")):
        pat = re.compile(r'// @gen:' + key + r' begin.*?// @gen:' + key + r' end', re.DOTALL)
        if not pat.search(src):
            print(f"[sync_vfx_test] WARNING: @gen:{key} markers missing in "
                  f"visual_composer.c — calls not synced", file=sys.stderr)
            continue
        body = "".join("    VC_" + call.format(n=arch[p]) + "\n" for p in dispatch_paths)
        src = pat.sub(f"// @gen:{key} begin\n{body}// @gen:{key} end", src)

    with open(VC_C_PATH, "w") as f:
        f.write(src)
    return True


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
    ids = [CAT_IDS.get(e.get("category", "common"), 6) for e in entries]
    rows = []
    for i in range(0, len(ids), 10):
        rows.append("    " + ", ".join(str(x) for x in ids[i:i+10]) + ",")
    return "\n".join([
        "// @gen:newfx_categories begin",
        "// NEWFX_CAT_FIRE=0 WATER=1 WOOD=2 METAL=3 EARTH=4 TAIJI=5 COMMON=6",
        "static const int s_newFxCategories[] = {",
    ] + rows + ["};", "// @gen:newfx_categories end"])

def gen_fire_function(entries):
    """One owner for all one-shot calls.

    Both UI activation and headless warm-up call this function, so a source
    fixture's compose call exists once in vfx_test.c rather than being copied
    into every input path.
    """
    fire_entries = [(i, e) for i, e in enumerate(entries)
                    if e["type"] in ("oneshot", "persistent")]
    rmap = {
        "$POS": "pos",
        "$SOURCE": "Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f})",
        "$TARGET": "Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f})",
        "$CONTROL1": "Vector3Add(Vector3Lerp(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f}), 0.33f), (Vector3){0.0f, 0.9f, 0.7f})",
        "$CONTROL2": "Vector3Add(Vector3Lerp(Vector3Add(pos, (Vector3){-2.0f, 1.2f, 0.0f}), Vector3Add(pos, (Vector3){2.5f, 1.8f, 0.8f}), 0.66f), (Vector3){0.0f, 0.5f, -0.7f})",
        "$VELOCITY": "(Vector3){1.4f, 2.2f, 0.5f}",
        "$TIME": "0.0f", "$PROG": "0.0f", "$SEED": "posSeed",
        "$XFORM": "NULL",
    }
    def rexpand(t):
        for k, v in rmap.items():
            t = t.replace(k, v)
        return t

    lines = ["// @gen:newfx_fire begin"]
    if any("$SEED" in e.get("trigger_call", e.get("spawn_call", ""))
           for _, e in fire_entries):
        lines.append("    int posSeed = (int)(pos.x * 17.0f + pos.z * 31.0f) & 0xFFFF;")
    lines.append("    switch (newfxIndex) {")
    for idx, e in fire_entries:
        if e["type"] == "persistent":
            lines += [f"    case {idx}:",
                      f"        if (s_vfxFixtureHandle[{idx}] >= 0) {e['kill_fn']}(s_vfxFixtureHandle[{idx}]);",
                      f"        s_vfxFixtureHandle[{idx}] = {rexpand(e['spawn_call'])};",
                      "        return true;"]
        else:
            lines.append(f"    case {idx}: {rexpand(e['trigger_call'])}; return true;")
    lines += ["    default: return false;", "    }", "// @gen:newfx_fire end"]
    return "\n".join(lines)

def gen_stop_function(entries):
    """Generate the one release point for every fixture that owns a handle."""
    owned = [(i, e) for i, e in enumerate(entries)
             if e["type"] in ("continuous", "persistent") and e.get("kill_fn")]
    lines = ["// @gen:newfx_stop begin"]
    for idx, e in owned:
        lines += [f"    if (s_vfxFixtureHandle[{idx}] >= 0)",
                  f"        {e['kill_fn']}(s_vfxFixtureHandle[{idx}]);",
                  f"    s_vfxFixtureHandle[{idx}] = -1;",
                  f"    s_vfxFixtureLastTime[{idx}] = -1.0f;"]
    lines.append("// @gen:newfx_stop end")
    return "\n".join(lines)

def gen_trigger_block(entries):
    lines = ["// @gen:newfx_trigger begin"]
    lines += [f"{INDENT}if (!VFXTest_FireNewFx(s_testIndex, s_prefabStartPos)) {{",
              f"{INDENT}    /* continuous — handled per-frame in VFXTest_Draw3D */",
              f"{INDENT}    s_isPlayingMesh = true;",
              f"{INDENT}    s_meshTime = 0.0f;",
              f"{INDENT}}}"]
    lines.append("// @gen:newfx_trigger end")
    return "\n".join(lines)

def gen_render_trigger_block(entries):
    RINDENT = "    "
    lines = ["// @gen:newfx_render_trigger begin",
             f"{RINDENT}(void)VFXTest_FireNewFx(newfxIndex, spawnPos);",
              "// @gen:newfx_render_trigger end"]
    return "\n".join(lines)

def gen_draw_block(entries):
    lines = [
        "// @gen:newfx_draw begin",
        f"{INDENT}float progress = fmodf(s_meshTime, 2.0f) * 0.5f;",
    ]
    continuous = [e for e in entries if e["type"] == "continuous"]
    if any("$SEED" in e.get("draw_call", e.get("spawn_call", "")) for e in continuous):
        lines.append(f"{INDENT}int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;")
    lines.append(f"{INDENT}switch (s_testIndex) {{")
    for idx, e in enumerate(entries):
        if e["type"] != "continuous": continue
        if e.get("fixture") == "follower":
            spawn_call = expand(e['spawn_call']).replace(
                "&s_vfxFixtureXf[s_testIndex]", f"&s_vfxFixtureXf[{idx}]")
            lines += [f"{INDENT}    case {idx}:", f"{INDENT}    {{",
                      f"{INDENT}        float a = s_meshTime * 1.35f;",
                      f"{INDENT}        Vector3 fixturePos = Vector3Add(s_prefabStartPos,",
                      f"{INDENT}            (Vector3){{3.0f * sinf(a), 1.5f + 0.45f * sinf(a * 0.7f), 2.1f * cosf(a * 1.3f)}});",
                      f"{INDENT}        if (s_meshTime < s_vfxFixtureLastTime[{idx}] && s_vfxFixtureHandle[{idx}] >= 0)",
                      f"{INDENT}            {e['kill_fn']}(s_vfxFixtureHandle[{idx}]);",
                      f"{INDENT}        if (s_meshTime < s_vfxFixtureLastTime[{idx}]) s_vfxFixtureHandle[{idx}] = -1;",
                      f"{INDENT}        s_vfxFixtureLastTime[{idx}] = s_meshTime;",
                      f"{INDENT}        s_vfxFixtureXf[{idx}] = MatrixTranslate(fixturePos.x, fixturePos.y, fixturePos.z);",
                      f"{INDENT}        if (s_vfxFixtureHandle[{idx}] < 0)",
                      f"{INDENT}            s_vfxFixtureHandle[{idx}] = {spawn_call};",
                      f"{INDENT}        break;", f"{INDENT}    }}"]
        elif "draw_block" in e:
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

def validate_lifecycle_catalog(entries):
    """Fail before writing when a fixture can violate its primary contract."""
    valid = {"event", "draw", "emitter", "trail"}
    for e in entries:
        fn = e["fn"]
        lifecycle = e.get("lifecycle")
        if lifecycle not in valid:
            raise SystemExit(f"[sync_vfx_test] {fn}: invalid P0 lifecycle {lifecycle!r}")
        if lifecycle == "event":
            if e.get("fixture") != "burst" or e.get("type") != "oneshot" or "trigger_call" not in e:
                raise SystemExit(f"[sync_vfx_test] {fn}: Event must be a one-shot burst fixture")
        elif lifecycle == "draw":
            if e.get("fixture") != "timed" or e.get("type") != "continuous" or "draw_call" not in e:
                raise SystemExit(f"[sync_vfx_test] {fn}: Draw must be a timed per-frame fixture")
        elif lifecycle == "trail":
            if e.get("fixture") != "follower" or not e.get("kill_fn"):
                raise SystemExit(f"[sync_vfx_test] {fn}: Trail requires follower fixture + Kill")
        elif lifecycle == "emitter" and e.get("fixture") == "persistent" and not e.get("kill_fn"):
            raise SystemExit(f"[sync_vfx_test] {fn}: persistent Emitter requires Kill")

    by_fn = {e["fn"]: e for e in entries}
    for event_fn in ("VFX_ComposeImpactPackage", "VFX_ComposeSparkTrail"):
        e = by_fn.get(event_fn)
        if not e or e.get("type") != "oneshot" or "draw_call" in e:
            raise SystemExit(f"[sync_vfx_test] {event_fn}: Event must never be emitted every frame")

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
    entries = manifest.get("entries", [])
    excluded = all_excluded(manifest)

    # ── 3. One fixture per source `.inl` ──────────────────────────────────────
    new_entries = source_fixture_entries(inl_fns, excluded)
    if len(new_entries) > 96:
        raise SystemExit("[sync_vfx_test] fixture slot capacity exceeded (96); increase VFXTEST_FIXTURE_SLOTS first")
    validate_lifecycle_catalog(new_entries)
    old_by_source = {e.get("source", e.get("fn", "")): e for e in entries}
    new_by_source = {e["source"]: e for e in new_entries}
    added = [e for src, e in new_by_source.items() if src not in old_by_source]
    removed = [e for src, e in old_by_source.items() if src not in new_by_source]
    changed = (entries != new_entries or manifest.get("schema") != 2
               or manifest.get("_placeholders") != MANIFEST_PLACEHOLDERS
               or manifest.get("_categories") != MANIFEST_CATEGORIES
               or manifest.get("excluded") != {} or "overrides" in manifest)

    # ── 4. Report ─────────────────────────────────────────────────────────────
    if added:
        print(f"[sync_vfx_test] Adding {len(added)} default fixture(s):")
        for e in added:
            print(f"  + {e['source']} → {e['fn']} ({e['fixture']})")
    if removed:
        print(f"[sync_vfx_test] Removing {len(removed)} stale/duplicate fixture(s):")
        for e in removed:
            print(f"  - {e.get('source', e['fn'])}")
    if not added and not removed and not changed:
        print("[sync_vfx_test] one-fixture-per-.inl manifest is in sync.")

    if dry:
        # Still check visual_composer files + the element master includes.
        drift = changed
        drift |= bool(update_vc_c(COMP_DIR, dry_run=True))
        drift |= bool(update_vc_h(inl_fns, excluded, dry_run=True))
        arch = scan_archetypes(COMP_DIR)
        drift |= bool(update_subdir_includes(COMP_DIR, manifest=manifest,
                                             dry_run=True, archetypes=arch))
        drift |= bool(update_archetype_dispatch(COMP_DIR, dry_run=True))
        if not drift:
            print("[sync_vfx_test] everything in sync.")
        sys.exit(1 if drift else 0)

    # ── 5. Save the source-keyed manifest ─────────────────────────────────────
    if changed:
        manifest["schema"] = 2
        manifest["_comment"] = (
            "Generated fixture registry. One source .inl owns one NEW FX entry; "
            "sync_vfx_test.py supplies the default scene and inputs for new compositions."
        )
        manifest["_placeholders"] = MANIFEST_PLACEHOLDERS
        manifest["_categories"] = MANIFEST_CATEGORIES
        manifest["entries"] = new_entries
        # Composition discovery is now deliberately complete: a new .inl must
        # get a bench fixture automatically, never vanish behind a stale list.
        manifest["excluded"] = {}
        manifest.pop("overrides", None)
        with open(MANIFEST_PATH, "w") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
            f.write("\n")
        excluded = all_excluded(manifest)

    # ── 7. Update visual_composer.c and .h ───────────────────────────────────
    update_vc_c(COMP_DIR)
    update_vc_h(inl_fns, excluded)

    # ── 7b/7c. Element master includes + stateful-archetype dispatch ─────────
    # Archetypes are resolved first: the master must skip them (visual_composer.c
    # owns their include) or the file lands in the TU twice.
    arch = scan_archetypes(COMP_DIR)
    update_subdir_includes(COMP_DIR, manifest=manifest, archetypes=arch)
    update_archetype_dispatch(COMP_DIR)

    # ── 8. Regenerate vfx_test.c ─────────────────────────────────────────────
    with open(VFX_TEST_PATH) as f:
        vfx = f.read()

    for key, gen in [
        ("newfx_names",          gen_names_array(new_entries)),
        ("newfx_categories",     gen_categories_array(new_entries)),
        ("newfx_stop",           gen_stop_function(new_entries)),
        ("newfx_fire",           gen_fire_function(new_entries)),
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
