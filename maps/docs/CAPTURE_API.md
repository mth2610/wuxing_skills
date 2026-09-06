# Map capture interface

Implemented entry point: `main.c`, extending the existing `--render-vfx` path.

```sh
env WUXING_MAP=verdant_path ./build/wuxing \
  --render-neutral-smoke --origin 27,0,20 --eye 30,5,25 \
  --warmup 90 --out /tmp/meadow.png
```

- `--origin x,y,z`: effect world position in metres. Defaults to the historical arena position `(6,0,4.4)`.
- `--render-neutral-smoke`: dedicated smoke capture without elemental tint (`sandbox/vfx_test.c`). It retains the smoke asset's authored shading; it is not a white calibration swatch. `--render-vfx 36` remains the fire-tinted catalog fixture.
- `--eye x,y,z`: explicit capture camera position. The target is origin plus `(0,0.2,0)`. Without this flag the existing orbit camera remains active.
- Coordinates must be finite. Explicit views must be nonvertical and at least one metre from the target; invalid capture settings return exit code 2 before window creation.
- Capture mode pins the effect mouse target to origin and disables camera shake. Coordinates are not automatically projected to the terrain; authors must choose the intended height.
- Failed image export returns a nonzero exit code.
- `CAPTURE:` log entries identify requested map, origin, camera/target, tuning path, warmup, actual sun/ambient values and real-shadow enable state. They do not prove shadow receiver correctness or full resolved post-process state.

`scripts/capture_meadow_fixtures.sh` captures four Verdant views and saves executable/tuning hashes, source revision/diff, worktree status and runtime logs next to the PNGs. It inherits the caller's tuning settings. The default output is a unique temporary directory; an explicit output directory allows deliberate replacement of identically named captures.

```sh
bash scripts/capture_meadow_fixtures.sh
bash scripts/capture_meadow_fixtures.sh /tmp/meadow-review ./build/wuxing
```

> **Project convention:** The four positions are authored reference views, not automatic map framing. The dedicated neutral-smoke fixture avoids dependence on generated catalog indices. Screenshots require visual inspection; successful export does not assert acceptable lighting, visible smoke or AAA quality.

## Patch Log

| Date | Editor | Section | Source | Tier |
| --- | --- | --- | --- | --- |
| 2026-09-06 | Codex | CLI behavior | `main.c` | Ground-truth |
| 2026-09-06 | Codex | Batch capture | `scripts/capture_meadow_fixtures.sh` | Ground-truth |
| 2026-09-06 | Codex | Fixture selection | Authored Phase-0 decision | Project convention |
