#!/usr/bin/env python3
"""Scaffold a new Wuxing skill from templates.

Usage:
    python3 scripts/new_skill.py <element> <snake_name> --archetype <type> [--shader]

    element   : water | wood | fire | earth | metal | taiji
    snake_name: snake_case base name (without _skill suffix), e.g. fire_lance
    archetype : projectile | ground | path | attached
    --shader  : also emit a minimal .vs/.fs pair

Generates:
    skills/<element>/<snake_name>_skill/
        <snake_name>_skill.h
        <snake_name>_skill.c
        <snake_name>_skill_params.inl
        <snake_name>_skill_tunables.inl
        [<snake_name>.vs]  (with --shader)
        [<snake_name>.fs]  (with --shader)

Then runs scripts/generate_registry.py and prints next steps.
Refuses to overwrite an existing skill directory.
"""

import argparse
import os
import re
import subprocess
import sys

VALID_ELEMENTS = {"water", "wood", "fire", "earth", "metal", "taiji"}
VALID_ARCHETYPES = {"projectile", "ground", "path", "attached"}

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
TEMPLATES_DIR = os.path.join(SCRIPT_DIR, "templates")


def snake_to_camel(snake: str) -> str:
    return "".join(part.capitalize() for part in snake.split("_"))


def render(template_path: str, subs: dict) -> str:
    with open(template_path, "r") as f:
        text = f.read()
    for key, value in subs.items():
        text = text.replace(key, value)
    return text


def write_file(path: str, content: str) -> None:
    with open(path, "w") as f:
        f.write(content)
    print(f"  created  {os.path.relpath(path, REPO_ROOT)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("element", help="Element: water | wood | fire | earth | metal | taiji")
    parser.add_argument("snake_name", help="Snake-case base name, e.g. fire_lance (no _skill suffix)")
    parser.add_argument("--archetype", required=True, choices=sorted(VALID_ARCHETYPES),
                        help="Skill archetype")
    parser.add_argument("--shader", action="store_true", help="Also emit a minimal .vs/.fs pair")
    args = parser.parse_args()

    element = args.element.lower()
    snake = args.snake_name.lower()

    if element not in VALID_ELEMENTS:
        print(f"ERROR: element must be one of {sorted(VALID_ELEMENTS)}, got '{element}'", file=sys.stderr)
        return 1

    if not re.fullmatch(r"[a-z][a-z0-9_]*", snake):
        print(f"ERROR: snake_name must be lowercase letters/digits/underscores, got '{snake}'", file=sys.stderr)
        return 1

    if snake.endswith("_skill"):
        print("NOTE: snake_name already ends in '_skill' — stripping suffix (the script adds it automatically).")
        snake = snake[:-len("_skill")]

    camel = snake_to_camel(snake)       # e.g. FireLance
    screaming = snake.upper()           # e.g. FIRE_LANCE

    skill_dir_name = f"{snake}_skill"
    skill_dir = os.path.join(REPO_ROOT, "skills", element, skill_dir_name)

    if os.path.exists(skill_dir):
        print(f"ERROR: '{os.path.relpath(skill_dir, REPO_ROOT)}' already exists — refusing to overwrite.", file=sys.stderr)
        return 1

    os.makedirs(skill_dir)
    print(f"\nScaffolding skills/{element}/{skill_dir_name}/")

    subs = {
        "{{Name}}": camel,
        "{{name}}": snake,
        "{{NAME}}": screaming,
        "{{element}}": element,
    }

    # Header (same for all archetypes)
    h_src = os.path.join(TEMPLATES_DIR, "skill.h")
    write_file(os.path.join(skill_dir, f"{snake}_skill.h"), render(h_src, subs))

    # .c body — archetype-specific
    c_template_map = {
        "projectile": "skill_projectile.c",
        "ground":     "skill_ground.c",
        "path":       "skill_path.c",
        "attached":   "skill_attached.c",
    }
    c_src = os.path.join(TEMPLATES_DIR, c_template_map[args.archetype])
    write_file(os.path.join(skill_dir, f"{snake}_skill.c"), render(c_src, subs))

    # .inl files
    params_src = os.path.join(TEMPLATES_DIR, "skill_params.inl")
    write_file(os.path.join(skill_dir, f"{snake}_skill_params.inl"), render(params_src, subs))

    tunables_src = os.path.join(TEMPLATES_DIR, "skill_tunables.inl")
    write_file(os.path.join(skill_dir, f"{snake}_skill_tunables.inl"), render(tunables_src, subs))

    # Optional shaders
    if args.shader:
        vs_src = os.path.join(TEMPLATES_DIR, "skill.vs")
        fs_src = os.path.join(TEMPLATES_DIR, "skill.fs")
        write_file(os.path.join(skill_dir, f"{snake}.vs"), render(vs_src, subs))
        write_file(os.path.join(skill_dir, f"{snake}.fs"), render(fs_src, subs))

    # Run registry generator
    gen_script = os.path.join(SCRIPT_DIR, "generate_registry.py")
    print("\nRunning generate_registry.py …")
    result = subprocess.run([sys.executable, gen_script], cwd=REPO_ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        print("  generate_registry.py FAILED:")
        print(result.stderr or result.stdout)
        return 1

    # Try to find the registered skill index for the user
    reg_header = os.path.join(REPO_ROOT, "core", "skills_generated.h")
    skill_index = None
    if os.path.exists(reg_header):
        with open(reg_header) as f:
            for line in f:
                m = re.search(rf"SKILL_({screaming})\s+(\d+)", line, re.IGNORECASE)
                if not m:
                    m = re.search(rf"\b(\d+)\b.*{snake}", line)
                if m:
                    skill_index = m.group(2) if m.lastindex >= 2 else None
                    break

    print()
    print("=" * 60)
    print(f"  Skill created: {camel} ({element} / {args.archetype})")
    if skill_index:
        print(f"  Skill index  : {skill_index}")
    print()
    print("  Next steps:")
    print(f"    1. make && ./wuxing")
    print(f"    2. In sandbox: press K to cycle map, then cast the skill.")
    if skill_index:
        print(f"       (skill index {skill_index} — use CastSkill({skill_index}, ...) in sandbox)")
    print(f"    3. Edit skills/{element}/{skill_dir_name}/{snake}_skill.c")
    print(f"       to implement the real VFX/logic.")
    print("=" * 60)
    return 0


if __name__ == "__main__":
    sys.exit(main())
