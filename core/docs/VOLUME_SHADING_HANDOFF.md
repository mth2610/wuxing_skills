# Volume tube shading — RESOLVED. Handoff, 06/08/2026

**Read this before touching `core/trails/shaders/trail_volume.fs`, its space
handling, or `PMTube` normals.**

## RESOLVED — the session's root cause was WRONG; the fix is in and verified

The last round ended on the conclusion **"`rlNormal3f` does not deliver
per-vertex normals through rlgl's immediate-mode batch." It does.** The rlvk
`imm_normal` scenario (`third_party/vulkan/tests/rlvk_visual_test.c`) sends a
KNOWN normal down this exact draw path and reads it back numerically:

```
Nworld = (0.30, 0.90, -0.32)
raw vertexNormal      -> (0.43, 0.85, 0.30) = view * N        (d 0.002)
matModel*vertexNormal -> (0.18, 0.56, 0.80) = view * view * N (d 0.005)
```

So the attribute **arrives**, correctly — and the old shader used it
**twice** view-rotated. `main.c`'s `MyBeginMode3D` calls `rlPushMatrix()` in
RL_MODELVIEW, which arms rlgl's `transformRequired` and parks the VIEW matrix
in `State.transform` (`ENGINE_LANDMINES §9`). For an immediate-mode draw
(what `PMTube_DrawFaded` uses):
- `rlVertex3f` / `rlNormal3f` transform on the CPU by `State.transform`
  (rlgl.h:1529/1612) → the attributes reaching the shader are **already VIEW
  space**;
- the batch flush then uploads `matModel = State.transform` = that same view
  matrix (rlgl.h:3082, mirrored in `rlvk_core.inl:595`);
- so `vs_header.glsl`'s `matModel * vec4(vertexNormal, 0)` applies the view
  rotation a **second** time — the whole of the `|N·V|` inversion. Mode 14
  came back white for the same reason: both `Nattr` and `Ndfdx` were doubly
  transformed by the same matrix, so they agreed while being jointly wrong.

The "many colours" probe that seemed to prove the attribute was missing
(`rlNormal3f(0,1,0)` on every vertex, read back via `volume_debug = 5`) was
itself a victim of §5 trap 3: that debug view sits ABOVE the `discard`,
compositing both tube walls with opaque alpha and no depth sort, so a pixel's
colour is raster-order — camera-angle — dependent. A constant normal still
reads back "many colours".

### The fix — APPLIED (core-side, 06/08/2026)

`trail_volume.vs` no longer calls `VS_FinalOutput`: it passes the attributes
through untouched (they are already view space) and only `gl_Position` goes
through `mvp`. `trail_volume.fs` takes the view vector as
`normalize(-fragPosition)` (in view space the camera IS the origin — no
uniform can fail to arrive). `u_volViewSrc` / `vol_view_src` are gone; a
switch over a settled question rots.

**Guards:** `core/tests/volume_space_contract_test.c` (headless, pins the
contract in source — `VS_FinalOutput` must never be "restored" on this draw
path) + the rlvk `imm_normal` scenario (the only instrument that can decide a
space question: it sends a known value and reads it back).

### Follow-up state

- The beam stays **ONE PLAIN CYLINDER** (`beam_probe = 1`, the shipped
  default). The layered version (steps 1–2 of the P4 spec, intact below the
  switch) can be turned back on with `beam_probe = 0` — the `|N·V|` it layers
  on is now meaningful.
- `vol_depth_mode` / `vol_rim` (§7) were tuned against a broken `|N·V|`; that
  decision should be re-judged.
- §7's separate, independently-measured defect — the shipping `(1-|N·V|)²`
  term peaks at `b/R = 0.960`, i.e. it is a RIM term, not a volume term — is
  independent of this and still open.

Everything below (§1–§8) is the historical trail. §5 is the nine instrument
traps — read it before writing any debug view for this module.

---

## 1. The symptom (historical — superseded by RESOLVED above)

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

**RESOLVED: it was NONE of (a)/(b)/(c) — see the RESOLVED section at the top.**
The constant-normal probe (emit `rlNormal3f(0,1,0)` everywhere, read back with
`volume_debug = 5`) returned many colours — but that was §5 trap 3 (the debug
view composites both walls above the discard), NOT evidence that the attribute
was missing. The `imm_normal` scenario later proved the attribute arrives fine;
the real defect was the double view-transform (`matModel * N` on a view-space
attribute). The probe has since been removed from the source; it was
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
vol_cull        = 0   # draw both walls; the smoke materials are authored for this mode
vol_normal_src  = 0   # 1 = normal from dFdx instead of the attribute
vol_depth_mode  = 0   # gain on the |N.V|^p BODY term
vol_rim         = 1   # gain on the (1-|N.V|)^p RIM term
vol_depth_pow   = 2.0
vol_density     = 1.75
vol_erode       = 0     # noise erosion of the silhouette; 0 = off (soft rim)
vol_erode_band  = 0.2   # width of the torn band, inward, in b/R units
volume_debug    = 0
```

`vol_view_src` is GONE (06/08/2026): the view vector is settled at
`normalize(-fragPosition)` — a switch over a settled question rots
(`third_party/vulkan/CLAUDE.md` methodology rule 3).

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
`PMTube_DebugSetConstantNormal` (the constant-normal probe — its "many
colours" result was a §5 trap 3 artefact, and the attribute was later proven
fine by `imm_normal`). The `volume_debug` modes and the remaining `vol_*`
tunables all persist; they are inert at their defaults.

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
  `facing`/`d` split, `u_volCull`/`u_volNormalSrc`, corrected space comment,
  `V = normalize(-fragPosition)` (06/08, the fix); `u_volViewSrc` removed
- `core/trails/shaders/trail_volume.vs` — rewritten 06/08/2026: passes the
  attributes through untouched (they are view space on this draw path), only
  `gl_Position` goes through `mvp`; full reasoning in its header
- `core/trails/trail_system.c` — `viewPos` still set from the camera (mode 15
  reads it back); the remaining tunables; `TUBE_NDOTV_CPU`; spawn log reads
  values back
- `core/composition/common/vc_beam.inl` — `beam_probe`
- `third_party/vulkan/tests/rlvk_visual_test.c` — `imm_normal` scenario (the
  instrument that decided the question)
- `core/tests/volume_space_contract_test.c` — pins the space contract in
  source (06/08/2026, new)
- `core/tests/beam_geometry_test.c` — asserts pinning every trap in §5 that can
  be pinned from source
- `core/tests/silhouette_test.c` — `EDGE_RIM`, two-sided comparison, softness
  sweep, `Test_WhereTheOpacityActuallyPeaks`
- `core/docs/LANDMINES.md` — the unbounded-branch and sentinel entries
