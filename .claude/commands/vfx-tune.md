# /vfx-tune — Autonomous VFX composition tuner

Arguments: `<VFX_FunctionName> [iterations=5] [warmup=90]`

Examples:
- `/vfx-tune VFX_ComposeFlameWisp`
- `/vfx-tune VFX_ComposeElementalMist 8`
- `/vfx-tune VFX_ComposePlasmaOrb 3 120`

---

Parse `$ARGUMENTS`:
- `VFX_NAME` = first token (required)
- `ITERATIONS` = second token (default 5)
- `WARMUP` = third token (default 90)

Then run this procedure **without asking clarifying questions**:

## Step 1 — Resolve index and .inl file

Look up `VFX_NAME` in `scripts/vfx_test_manifest.json`:
```
python3 -c "
import json
d = json.load(open('scripts/vfx_test_manifest.json'))
for i, e in enumerate(d['entries']):
    if e['fn'] == 'VFX_NAME':
        print(i, e.get('type',''))
"
```
Then grep for the function definition to find its `.inl` file:
```
grep -rl "VFX_NAME" core/composition/ --include="*.inl"
```

## Step 2 — Spawn a subagent for the tuning loop

Spawn an Agent with this exact briefing (fill in resolved values):

```
You are a VFX composition tuner for a C/Raylib wuxia game (1 unit = 1 meter, isometric arena).

TASK: Tune VFX_NAME (NEWFX index INDEX) to pass Gemini eval (score ≥ 7/10).
FILE TO EDIT: PATH_TO_INL
MAX ITERATIONS: ITERATIONS
WARMUP FRAMES: WARMUP

## What to do each iteration:

1. Run eval (first iteration also builds):
   python3 scripts/vfx_iterate.py \
       --vfx VFX_NAME \
       --index INDEX \
       --inl PATH_TO_INL \
       --material MATERIAL \
       --description "DESCRIPTION" \
       --warmup WARMUP \
       --compact \
       --no-build   # omit on first iteration
   
   Read the compact output: score, FILE path, HINTS list.

2. If PASS → stop, report final score and what changed.

3. If FAIL → apply hints to PATH_TO_INL using Edit tool.
   Rules:
   - Read the file ONCE at the start, never re-read after edits.
   - Each edit: minimal old_string → new_string, target the specific param/value.
   - Don't restructure or rename — only tweak numeric values and color constants.
   - After editing, re-run with --no-build=false (rebuild each iteration).

4. Repeat up to ITERATIONS times.

## Constraints
- Scale: particle speed 1–3 m/s, force 3–7 N, mesh radius 0.10–0.20 m.
- Never add new API calls not already present in the file.
- Never change function signatures.
- If a hint says "add layer" but the layer API isn't already in the file, skip it.

Report back: final score, pass/fail, list of changes made (old value → new value).
```

Before spawning, fill in `MATERIAL` (guess from VFX name: fire→FIRE, water→WATER, wood→WOOD, metal→METAL, earth→EARTH, taiji→TAIJI, plasma→METAL, mist→WATER) and `DESCRIPTION` (one sentence from the function name).

## Step 3 — Report result

After the subagent finishes, summarize to the user in ≤3 lines: final score, pass/fail, key changes.
