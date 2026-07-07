#!/usr/bin/env python3
"""
sync_vfx_test.py — Syncs NEWFX tab in sandbox/vfx_test.c with visual_composer.h.

Workflow:
  1. Parse core/composition/visual_composer.h for VFX_Compose* + other VFX_ public functions.
  2. Compare against scripts/vfx_test_manifest.json.
  3. Warn about unaccounted additions / removed functions.
  4. Regenerate the three @gen-marked sections in sandbox/vfx_test.c.

Usage:
  python3 scripts/sync_vfx_test.py          # apply sync
  python3 scripts/sync_vfx_test.py --check  # dry-run: report only, no writes
"""

import argparse
import json
import os
import re
import sys

REPO_ROOT     = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER_PATH   = os.path.join(REPO_ROOT, "core/composition/visual_composer.h")
MANIFEST_PATH = os.path.join(REPO_ROOT, "scripts/vfx_test_manifest.json")
VFX_TEST_PATH = os.path.join(REPO_ROOT, "sandbox/vfx_test.c")

# Placeholder → literal C substitution
PLACEHOLDERS = {
    "$POS":  "s_prefabStartPos",
    "$TIME": "s_meshTime",
    "$PROG": "progress",
    "$SEED": "posSeed",
}

INDENT = "          "  # 10 spaces — matches surrounding code in vfx_test.c


# ── helpers ──────────────────────────────────────────────────────────────────

def expand(template):
    """Replace $POS/$TIME/$PROG/$SEED placeholders in a C call template."""
    for k, v in PLACEHOLDERS.items():
        template = template.replace(k, v)
    return template


def parse_header(path):
    """Return set of public VFX function names declared in the header."""
    with open(path) as f:
        text = f.read()
    # Match function declarations: void/int FUNC_NAME(
    pattern = re.compile(r'\b(?:void|int)\s+(VFX_\w+)\s*\(')
    return set(pattern.findall(text))


def load_manifest(path):
    with open(path) as f:
        return json.load(f)


def all_excluded(manifest):
    excl = manifest.get("excluded", {})
    result = set()
    for v in excl.values():
        if isinstance(v, list):
            result.update(v)
        elif isinstance(v, dict):
            result.update(k for k in v if not k.startswith("_"))
    return result


# ── generators ───────────────────────────────────────────────────────────────

def gen_names_array(entries):
    """Generate s_newFxNames[] block."""
    labels = [e["label"] for e in entries]
    # pack 6 per row to match existing style
    rows = []
    for i in range(0, len(labels), 6):
        chunk = labels[i:i+6]
        rows.append("    " + ", ".join(f'"{l}"' for l in chunk) + ",")
    lines = [
        "// @gen:newfx_names begin",
        f"// {len(entries)} entries — edit scripts/vfx_test_manifest.json, then re-run sync_vfx_test.py",
        "static const char* s_newFxNames[] = {",
    ] + rows + [
        "};",
        "// @gen:newfx_names end",
    ]
    return "\n".join(lines)


def gen_trigger_block(entries):
    """Generate the if/else trigger dispatch inside TEST_CAT_NEWFX."""
    oneshots = [(i, e) for i, e in enumerate(entries) if e["type"] == "oneshot"]
    lines = [f"// @gen:newfx_trigger begin"]
    if not oneshots:
        lines.append(f"{INDENT}{{")
        lines.append(f"{INDENT}    s_isPlayingMesh = true;")
        lines.append(f"{INDENT}    s_meshTime = 0.0f;")
        lines.append(f"{INDENT}}}")
    else:
        first = True
        for idx, e in oneshots:
            kw = "if" if first else "} else if"
            first = False
            lines.append(f"{INDENT}{kw} (s_testIndex == {idx}) {{ /* {e['label']} */")
            call = expand(e["trigger_call"])
            lines.append(f"{INDENT}    {call};")
        lines.append(f"{INDENT}}} else {{")
        lines.append(f"{INDENT}    /* continuous — handled per-frame in VFXTest_Draw3D */")
        lines.append(f"{INDENT}    s_isPlayingMesh = true;")
        lines.append(f"{INDENT}    s_meshTime = 0.0f;")
        lines.append(f"{INDENT}}}")
    lines.append(f"// @gen:newfx_trigger end")
    return "\n".join(lines)


def gen_draw_block(entries):
    """Generate the switch dispatch inside VFXTest_Draw3D's NEWFX branch."""
    lines = [
        f"// @gen:newfx_draw begin",
        f"{INDENT}float progress = fminf(s_meshTime / 1.0f, 1.0f);",
        f"{INDENT}int posSeed = (int)(s_prefabStartPos.x * 17.0f + s_prefabStartPos.z * 31.0f) & 0xFFFF;",
        f"{INDENT}switch (s_testIndex) {{",
    ]
    for idx, e in enumerate(entries):
        if e["type"] != "continuous":
            continue
        if "draw_block" in e:
            # multiline block
            block_lines = e["draw_block"]
            first_line = expand(block_lines[0])
            lines.append(f"{INDENT}    case {idx}: {first_line}")
            for bl in block_lines[1:-1]:
                lines.append(f"{INDENT}        {expand(bl)}")
            last_line = expand(block_lines[-1])
            lines.append(f"{INDENT}        break;")
            lines.append(f"{INDENT}    {last_line}")
        else:
            call = expand(e["draw_call"])
            lines.append(f"{INDENT}    case {idx}: {call}; break;")
    lines += [
        f"{INDENT}}}",
        f"// @gen:newfx_draw end",
    ]
    return "\n".join(lines)


# ── section replacement ───────────────────────────────────────────────────────

def replace_section(content, key, new_body):
    """Replace everything between @gen:{key} begin and @gen:{key} end (inclusive)."""
    begin_tag = f"// @gen:{key} begin"
    end_tag   = f"// @gen:{key} end"
    pattern = re.compile(
        re.escape(begin_tag) + r".*?" + re.escape(end_tag),
        re.DOTALL,
    )
    if not pattern.search(content):
        print(f"  WARNING: marker '@gen:{key}' not found in vfx_test.c — skipping section", file=sys.stderr)
        return content
    return pattern.sub(new_body, content)


def update_count(content, count):
    """Replace the hard-coded NEWFX maxIdx number on lines tagged // @gen:newfx_count."""
    def replacer(m):
        return m.group(0).replace(m.group(1), str(count))
    pattern = re.compile(r'maxIdx\s*=\s*(\d+);\s*names\s*=\s*s_newFxNames;[^\n]*// @gen:newfx_count')
    if not pattern.search(content):
        print("  WARNING: '@gen:newfx_count' tag not found in vfx_test.c — maxIdx not updated", file=sys.stderr)
        return content
    return pattern.sub(replacer, content)


# ── diff report ───────────────────────────────────────────────────────────────

def report_diff(header_fns, entries, excluded_fns):
    accounted = {e["fn"] for e in entries} | excluded_fns
    new_fns   = header_fns - accounted
    removed   = {e["fn"] for e in entries} - header_fns

    ok = True
    if new_fns:
        ok = False
        print("\n[sync_vfx_test] NEW functions in visual_composer.h not yet in manifest:")
        for fn in sorted(new_fns):
            print(f"  + {fn}")
        print("  → Add them to scripts/vfx_test_manifest.json (entries or excluded.deferred).")
    if removed:
        ok = False
        print("\n[sync_vfx_test] Functions in manifest REMOVED from visual_composer.h:")
        for fn in sorted(removed):
            print(f"  - {fn}")
        print("  → Remove or comment them out in scripts/vfx_test_manifest.json.")
    if ok:
        print("[sync_vfx_test] Manifest is in sync with visual_composer.h.")
    return ok


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Dry-run: report only, no writes")
    args = parser.parse_args()

    header_fns = parse_header(HEADER_PATH)
    manifest   = load_manifest(MANIFEST_PATH)
    entries    = manifest["entries"]
    excluded   = all_excluded(manifest)

    in_sync = report_diff(header_fns, entries, excluded)

    if args.check:
        sys.exit(0 if in_sync else 1)

    # Generate sections
    names_block   = gen_names_array(entries)
    trigger_block = gen_trigger_block(entries)
    draw_block    = gen_draw_block(entries)
    count         = len(entries)

    with open(VFX_TEST_PATH) as f:
        content = f.read()

    content = replace_section(content, "newfx_names",   names_block)
    content = replace_section(content, "newfx_trigger", trigger_block)
    content = replace_section(content, "newfx_draw",    draw_block)
    content = update_count(content, count)

    with open(VFX_TEST_PATH, "w") as f:
        f.write(content)

    print(f"[sync_vfx_test] vfx_test.c updated ({count} NEWFX entries).")


if __name__ == "__main__":
    main()
