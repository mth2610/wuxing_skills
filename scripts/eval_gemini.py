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
        [--model gemini-2.0-flash]
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


def call_gemini(api_key: str, model: str, image_b64: str, prompt: str) -> dict:
    url = (
        f"https://generativelanguage.googleapis.com/v1beta/models/"
        f"{model}:generateContent?key={api_key}"
    )
    body = {
        "contents": [{
            "parts": [
                {"text": prompt},
                {"inline_data": {"mime_type": "image/png", "data": image_b64}},
            ]
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
    ap = argparse.ArgumentParser(description="Evaluate VFX screenshot with Gemini")
    ap.add_argument("--image",       required=True,  help="Path to PNG screenshot")
    ap.add_argument("--vfx",         required=True,  help="VFX function name")
    ap.add_argument("--material",    default="",     help="VC_MaterialId string (e.g. ICE)")
    ap.add_argument("--description", default="",     help="What this effect should look like")
    ap.add_argument("--params",      default="{}",   help="JSON string of current param values")
    ap.add_argument("--api-key",     default="",     help="Gemini API key (or GEMINI_API_KEY env)")
    ap.add_argument("--model",       default="gemini-2.5-flash-lite", help="Gemini model id")
    ap.add_argument("--out",         default="",     help="Optional path to write JSON result")
    args = ap.parse_args()

    api_key = args.api_key or os.environ.get("GEMINI_API_KEY", "")
    if not api_key:
        print("ERROR: Gemini API key required (--api-key or GEMINI_API_KEY env)", file=sys.stderr)
        sys.exit(1)

    if not os.path.isfile(args.image):
        print(f"ERROR: image not found: {args.image}", file=sys.stderr)
        sys.exit(1)

    # Validate params JSON
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

    print(f"[eval_gemini] Calling {args.model} for {args.vfx} ({args.material})...",
          file=sys.stderr)

    image_b64 = load_image_b64(args.image)
    result = call_gemini(api_key, args.model, image_b64, prompt)

    # Attach metadata
    result["_meta"] = {
        "vfx": args.vfx,
        "material": args.material,
        "image": args.image,
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
