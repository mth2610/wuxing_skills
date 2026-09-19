#!/usr/bin/env python3
"""Validate the canonical inventory for assets/textures/vfx without Pillow."""

import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSET_ROOT = ROOT / "assets" / "textures" / "vfx"
CATALOG_PATH = ASSET_ROOT / "catalog.json"
SURFACE_MANIFEST = ROOT / "assets" / "vfx_surface_profiles.json"

REQUIRED = {
    "path", "source", "dimensions", "grid", "frames", "channels",
    "role", "status", "rgba8_bytes",
}
STATUSES = {"registry_preview", "primary_candidate", "support", "debug"}


def png_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError("not a PNG with a valid IHDR")
    return struct.unpack(">II", header[16:24])


def main() -> int:
    failures: list[str] = []
    if not CATALOG_PATH.is_file():
        print(f"FAIL: missing {CATALOG_PATH.relative_to(ROOT)}")
        return 1

    catalog = json.loads(CATALOG_PATH.read_text(encoding="utf-8"))
    if catalog.get("schema_version") != 1:
        failures.append("schema_version must be 1")
    entries = catalog.get("assets", [])
    declared = {entry.get("path") for entry in entries}
    actual = {
        path.relative_to(ASSET_ROOT).as_posix()
        for path in ASSET_ROOT.rglob("*.png")
    }
    if len(entries) != len(declared):
        failures.append("catalog contains duplicate paths")
    for path in sorted(actual - declared):
        failures.append(f"uncataloged PNG: {path}")
    for path in sorted(declared - actual):
        failures.append(f"catalog path does not exist: {path}")

    surface_text = SURFACE_MANIFEST.read_text(encoding="utf-8")
    total_rgba8 = 0
    for entry in entries:
        path_text = entry.get("path", "<missing>")
        missing = REQUIRED - entry.keys()
        if missing:
            failures.append(f"{path_text}: missing fields {sorted(missing)}")
            continue
        if entry["status"] not in STATUSES:
            failures.append(f"{path_text}: invalid status {entry['status']}")
        path = ASSET_ROOT / path_text
        if not path.is_file():
            continue
        try:
            width, height = png_dimensions(path)
        except ValueError as error:
            failures.append(f"{path_text}: {error}")
            continue
        if entry["dimensions"] != [width, height]:
            failures.append(
                f"{path_text}: dimensions {entry['dimensions']} != PNG {width}x{height}"
            )
        columns, rows = entry["grid"]
        if columns <= 0 or rows <= 0 or width % columns or height % rows:
            failures.append(f"{path_text}: grid {columns}x{rows} does not divide PNG")
        if not 0 < entry["frames"] <= columns * rows:
            failures.append(f"{path_text}: frames exceed grid capacity")
        expected_bytes = width * height * 4
        if entry["rgba8_bytes"] != expected_bytes:
            failures.append(
                f"{path_text}: rgba8_bytes {entry['rgba8_bytes']} != {expected_bytes}"
            )
        total_rgba8 += expected_bytes
        if not entry["source"].strip() or not entry["channels"].strip():
            failures.append(f"{path_text}: source/channels must be explicit")
        if entry["status"] == "registry_preview":
            manifest_path = f"assets/textures/vfx/{path_text}"
            if manifest_path not in surface_text:
                failures.append(f"{path_text}: registry_preview asset is absent from surface manifest")

    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1

    mib = total_rgba8 / (1024 * 1024)
    print(f"Extracted VFX catalog: {len(entries)} assets, {mib:.1f} MiB RGBA8 validated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
