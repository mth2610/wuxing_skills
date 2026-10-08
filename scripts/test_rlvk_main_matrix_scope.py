#!/usr/bin/env python3
"""Guard the game's custom 3D scope against rlgl's shared-stack ordering trap."""

from pathlib import Path
import re


def scope_body(source: str, name: str) -> str:
    start = source.index("{", source.index(f"static void {name}("))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source[start + 1:end - 1], flags=re.S)


def main() -> None:
    source = (Path(__file__).resolve().parents[1] / "main.c").read_text()
    calls = re.compile(r"rlMatrixMode\((RL_\w+)\)|rl(Push|Pop)Matrix\(\)")
    stack = []
    mode = "RL_MODELVIEW"
    pushes = []
    for name in ("MyBeginMode3D", "MyEndMode3D"):
        for match in calls.finditer(scope_body(source, name)):
            if match[1]:
                mode = match[1]
            elif match[2] == "Push":
                stack.append(mode)
                pushes.append(mode)
            else:
                assert stack, f"{name}: matrix stack underflow"
                saved_mode = stack.pop()
                assert saved_mode == mode, f"{name}: {mode} pop restores {saved_mode} matrix"
    assert pushes == ["RL_PROJECTION", "RL_MODELVIEW"], pushes
    assert not stack, f"unbalanced custom 3D scope: {stack}"
    assert mode == "RL_MODELVIEW", "custom 3D scope must return with modelview active"
    print("PASS main custom 3D matrix scope restores matching stack entries")


if __name__ == "__main__":
    main()
