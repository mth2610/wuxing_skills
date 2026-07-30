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

        if not (adopted or appended or dangling_gen or dangling_manual or excluded_out):
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
    kept    = [p for p in current if p in arch]
    dropped = [p for p in current if p not in arch]
    added   = sorted(p for p in arch if p not in current)
    order   = kept + added

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
        body = "".join("    VC_" + call.format(n=arch[p]) + "\n" for p in order)
        src = pat.sub(f"// @gen:{key} begin\n{body}// @gen:{key} end", src)

    with open(VC_C_PATH, "w") as f:
        f.write(src)
    return True


def prune_manifest_cruft(manifest, inl_fns, dry_run=False):
    """
    A deleted VFX leaves its `overrides` key and any `excluded` listing behind.
    They are inert but they accumulate, and a later function reusing the name
    silently inherits a stale override. Drop overrides for functions that no
    longer exist; only WARN about `excluded`/`exclude_from_auto_include`, which
    are hand-curated and may intentionally name things not yet written.
    """
    changed = False
    stale_ov = [fn for fn in manifest.get("overrides", {}) if fn not in inl_fns]
    if stale_ov:
        changed = True
        verb = "would drop" if dry_run else "dropping"
        print(f"[sync_vfx_test] manifest: {verb} {len(stale_ov)} stale override(s): "
              f"{', '.join(stale_ov)}")
        if not dry_run:
            for fn in stale_ov:
                del manifest["overrides"][fn]

    for group, v in manifest.get("excluded", {}).items():
        if not isinstance(v, list):
            continue
        stale = [fn for fn in v if fn.startswith("VFX_") and fn not in inl_fns]
        if stale:
            print(f"[sync_vfx_test] NOTE: excluded.{group} names "
                  f"{len(stale)} function(s) that no longer exist: {', '.join(stale)}")

    for sub, files in manifest.get("exclude_from_auto_include", {}).items():
        if not isinstance(files, list):
            continue
        d = os.path.join(COMP_DIR, sub)
        if not os.path.isdir(d):
            continue
        stale = [f for f in files if not os.path.isfile(os.path.join(d, f))]
        if stale:
            print(f"[sync_vfx_test] NOTE: exclude_from_auto_include.{sub} names "
                  f"{len(stale)} missing file(s): {', '.join(stale)}")

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


def is_manual(entry):
    """Hand-written entry for something that is NOT a composition function in a
    .inl — e.g. a post-FX or engine API you still want a button for in the NEW FX
    tab. Without this the removal pass below deletes it on the next run, because
    it cannot find the name among the scanned .inl files, and it does so
    silently. Set "_manual": true in the manifest entry to keep it."""
    return bool(entry.get("_manual"))


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
               if not is_synthetic(e["fn"]) and not is_manual(e)
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
        # Still check visual_composer files + the element master includes.
        drift = bool(new_fns or removed)
        drift |= bool(update_vc_c(COMP_DIR, dry_run=True))
        drift |= bool(update_vc_h(inl_fns, excluded, dry_run=True))
        arch = scan_archetypes(COMP_DIR)
        drift |= bool(update_subdir_includes(COMP_DIR, manifest=manifest,
                                             dry_run=True, archetypes=arch))
        drift |= bool(update_archetype_dispatch(COMP_DIR, dry_run=True))
        drift |= bool(prune_manifest_cruft(manifest, inl_fns, dry_run=True))
        if not drift:
            print("[sync_vfx_test] everything in sync.")
        sys.exit(1 if drift else 0)

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
    pruned = prune_manifest_cruft(manifest, inl_fns)
    if new_fns or removed or pruned:
        manifest["entries"] = new_entries
        if "overrides" not in manifest:
            manifest["overrides"] = {}
        with open(MANIFEST_PATH, "w") as f:
            json.dump(manifest, f, indent=2, ensure_ascii=False)
            f.write("\n")

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
