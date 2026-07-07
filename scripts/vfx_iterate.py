#!/usr/bin/env python3
"""
VFX iteration loop: build → render → Gemini eval → print feedback for Claude.

Usage:
    python3 scripts/vfx_iterate.py \\
        --vfx VFX_ComposeElementalMist \\
        --index 33 \\
        --material ICE \\
        --description "cold dry-ice mist creeping outward, heavy, 3 layers" \\
        --params '{"fog_spawn_chance":0.6,"viscosity":2.8,"radial_push":-0.22}' \\
        [--warmup 90]          # frames before screenshot (default 90)
        [--iterations 1]       # how many build→eval cycles (default 1)
        [--pass-threshold 7]   # overall score to consider done (default 7)
        [--no-build]           # skip make (use existing binary)
        [--api-key KEY]        # or GEMINI_API_KEY env var

Each iteration:
  1. make (unless --no-build)
  2. ./wuxing --render-vfx <index> --warmup <N> --out <tmp.png>
  3. eval_gemini.py → JSON
  4. Print JSON + human-readable summary to stdout
  5. If pass → exit 0. If not done → print hints and wait for Claude to edit code.

Exit code: 0 if last eval passed, 1 otherwise.
"""

import argparse
import json
import os
import subprocess
import sys
import textwrap
from pathlib import Path

def _load_dotenv():
    """Load .env from project root if present."""
    env_path = Path(__file__).parent.parent / ".env"
    try:
        with open(env_path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    os.environ.setdefault(k.strip(), v.strip())
    except FileNotFoundError:
        pass

_load_dotenv()


SCRIPT_DIR   = Path(__file__).parent
PROJECT_DIR  = SCRIPT_DIR.parent
FEEDBACK_DIR = SCRIPT_DIR / "vfx_feedback"


def run(cmd: list, cwd=None, capture=False, timeout=120):
    """Run a subprocess; on failure print stderr and raise."""
    r = subprocess.run(
        cmd, cwd=cwd or PROJECT_DIR,
        capture_output=capture,
        timeout=timeout,
    )
    if r.returncode != 0:
        if capture:
            print(r.stderr.decode(errors="replace"), file=sys.stderr)
        raise RuntimeError(f"Command failed (exit {r.returncode}): {' '.join(cmd)}")
    return r


def build():
    print("[iterate] Building...", file=sys.stderr)
    run(["make"], timeout=180)
    print("[iterate] Build OK.", file=sys.stderr)


def render(index: int, warmup: int, out_path: str):
    run([
        str(PROJECT_DIR / "wuxing"),
        "--render-vfx", str(index),
        "--warmup",     str(warmup),
        "--out",        out_path,
    ], capture=True, timeout=60)
    if not os.path.isfile(out_path):
        raise RuntimeError(f"Render produced no output at {out_path}")


def render_frames(index: int, frame_warmups: list, base_path: str) -> list:
    """Render one PNG per warmup value; returns list of saved paths."""
    paths = []
    for w in frame_warmups:
        stem = base_path.replace(".png", f"_f{w:03d}.png")
        print(f"[iterate] Rendering index {index} warmup={w}...", file=sys.stderr)
        render(index, w, stem)
        size_kb = os.path.getsize(stem) // 1024
        print(f"[iterate]   saved ({size_kb} KB): {stem}", file=sys.stderr)
        paths.append(stem)
    return paths


def evaluate(images: list, vfx: str, material: str, description: str,
             params: str, api_key: str, model: str, out_path: str) -> dict:
    print(f"[iterate] Calling Gemini eval ({len(images)} frame(s))...", file=sys.stderr)
    image_args = []
    for p in images:
        image_args += ["--image", p]
    r = run([
        sys.executable,
        str(SCRIPT_DIR / "eval_gemini.py"),
        *image_args,
        "--vfx",         vfx,
        "--material",    material,
        "--description", description,
        "--params",      params,
        "--api-key",     api_key,
        "--model",       model,
        "--out",         out_path,
    ], capture=True, timeout=120)
    return json.loads(r.stdout.decode())


def print_compact(result: dict, iteration: int, inl_file: str):
    overall = result.get("overall", 0)
    passed  = result.get("pass", False)
    scores  = result.get("scores", {})
    hints   = result.get("hints", [])
    meta    = result.get("_meta", {})
    label   = "PASS" if passed else "FAIL"
    s = scores
    print(f"EVAL {overall}/10 {label} iter={iteration} "
          f"[id={s.get('identity','?')} mo={s.get('motion','?')} "
          f"de={s.get('density','?')} co={s.get('coherence','?')}]")
    print(f"VFX: {meta.get('vfx','')}  FILE: {inl_file}")
    if hints:
        print("HINTS:")
        for h in hints:
            print(f"  {h.get('param','')} → {h.get('direction','')}  ({h.get('reason','')})")


def print_report(result: dict, iteration: int):
    overall  = result.get("overall", 0)
    passed   = result.get("pass", False)
    scores   = result.get("scores", {})
    issues   = result.get("issues", [])
    hints    = result.get("hints", [])
    summary  = result.get("summary", "")
    meta     = result.get("_meta", {})

    bar   = "█" * overall + "░" * (10 - overall)
    label = "PASS ✓" if passed else "FAIL ✗"

    print(f"\n{'='*60}")
    print(f"  Iteration {iteration}  |  {meta.get('vfx','')}  [{meta.get('material','')}]")
    print(f"  Overall: {overall}/10  [{bar}]  {label}")
    print(f"  identity={scores.get('identity','?')}  "
          f"motion={scores.get('motion','?')}  "
          f"density={scores.get('density','?')}  "
          f"coherence={scores.get('coherence','?')}")
    print(f"{'='*60}")

    if summary:
        for line in textwrap.wrap(summary, 58):
            print(f"  {line}")
        print()

    if issues:
        print("  Issues:")
        for iss in issues:
            sev = iss.get("severity","?").upper()
            asp = iss.get("aspect","?")
            desc = iss.get("description","")
            print(f"    [{sev:6}] {asp}: {desc}")
        print()

    if hints:
        print("  Adjustment hints for Claude:")
        for h in hints:
            print(f"    • {h.get('param','')} → {h.get('direction','')}  "
                  f"({h.get('reason','')})")
        print()

    print(f"{'='*60}\n")


def main():
    ap = argparse.ArgumentParser(description="VFX build→render→eval loop")
    ap.add_argument("--vfx",         required=True,  help="VFX function name")
    ap.add_argument("--index",       required=True,  type=int, help="NEWFX tab index (0-33)")
    ap.add_argument("--material",    default="",     help="Material name (e.g. ICE)")
    ap.add_argument("--description", default="",     help="Visual description of the effect")
    ap.add_argument("--params",      default="{}",   help="JSON of current param values")
    ap.add_argument("--warmup",      default=-1,     type=int,
                    help="Warmup frames before screenshot. Default: auto (20 for oneshot, 90 for continuous)")
    ap.add_argument("--iterations",  default=1,      type=int)
    ap.add_argument("--pass-threshold", default=7,   type=int)
    ap.add_argument("--inl",         default="",     help="Path to the .inl file (shown in compact output)")
    ap.add_argument("--no-build",    action="store_true")
    ap.add_argument("--compact",     action="store_true", help="Print terse edit-prompt instead of full JSON")
    ap.add_argument("--api-key",     default="")
    ap.add_argument("--model",       default="gemini-2.5-flash")
    args = ap.parse_args()

    # Auto-detect frame schedule from manifest type
    warmup = args.warmup
    try:
        manifest = json.load(open(SCRIPT_DIR / "vfx_test_manifest.json"))
        entry = next((e for e in manifest["entries"] if e["fn"] == args.vfx), None)
        is_oneshot = entry and entry.get("type") == "oneshot"
    except Exception:
        is_oneshot = False

    if warmup >= 0:
        # Single explicit warmup → single frame (backward compat)
        frame_warmups = [warmup]
    elif is_oneshot:
        frame_warmups = [5, 15, 35]   # attack / peak / decay
        print("[iterate] Auto multi-frame (oneshot): warmup 5, 15, 35", file=sys.stderr)
    else:
        frame_warmups = [20, 60, 120]  # ramp / mid / steady
        print("[iterate] Auto multi-frame (continuous): warmup 20, 60, 120", file=sys.stderr)

    api_key = args.api_key or os.environ.get("GEMINI_API_KEY", "")
    if not api_key:
        print("ERROR: Gemini API key required (--api-key or GEMINI_API_KEY env)", file=sys.stderr)
        sys.exit(1)

    FEEDBACK_DIR.mkdir(exist_ok=True)

    last_result = None
    inl_file = args.inl or ""
    FEEDBACK_DIR.mkdir(exist_ok=True)
    for i in range(1, args.iterations + 1):
        print(f"\n[iterate] ── Iteration {i}/{args.iterations} ──", file=sys.stderr)

        if not args.no_build:
            build()

        base_png = str(FEEDBACK_DIR / f"{args.vfx}_iter{i:02d}.png")
        frame_paths = render_frames(args.index, frame_warmups, base_png)

        feedback_path = str(FEEDBACK_DIR / f"{args.vfx}_iter{i:02d}.json")
        result = evaluate(
            frame_paths, args.vfx, args.material, args.description,
            args.params, api_key, args.model, feedback_path,
        )
        last_result = result

        if args.compact:
            print_compact(result, i, inl_file or frame_paths[0])
        else:
            print_report(result, i)

        if result.get("overall", 0) >= args.pass_threshold and result.get("pass", False):
            print(f"[iterate] ✓ Passed threshold ({args.pass_threshold}) on iteration {i}.",
                  file=sys.stderr)
            sys.exit(0)

        if i < args.iterations:
            if not args.compact:
                print("[iterate] Edit the VFX .inl file, then press Enter to continue...")
                try:
                    input()
                except EOFError:
                    pass

    # Print final JSON to stdout (full mode only)
    if last_result and not args.compact:
        print(json.dumps(last_result, indent=2, ensure_ascii=False))

    passed = last_result and last_result.get("pass", False)
    sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
