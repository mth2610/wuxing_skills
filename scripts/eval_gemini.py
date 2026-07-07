#!/usr/bin/env python3
"""
Evaluate a VFX screenshot with Gemini Vision.

Usage:
    python3 scripts/eval_gemini.py \\
        --image /tmp/vfx_eval.png \\
        --vfx VFX_ComposeElementalMist \\
        --material ICE \\
        --description "cold dry-ice mist creeping from a point, heavy/slow, 3 layers" \\
        --params '{"fog_spawn_chance":0.6,"wisp_spawn_chance":0.28,"viscosity":2.8,"radial_push":-0.22}' \\
        [--api-key YOUR_KEY]  # or set GEMINI_API_KEY env var
        [--model gemini-3.5-flash]
        [--out scripts/vfx_feedback/result.json]

Output (stdout + --out file):
    JSON with keys: vfx, material, overall (0-10), pass (bool),
    scores {identity, motion, density, coherence},
    issues [{aspect, severity, description}],
    hints [{param, direction, reason}]
"""

import argparse
import base64
import json
import os
import sys
import urllib.request
import urllib.error

def _load_dotenv():
    """Load .env from project root (two levels up from this script) if present."""
    env_path = os.path.join(os.path.dirname(__file__), "..", ".env")
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


RUBRIC = """
You are a VFX art director for a wuxia (Chinese martial arts) game.
Evaluate the VFX screenshot against the criteria below.
The arena is a dark isometric night-time scene. Effects should feel
cinematic and elemental — never robotic or generic.

### Effect info
Name: {vfx}
Element/Material: {material}
Description: {description}
Current parameter values: {params}

### Scoring criteria (each 0-10)
- identity    : Does it feel unmistakably like {material}? (color palette, motion style, texture)
- motion      : Is movement organic and appropriately paced? (too fast = robotic, too slow = dead)
- density     : Right visual weight? (too sparse = invisible, too dense = noise)
- coherence   : Do all layers work together as one unified effect?

### Output format
Reply ONLY with valid JSON, no markdown fences, no extra text:
{{
  "overall": <int 0-10>,
  "pass": <bool, true if overall>=7 and no high-severity issues>,
  "scores": {{
    "identity": <int>,
    "motion": <int>,
    "density": <int>,
    "coherence": <int>
  }},
  "issues": [
    {{"aspect": "<identity|motion|density|coherence|technical>",
      "severity": "<low|medium|high>",
      "description": "<one sentence, concrete>"}}
  ],
  "hints": [
    {{"param": "<param name from current params, or free-text>",
      "direction": "<increase|decrease|change to X>",
      "reason": "<one sentence>"}}
  ],
  "summary": "<2-3 sentence plain-English verdict>"
}}
"""


def load_image_b64(path: str) -> str:
    with open(path, "rb") as f:
        return base64.standard_b64encode(f.read()).decode()


def call_gemini(api_key: str, model: str, images_b64: list, prompt: str) -> dict:
    """images_b64: list of base64 strings (1 or more frames in sequence)."""
    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"{model}:generateContent?key={api_key}"
    )
    image_parts = [{"inline_data": {"mime_type": "image/png", "data": b64}}
                   for b64 in images_b64]
    frame_note = (f"\n\nNOTE: {len(images_b64)} frames shown in chronological order "
                  f"(left=early, right=late). Evaluate the full animation arc."
                  if len(images_b64) > 1 else "")
    body = {
        "contents": [{
            "parts": [{"text": prompt + frame_note}] + image_parts
        }],
        "generationConfig": {
            "temperature": 0.2,
            "responseMimeType": "application/json",
        },
    }
    data = json.dumps(body).encode()
    req = urllib.request.Request(url, data=data,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = json.loads(resp.read())
    except urllib.error.HTTPError as e:
        body_text = e.read().decode(errors="replace")
        raise RuntimeError(f"Gemini API error {e.code}: {body_text}") from e

    # Extract text from response
    try:
        text = raw["candidates"][0]["content"]["parts"][0]["text"]
    except (KeyError, IndexError) as e:
        raise RuntimeError(f"Unexpected Gemini response shape: {raw}") from e

    # Strip markdown fences if model ignored responseMimeType
    text = text.strip()
    if text.startswith("```"):
        text = text.split("```", 2)[1]
        if text.startswith("json"):
            text = text[4:]
        text = text.rsplit("```", 1)[0].strip()

    return json.loads(text)


def main():
    ap = argparse.ArgumentParser(description="Evaluate VFX screenshot(s) with Gemini")
    ap.add_argument("--image",       action="append", dest="images",
                    help="PNG frame path (repeat for multiple frames, sent in order)")
    ap.add_argument("--vfx",         required=True,  help="VFX function name")
    ap.add_argument("--material",    default="",     help="VC_MaterialId string (e.g. ICE)")
    ap.add_argument("--description", default="",     help="What this effect should look like")
    ap.add_argument("--params",      default="{}",   help="JSON string of current param values")
    ap.add_argument("--api-key",     default="",     help="Gemini API key (or GEMINI_API_KEY env)")
    ap.add_argument("--model",       default="gemini-2.5-flash", help="Gemini model id")
    ap.add_argument("--out",         default="",     help="Optional path to write JSON result")
    args = ap.parse_args()

    api_key = args.api_key or os.environ.get("GEMINI_API_KEY", "")
    if not api_key:
        print("ERROR: Gemini API key required (--api-key or GEMINI_API_KEY env)", file=sys.stderr)
        sys.exit(1)

    images = args.images or []
    if not images:
        print("ERROR: at least one --image required", file=sys.stderr)
        sys.exit(1)
    for p in images:
        if not os.path.isfile(p):
            print(f"ERROR: image not found: {p}", file=sys.stderr)
            sys.exit(1)

    try:
        params_obj = json.loads(args.params)
    except json.JSONDecodeError as e:
        print(f"ERROR: --params is not valid JSON: {e}", file=sys.stderr)
        sys.exit(1)

    prompt = RUBRIC.format(
        vfx=args.vfx,
        material=args.material or "(not specified)",
        description=args.description or "(not specified)",
        params=json.dumps(params_obj, indent=2),
    )

    print(f"[eval_gemini] Calling {args.model} for {args.vfx} ({args.material}) "
          f"[{len(images)} frame(s)]...", file=sys.stderr)

    images_b64 = [load_image_b64(p) for p in images]
    result = call_gemini(api_key, args.model, images_b64, prompt)

    result["_meta"] = {
        "vfx": args.vfx,
        "material": args.material,
        "images": images,
        "model": args.model,
        "params_evaluated": params_obj,
    }

    out_json = json.dumps(result, indent=2, ensure_ascii=False)

    if args.out:
        os.makedirs(os.path.dirname(args.out), exist_ok=True) if os.path.dirname(args.out) else None
        with open(args.out, "w") as f:
            f.write(out_json)
        print(f"[eval_gemini] Saved to {args.out}", file=sys.stderr)

    print(out_json)


if __name__ == "__main__":
    main()
