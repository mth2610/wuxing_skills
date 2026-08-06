# Volume tube shading — ROOT CAUSE FOUND. Handoff, 06/08/2026

**Read this before touching `core/trails/shaders/trail_volume.fs`, its space
handling, or `PMTube` normals.**

## ROOT CAUSE (found on the last round, 06/08/2026)

**`rlNormal3f` does not deliver per-vertex normals through rlgl's
immediate-mode batch.** `PMTube_DrawFaded` was made to emit
`rlNormal3f(0, 1, 0)` for *every* vertex — a known constant. The shader should
then paint one flat colour under `volume_debug = 5`. It painted **many
colours**. What arrives in `fragNormal` is therefore **not what the draw code
sends**.

That single fact explains the whole investigation: `|N·V|` is computed from a
`fragNormal` that has no relationship to the surface, so it comes out
inverted, changes with camera angle, and cannot be fixed by any amount of
tuning, space-handling or term reshaping.

It also reconciles the contradiction that stalled §4: the CPU sweep was right
(the mesh IS correct), and mode 14 was white **not** because the attribute was
correct but because `Ndfdx` is derived from `fragPosition` and both were being
compared inside the same wrong frame — mode 14 could never tell "correct" from
"consistently wrong", which §4 candidate (a) predicted.

### The fix, not yet applied

Volume shading must stop reading `fragNormal` on this draw path. Two options:

1. **`dFdx/dFdy` of `fragPosition`** — already wired as `vol_normal_src = 1`.
   `core/tests/silhouette_test.c`'s `Test_FacetNormalsAreEnough` measured flat
   normals as sufficient (edge hardness under the limit at >= 24 radial). But
   note `TRAIL_TUBE_RADIAL_MAX = 16` currently clamps below that, and flat
   normals give 16 discrete values on a 16-sided tube.
2. **Stop using immediate mode for the tube** — build a real `Mesh` with a
   normal buffer and `DrawMesh`. Larger change; also changes the `matModel`
   convention (see §3), so it must be done together with the space handling.

### Where the beam was left, and why

`VFX_ComposeBeam` now draws as **ONE PLAIN CYLINDER** — `beam_probe = 1` is
the shipped default (`tuning.cfg` and `vc_beam.inl`). No taper, no deform, no
additive core, no sheet, no UV scroll.

That is deliberate and it is where the next session should start. Everything
the beam would otherwise layer on — the taper's shading, the churn's
silhouette, the rim term — is a function of `|N·V|`, which is currently
meaningless on this draw path. Tuning any of it now would bake the noise into
the numbers, and those numbers would then have to be thrown away twice: once
when the normals are fixed, once when someone notices they were fitted to
garbage.

The layered version (steps 1–2 of the P4 spec) is written and intact below the
switch. Turn it back on with `beam_probe = 0` **after** the normal path is
fixed and `volume_debug = 13` shows the `b/R` contour landing on the real
silhouette rather than down the middle of the tube.

**Suggested order for the next session:**
1. Fix the normal path (option 1 or 2 above) on the plain cylinder.
2. Verify with `volume_debug = 13` + `14` — contour on the silhouette, and a
   constant-normal probe reading back constant.
3. Only then revisit `vol_depth_mode` / `vol_rim` (§7) — that decision was
   made against a broken `|N·V|` and is not trustworthy.
4. Only then `beam_probe = 0` and judge the layered beam.

Everything else in this file is the ruled-out ground and, in §5, the nine
instrument traps that cost most of the session. Read §5 before writing any
debug view for this module.

---

## 1. The symptom

On a **plain, stationary, untextured, undeformed cylinder** (the `beam_probe`
fixture, §4), the shader's `d = |N·V|` is **inverted**:

- at the **centre of the silhouette** — where the surface faces the camera and
  `|N·V|` must be ≈ **1** — it measures ≈ **0**
- at the **rim** — where the surface grazes and `|N·V|` must be ≈ **0** — it
  measures ≈ **1**

Visible as: `volume_debug = 13` paints the `b/R = 1.0` contour (the projected
silhouette) as a stripe **down the middle of the tube** instead of along its
outline.

Downstream this explains every user-visible complaint in the session — "khói
mờ và loãng", "khói dồn về hai bên, giữa rỗng", "chỉ thấy nửa vỏ ở một số
góc", "biên sai vị trí" — because every one of them is `edge = f(|N·V|)`.

---

## 2. RULED OUT — with the evidence. Do not re-test these.

| Ruled out | How | Result |
|---|---|---|
| Mesh geometry & normals | `TUBE_NDOTV_CPU` log — computes `\|N·V\|` on the **CPU** straight off the built mesh + the real camera, never touches the shader | `min=0.049 max=0.990` — the full sweep. Mesh and normals are **correct** |
| Normal attribute not arriving | `volume_debug = 14`: `\|dot(Nattr, Ndfdx)\|` | **WHITE** — the attribute normal matches the geometric normal from `dFdx`. Attribute arrives and is correct |
| `viewPos` uniform not arriving | `volume_debug = 15` (`\|viewPos\|` as bands) + spawn log prints its location | Location valid; magnitude **changes as the camera orbits** → it arrives and is a real **world** coordinate |
| Debug path itself broken | `volume_debug = 8` (grey) / `9` (red) | Both arrive as written |
| Non-uniform scale in `matModel` | implied by mode 14 being white | A non-uniform scale would make `Nattr` diverge from `Ndfdx` (normals need inverse-transpose). They agree |
| Camera position wrong in the draw call | `TUBE_NDOTV_CPU` prints `cam=(9.4,4.8,9.4)` and `ring0=(5.3,1.2,4.7)`, and CPU `\|N·V\|` from those is correct | Camera passed to the tube draw is the real one |

## 3. ESTABLISHED — new facts, measured this session

1. **`fragPosition` is VIEW SPACE**, for immediate-mode draws too.
   `volume_debug = 16` bands `|fragPosition|`: it **changes as the camera
   orbits**, and a long tube shows **two bands at once** with the boundary
   cutting across it (the `|P| = 10 m` plane). A world position does not move
   when the camera does.
   **This contradicts the postscript that used to be in `trail_volume.fs`**
   (which claimed immediate-mode gives world space, citing
   `sandbox/fresnel_probe.c`, 04/08). That comment has been corrected in place.
   `ENGINE_LANDMINES §9` was right all along: `matModel = model × view` for
   every draw inside the 3D pass.

2. **`viewPos` is a WORLD coordinate and does arrive.** So
   `V = viewPos - fragPosition` **mixes world with view** and is meaningless.
   The correct view vector for this path is `normalize(-fragPosition)`
   (`vol_view_src = 1`), because in view space the camera is at the origin.

3. **The shipping thickness term is a RIM term, not a volume term.** Measured
   in `core/tests/silhouette_test.c` (`Test_WhereTheOpacityActuallyPeaks`):
   `(1-|N·V|)²` peaks at `b/R = 0.960` with **exactly 0.000 at the axis**;
   `|N·V|²` peaks **on the axis**. This is a separate, independently-confirmed
   defect — see §7.

## 4. THE CONTRADICTION — RESOLVED, kept for the reasoning trail

With **all** of the above holding:

- `N` is correct (mode 14 white, CPU sweep correct)
- `fragPosition` is view space (mode 16)
- `V = normalize(-fragPosition)` is therefore the correct view vector
- ⇒ `d = |dot(N, V)|` **must** be ≈ 1 at the silhouette centre

…and yet with `vol_view_src = 1` the measurement is **still inverted**.

Three candidates, none tested:

**(a) `fragNormal` and `fragPosition` are in DIFFERENT spaces.** Both go
through `matModel` in `vs_header.glsl:34-35`, but if `matModel` is re-uploaded
between the position write and the normal write (rlvk UBO arena,
`ENGINE_LANDMINES §8`), they could be transformed by two different matrices.
Mode 14 would **not** catch this: `Ndfdx` is derived from `fragPosition`, so a
normal in the wrong space would still agree with itself. **Test:** compare
`Nattr` against a normal reconstructed from something independent of
`fragPosition` — e.g. `fragTexCoord.x` (the around-the-tube coordinate) which
gives the expected radial direction analytically.

**(b) The interpolated `fragNormal` is a TANGENT.** The inversion is exactly
what a circumferential tangent produces (⊥ to V at the centre, ∥ at the rim).
Mode 14 white argues against it, but see (a) — mode 14 cannot distinguish
"correct" from "consistently wrong". **Test:** in `PMTube_DrawFaded`, emit a
known constant normal (e.g. `rlNormal3f(0,1,0)`) for every vertex and check
whether the shader sees `(0,1,0)` in `volume_debug = 5`. If it sees something
else, the attribute is being routed to the wrong slot.

**(c) The debug views themselves are still lying.** Four separate instrument
defects were found this session (§5). Assume nothing that has not been
re-verified since the last of them was fixed.

**RESOLVED: it was (b), and (a) explains why mode 14 did not catch it.**
The constant-normal probe (emit `rlNormal3f(0,1,0)` everywhere, read back with
`volume_debug = 5`) returned many colours instead of one — see the ROOT CAUSE
section at the top. The probe has since been removed from the source; it was
`PMTube_DebugSetConstantNormal` in `pm_tube.inl`, trivially re-addable from
this description if it is ever needed again.

The general lesson worth carrying: **the only conclusive test of a data path
is to send a KNOWN value and read it back.** Every earlier round compared two
quantities that were *both* under suspicion, so none of them could conclude.

---

## 5. THE INSTRUMENT TRAPS — the expensive part. Read before writing any debug view.

Every one of these produced a *plausible, stable, completely wrong* image, and
each cost 1–3 rounds:

1. **An unbounded last branch swallows every mode added after it.**
   `if (u_volDebug > 8.5)` (constant red) had no upper bound — correct when it
   was last, fatal once modes 10/11 were added below. `volume_debug = 10`
   rendered **constant red**, and the *invariance* of that red across every
   toggle was misread as "a deep systemic bug" when it meant "the measurement
   is not running". **A constant cannot be told apart from a real quantity
   that happens to equal that constant.**

2. **The same shape again, one level up.** The enclosing block
   `if (u_volDebug > 0.5)` ends in a catch-all fallback (`q = ... : fade;
   return;`), so it swallowed modes 11/12 which live *below* the `discard`.
   Fixed by bounding the block too. **Rule: any "catch the rest" construct —
   open branch, open block, trailing `else`, `default:` — is correct when
   written and wrong forever after.** Both are now pinned by asserts in
   `core/tests/beam_geometry_test.c`.

3. **All debug views sit ABOVE the `discard`, so they composite BOTH tube
   walls** with opaque alpha and no depth sort. Which wall wins a pixel depends
   on raster order, i.e. **on camera angle**. This produced "scales", "discs",
   "spikes" and "colours change randomly when I rotate" — all artefacts, none
   real. Modes 11/12/13 were added below the discard for this reason.
   Corollary: **`vol_cull` does not affect any above-discard debug view**, so
   toggling it to "fix" one of them is meaningless.

4. **Forcing a normal's sign disables the cull.** The `dFdx` branch had
   `if (dot(N,V) < 0.0) N = -N;`, and `facing` was computed from that same `N`
   — so `facing >= 0` always and `discard` never ran. The fix that was supposed
   to make mode 12 single-walled silently made it double-walled. `facing`
   (cull) and `d` (shading) are now **two separate vectors**; cull always reads
   the attribute normal, whose outward sign `pm_tube.inl` enforces.

5. **A thin contour on a faceted tube blinks in and out.** With flat (`dFdx`)
   normals a 16-sided tube yields **16 discrete** `|N·V|` values; a narrow band
   only lights up when one happens to fall inside it, so it appears and
   disappears as the camera moves. Mode 13 now always uses the **interpolated**
   normal and draws a whole contour **scale**, not a single line.

6. **A probe thinner than its own sampling footprint measures the sampler.**
   At 0.10 m over 4.6 m the tube is ~10 px wide — under a pixel per face at 16
   radial — and `dFdx` reads a 2×2 pixel quad, so the derivative straddles
   different triangles. `beam_probe` now fattens the tube ×6.

7. **A log that recomputes its own inputs cannot report a clamp.** The spawn
   line printed `probe ? 32 : 16` while `trail_system.c` clamps to
   `TRAIL_TUBE_RADIAL_MAX = 16`; only `TUBE_CHURN_DEV_DEBUG`'s `n = 400`
   (25 × 16) was honest. Logs now read values **back off the `TrailEntity`**.

8. **`alphaMul = 0` means FULL, not off.** `trail_system.c` does
   `(ly->alphaMul > 0.0f) ? ly->alphaMul : 1.0f` in two places, so every "hide"
   written as `* 0.0f` renders at full alpha. Beam uses `1e-4f`. Not fixed at
   source: `{0}`-initialised configs across the codebase depend on the
   sentinel.

9. **Toggling a knob mid-session may only half-apply.** `beam_probe` is read in
   `Beam_BuildShape`/`Beam_SpawnOne` (spawn-time only) *and* in
   `Beam_ConfigureLayers` (per frame), so flipping it live gave a *half* probe
   — deform off but taper still on, 16 radial not 32. It now respawns on
   change.

**The meta-lesson.** `core/CLAUDE.md §1`'s table says a question like "is this
formula capable of the effect" belongs in `core/tests/` at a cost of
milliseconds, and only "does it look good" needs an eyeball. This question was
arithmetic and spent 13 rounds on the GPU. When a diagnostic image does not
change across several *independent* toggles, **suspect the measurement before
the subject** — and run the constant-output modes (8/9) *first*, exactly as
that shader's own comment instructs.

---

## 6. Tools that exist now (all live, no rebuild)

`tuning.cfg`:

```
beam_probe      = 1   # DEFAULT NOW — beam is one plain cylinder; 0 = full stack
vol_cull        = 1   # 0 = draw both walls
vol_normal_src  = 0   # 1 = normal from dFdx instead of the attribute
vol_view_src    = 0   # 1 = V from -fragPosition (no uniform involved)
vol_depth_mode  = 0   # gain on the |N.V|^p BODY term
vol_rim         = 1   # gain on the (1-|N.V|)^p RIM term
vol_depth_pow   = 2.0
vol_density     = 1.75
volume_debug    = 0
```

`volume_debug` modes:

| # | paints | below discard? |
|---|---|---|
| 1–4 | `edge` / `\|N·V\|` / `pattern` / vertex fade, greyscale | no |
| 5 | `fragNormal` as colour | no |
| 6 | `V` as colour | no |
| 7 | `\|fragPosition\|/40` greyscale | no |
| 8 / 9 | constant grey / constant red — **run these first** | short-circuit |
| 10 | `\|N·V\|` as 5 discrete bands (white = NaN) | no |
| 11 | `fragNormal` as colour | **yes** |
| 12 | `\|N·V\|` bands | **yes** |
| 13 | `b/R` contour scale; white line = 0.960, red = the projected silhouette | **yes** |
| 14 | `\|dot(Nattr, Ndfdx)\|` — white = the two normal sources agree | **yes** |
| 15 / 16 | `\|viewPos\|` / `\|fragPosition\|` as magnitude bands | **yes** |

**Removed at the end of the session** (re-add from this description if needed):
`TUBE_NDOTV_CPU` (CPU-side `|N·V|` off the built mesh — this is what proved the
mesh correct), `TUBE_CHURN_DEV_DEBUG` (churn deviation stats), and
`PMTube_DebugSetConstantNormal` (the constant-normal probe that found the root
cause). The `volume_debug` modes and the `vol_*` tunables all remain; they are
inert at their defaults.

Headless: `core/tests/silhouette_test.c` is a software rasteriser that models
both terms (`EDGE_RIM`, `EDGE_NDOTV`), culling, power sweeps and the
silhouette-softness factor. It needs no GPU and cannot be corrupted by any trap
in §5. **Prefer it.**

---

## 7. Separate, already-established, and NOT blocked by the above

The shipping thickness term `(1-|N·V|)²` is a **rim** term: opacity `0.000` at
the axis, peak at `b/R = 0.960`. That is a real defect independent of the
inversion, measured on CPU. `vol_depth_mode = 1 / vol_rim = 0 /
vol_density = 0.35` switches to the optical-depth form and the owner confirmed
it looks substantially better ("chất lượng cải thiện rất nhiều, nhất là
beam"), **but** the smoke column shares this shader and has not been reviewed
at that setting — changing the default once caused a visible regression there
already. Defaults are still the old values; the switch is opt-in.

Also open, in `docs/PROGRESS.md`: "Khói volume: mờ và loãng" (one shell cannot
make a volume; nested shells are the untried direction) and the two
long-standing trail defects (aliasing, unsorted alpha layers).

---

## 8. Files touched by this investigation

- `core/trails/shaders/trail_volume.fs` — debug modes 10–16, bounded branches,
  `facing`/`d` split, `u_volCull`/`u_volNormalSrc`/`u_volViewSrc`, corrected
  space comment
- `core/trails/trail_system.c` — `viewPos` now set from the camera; the
  tunables above; `TUBE_NDOTV_CPU`; spawn log reads values back
- `core/composition/common/vc_beam.inl` — `beam_probe`
- `core/tests/beam_geometry_test.c` — asserts pinning every trap in §5 that can
  be pinned from source
- `core/tests/silhouette_test.c` — `EDGE_RIM`, two-sided comparison, softness
  sweep, `Test_WhereTheOpacityActuallyPeaks`
- `core/docs/LANDMINES.md` — the unbounded-branch and sentinel entries
