# PROGRESS — VFX contrast và strand trail

**Cập nhật:** 10/08/2026
**Trạng thái:** **ĐÃ SỬA** — nguyên nhân gốc đã tìm ra và xác nhận bằng ảnh render.
Nguyên nhân **không nằm ở rlvk**, không nằm ở shader, và không nằm ở màu.

## 2026-08-18b — ShieldShell: the contact band's colour rings

Owner confirmed the contact rim is now on both faces, and reported the remaining
defect: the bright bands read as concentric RINGS of different colour rather than one
soft gradient. Fixed; gates green (core 69/74 — same five baseline failures — rlvk
27/27), and the matrix is byte-identical to the previous run, as expected: the harness
skips the map, so the contact term is inert in all five of its backgrounds.

**Cause, measured on a scanline through the ground line.** Two things that only bite
together:

1. The brightest channel is PINNED. `R` sat at 1.0 for 13 consecutive pixels across the
   band, so there is no luminance gradient left there — hue is the only quantity that
   can still vary.
2. §12.1's hue-preserving highlight restoration blends per-channel ACES against a
   hue-keeping curve with a weight that RISES AND FALLS along an intensity ramp
   (`smoothstep(1,2,peak) * (1 - smoothstep(5,9,peak))`). With the top channel pinned,
   the others go down and back up: `G = 185 → 170 → 231`. A non-monotone channel on a
   monotone ramp is a ring.

Confirmed with the knob (after finding it was unreachable — see below): at
`postfx_hue_restore = 0` G rises monotonically 197 → 255 and the band is clean; at 0.5
it dips; at 1.0 it dips harder. The tone map is doing exactly what §12.1 specifies.

**Fix, at the plateau rather than at the tone map.** `depthContact` returned
`1.0 - smoothstep(0, thickness, gap)`, and smoothstep has ZERO DERIVATIVE at its lower
edge — the profile holds ~1.0 for the first ~15% of its width. A cubic
`(1 - gap/thickness)^3` falls away immediately. Flat top 13 px → 5 px, clipped pixels
10924 → 7029, dip gone. Turning hue restoration down would also have removed the rings —
for every effect in the game, to pay for one effect's authoring mistake.

**Brightness was the wrong lever, and was measured before being discarded.** Halving
`shield_shell_rim` and `shield_shell_contact` moved the clipped area 10924 → 6940 and
left the dip exactly where it was. Only at roughly a sixth of the shipped strength did
the profile go monotone, which is not a look anyone asked for.

**Tried and REMOVED.** Unifying the two white-core ramps (`rimHot` keyed on wallDensity,
`contactHot` keyed on contact) into one `max()` hotness scalar, on the theory that two
white peaks straddling a saturated trough made the ring. It changes 4 pixels out of
921600 (Rclip 7029 → 7025). The two ramps are not the mechanism.

**Two instrument bugs found on the way, both fixed:**

* **`tuning.cfg` overrides appended at the end are dead text.** `FindKeyValue` takes the
  FIRST match, and the file already contained `postfx_hue_restore = 0.5`. The whole
  first A/B came back byte-identical at 0.0 / 0.5 / 1.0 and would have exonerated the
  tone map. Forcing the branch in the shader instead changed 11428 pixels, which is what
  exposed the no-op. Note also that this machine pins hue restore at **0.5** where §12.1
  documents a shipping default of 0.6 — every local measurement is at 0.5.
* **`render_vfx_matrix.sh`'s stale-binary guard counted `core/tests/`**, which links
  nothing from the game, so it refused to run every time a regression test was added
  beside a fix. Excluded; re-verified that it still fires on a real source change.

**Guards.** `core/tests/shield_shell_test.c` asserts the contact profile has no flat top
AND asserts the contrast against the old plateau form (0.729 vs 0.972 at one tenth of
the band), so the check is shown to discriminate; plus monotonicity across the whole
band. Two source-string checks that pinned the literal `smoothstep(0.0, u_contactThickness`
were loosened to the actual contract (`gap / u_contactThickness`) — they were pinning a
curve, which is a look, when what they mean is the normalisation.

**Not fixed, noted.** The silhouette itself goes from background to near-white in ONE
pixel (95,94,126 → 255,240,202): no outward falloff at all, because bloom contributes
essentially nothing at this threshold. `bloom_threshold = 0.9` and
`bloom_intensity = 0.35` are persisted overrides in `tuning.cfg`, so this is a global
call, not a shell one.

## 2026-08-18c — The hard silhouette edge was bloom_scatter, pinned outside its own range

Owner asked for the last open item — the silhouette going from background to near-white
in one pixel — to be fixed. It was not a shell problem.

**Measured first.** 20 rows outside the top rim read `86 86 86 85 87 ... 88 88` then
`252`: a perfectly FLAT lift, then a cliff. Bloom was demonstrably ON — sweeping
`bloom_intensity` 0 → 0.35 → 1.5 moved that floor 77 → 87 → 127 — so it was contributing
a uniform veil and no gradient at all, which is a different fault from "bloom is off".

**Cause.** The upsample chain folds each level into the one above with
`dst = mix(dst, tent(src), scatter)`. At `scatter = 1.0` that is `dst = tent(src)`: the
finer level is REPLACED at every step and the pyramid collapses to its coarsest mip, so
there is no near-field halo for any effect in the game. `BLOOM_SCATTER_DEFAULT` is 0.65,
and `tuning.cfg`'s own comment above the line reads "0 = chỉ quầng sát lõi, 1 = chỉ màn
sương rộng nhất. **Sweep 0.4 -> 0.8**". The persisted value was 1.0 — outside the range
the file itself documents. `git log -L` shows it went 0.65 → 1.0 in `b18e76d`, a commit
titled "update lut"; it reads as a change that rode along rather than a decision.

**Fix.** `tuning.cfg`: `bloom_scatter = 1.0` → `0.65`, the shipped default and what the
file held before that commit. The silhouette now ramps 87 → 203 monotonically over 20 px
into the rim. At 1:1 the halo is smooth (horizontal scan 84 → 110 → 84, no stepping); the
blockiness visible at 4x zoom is the 1/4-resolution bloom buffer and is not resolvable at
native size.

**This is a GLOBAL change — the numbers below are the cost, and it is the owner's call.**

| plate | before (scatter 1.0) | after (0.65) |
|---|---|---|
| dark cover% / chroma | 4.9 / 0.416 | 6.1 / 0.314 |
| mid cover% / darken% | 36.3 / 0.0 | 10.4 / 10.9 |
| white darken% / chroma | 88.5 / 0.341 | 81.3 / 0.322 |
| warm darken% / chroma | 87.5 / 0.291 | 65.5 / 0.325 |
| cool cover% / darken% | 19.7 / 18.6 | 12.1 / **50.8** |

Mostly favourable: the cool plate's darkening — the one real cost of the rear-emission
fix in 2026-08-18 — recovers 18.6 → 50.8, and the anomalous mid/cool footprints (36% and
20% of frame) collapse to sane 10–12%. Warm darkening drops 87.5 → 65.5.

Other fixtures, measured: FLAME VOLUME moves 80677 pixels by more than 4/255 (mean luma
70.56 → 69.79 — the wide haze tightens into a near halo; visually cleaner, not a
regression). IMPACT DUST and SMOKE COLUMN are PIXEL-IDENTICAL: nothing without
above-threshold emission is touched.

**Consequence to flag:** `BRIGHT_BACKGROUND_VFX_SPEC.md` §11b's measured baselines for
VOLUME TRAIL, FLAME VOLUME and PROJECTILE were all taken at scatter 1.0 and are now
stale. That file belongs to the renderer module, so it is flagged here rather than
edited.

**Second time this session that `tuning.cfg` was the story.** It also silently pinned
`postfx_hue_restore = 0.5` against a documented 0.6 default. Both are recorded in
`core/docs/LANDMINES.md`.

## 2026-08-18d — "Hạt hạt pixel": the final bloom composite was not part of the pyramid

Owner looked at the restored halo and reported blocky graininess along the bright edges.
Correct, and I had called it clean the turn before on a bad measurement — the scan that
"proved" smoothness ran 18 rows OUTSIDE the rim, through the far halo, while the
stair-steps were on the bright edge a few pixels away. A scanline placed where the
artifact isn't proves nothing.

**Cause.** `bloomTex` is a QUARTER-resolution target and the composite read it with a
single `texture(u_bloomTex, uv)`. Bilinear magnification at 4x reconstructs the signal as
piecewise-linear patches with a kink at every source texel boundary — a gradient
discontinuity on a 4-pixel grid, which the eye finds instantly along a high-contrast
curve. Bilinear interpolates; it does not reconstruct.

**Why it surfaced only now, and why that is not an argument to revert 2026-08-18c.** With
`bloom_scatter` pinned at 1.0 the pyramid collapsed to its coarsest mip, so `bloomTex`
held nothing but smooth low frequencies and there was no detail to alias. Restoring the
near halo put real quarter-res detail in that buffer and the reconstruction filter's
inadequacy became visible the same frame. The defect predates the scatter fix; the fix
only revealed it.

**Fix.** The composite now runs the same 3x3 tent `bloom_upsample.fs` uses, with
`u_bloomTexel` computed from the LIVE bloom target (not the window) so a resize or a
quality tier that changes bloom resolution cannot desync it. Eight extra taps in one
fullscreen pass, no new render targets.

**Energy is unchanged — the kernel is normalised.** FLAME VOLUME moves 1 pixel by more
than 4/255 (mean luma 69.79 → 69.80); IMPACT DUST and SMOKE COLUMN pixel-identical; the
SHIELD SHELL matrix moves about a point per plate (white darken 81.3 → 80.0, warm
65.5 → 64.2, cool 50.8 → 50.5). It is a reconstruction fix, not a look change — it only
does anything where bloom has a sharp, high-contrast edge, which is exactly the case
that was broken.

**Guard.** `core/tests/bloom_pyramid_contract_test.c` now REJECTS the single-fetch
composite, requires the tent, and requires the texel size to come from the live target.
Confirmed to fail on the pre-fix shader (three checks red).

Gates: core 69/74 (same five baseline failures), rlvk visual 27/27.

## 2026-08-18e — The rainbow rim is the tone map, proven on a one-hue ramp. NOT applied.

Owner asked the right question — shader, or the keep-it-vivid-on-bright-backgrounds mode?
— and named the right method: build the simplest possible gradient and check.

**The probe.** The shell's emission was replaced with a pure linear ramp of ONE colour
(`rimColor * t * 12`), body pass and far wall discarded, bloom off. No geometry, no term
stacking, no profiles — anything that survives belongs to the pipeline. Result on a
strictly RISING input: G came out `169 → 151 → 152 … 172 → 183 → 254 → 255 (frozen)`. A
reversal, then a slow plateau, then a fast sweep, then a frozen hue. Four zones from a
clean ramp. **It is the pipeline, not the shell.**

**Attribution, same probe, `postfx_hue_restore` swept:**

| hue_restore | G reversals on a rising ramp | worst drop | max dHue/step |
|---|---|---|---|
| 0.0 | 0 | 0 | 7.74 |
| 0.5 (shipping) | 1 | 18 | 7.18 |
| 1.0 | 5 | 71 | 10.32 |

**Mechanism.** Hue keeping restores chroma by LOWERING the non-peak channels. The weight
`hueRestore · smoothstep(1,2,peak) · (1 - smoothstep(5,9,peak))` rises and then falls, so
along a rising ramp those channels are pulled down and then released — a trough with two
edges, which is what a colour band is.

**And it is structural, not a tuning slip.** `rlvk_visual_test`'s `tonemap_shoulder`
requires the change to be BOUNDED: bit-identical for `peak < 1` AND for `peak > 9`. A
weight that is zero at both ends and non-zero between them cannot be monotone. Under this
blend-two-curves architecture, "bounded" and "no banding" are the same knob pointed in
opposite directions. §12.1 chose bounded, on purpose.

**Four reformulations measured, three dead:**

| formulation | reversals | worst drop | white chroma | white darken% |
|---|---|---|---|---|
| shipping | 1 | 18 | 0.320 | 80.0 |
| w=ss(1,3), desat=ss(1,6) | 1 | 3 | **0.073** | 69.9 |
| w=ss(1,4), desat=ss(5,12) | — | — | **0.107** | 69.4 |
| **H: constant w, desat=ss(5,12)** | **0** | **0** | **0.347** | 79.8 |

Anything that starts whitening near peak 1 destroys the thing §12.1 exists to protect
(white chroma 0.320 → 0.073). Only **H** — taking the intensity dependence out of the
weight entirely and moving the whitening into a monotone desaturation of the hue-kept
colour — is monotone by construction, and it is better on every other axis: max hue rate
4.75 vs 7.18, chroma UP on every plate (white 0.320 → 0.347, warm 0.349 → 0.408, cool
0.140 → 0.215), darkening unchanged, `bright_vfx` and `bright_vfx_ldr` still PASS.

**H is NOT applied.** It fails `tonemap_shoulder` — "hue restoration leaked outside its
shoulder at peak 0.2 (d 0.03033): the change is NOT bounded and needs a full whole-scene
approval" — which is the gate doing exactly its job. Every material below the shoulder
shifts by up to ~0.03 (about 8/255). The standing instruction for this work is that rlvk
stays 27/27, and a whole-scene tone-map approval is the owner's call and the renderer
module's spec, not a shield fix. The exact one-line patch is saved at `HANDOFF_TONEMAP_CANDIDATE_H.md`;
it is shader-only and hot-loads, so it can be tried and reverted without a rebuild.

Gates after restoring the shipping tone map: core 69/74 (same five baseline failures),
rlvk visual 27/27.

## 2026-08-18 — ShieldShell: the black rear face, and the depth blit under it

All three of the owner's open symptoms had ONE cause each, and two of them were the
same cause. Fixed and measured; gates green (core 69/74 — the same five baseline
failures — rlvk visual 27/27, surface-registry and sync validators clean).

**1 + 2. Black rear face, black rear contact rim.** `VFX_ShieldShell_DrawRefraction`
ran the emission scope with a SINGLE `ShieldShell_DrawPass(true)`, inheriting
`RL_CULL_FACE_BACK` from the body pass — so emission covered the near wall only. The
far wall got the body pass, which only takes light out, and no radiance at all. Its
ground line was worse than dark: `contact` still raised that wall's alpha, while the
matching `glow += contactColor * contact` never reached the framebuffer. The emission
pass now composites both interfaces exactly like the body pass, declaring `u_wallPass`
for each.

**Where the earlier pass-level bisection pointed, and why it was not the whole story.**
The recorded measurement (emission raises luma +9 and drops chroma 8.5 inside the
shell) is correct and reproduced exactly — but it was taken on a FLAT background with
the map skipped, where there is no floor, no contact term, and no occluded rear wall.
The mask floor it implicated is real; it is not what makes the rear face black. The
matrix harness cannot see symptoms 1–3 at all: `WUXING_VFX_BG` skips the map, so the
ground line the owner is describing does not exist in any of its five backgrounds. A
scene render (`--render-vfx 21` with no `WUXING_VFX_BG`) is the instrument for this
bug; the matrix is the instrument for what the fix costs elsewhere.

**3. The contact band was a flat stripe** — and, underneath that, it was in the wrong
PLACE. `depthContact()` read the soft-depth snapshot, and
`ScreenDistort_SnapshotDepth()` wrote any region smaller than the full frame to the
wrong rows of its target: a negative source height mirrors the block, so the block also
has to be placed at the mirrored destination `(H - y - h) / D`. `y / D` is right only
for a full-frame region, which was the only region ever armed until a shell bounding
box reached it. Measured: at the pixel where the depth TEST had already cut the far
wall away, the depth TEXTURE still reported 0.35–1.5 m of clearance, so no band could
exist there. With the blit fixed the band traces the whole ground ellipse, and the
band itself now uses the silhouette rim's own treatment — near-white hot core, element
hue in the wider corona (§5.4) — instead of one flat colour times a ramp.

**What the fix costs, measured.** Drawing emission on the far wall doubles the broad
angle-independent terms over every interior pixel. Rear emission weight vs. the matrix
at warmup 90:

| rear glow | darken% cool | darken% white | darken% warm | chroma white |
|---|---|---|---|---|
| 0.00 (pre-fix, rear face BLACK) | 73.5 | 89.8 | 87.6 | 0.213 |
| 0.28 | 25.8 | 89.0 | 88.1 | 0.334 |
| **0.42 (shipped)** | **18.6** | **88.5** | **87.5** | **0.341** |
| 0.68 (first attempt) | 12.1 | 87.8 | 84.2 | 0.350 |

0.42 keeps white/warm darkening at the pre-fix level and buys most of the hue
retention (white chroma 0.213 → 0.341, a §5 goal). The cost is concentrated on the
cool plate, where the pre-fix shell's interior was invisible — cover% there was 6.2%
because only the rim differed from the background at all, so its 73.5% was measured
over a rim-only footprint. One line if that trade should be re-taken:
`glow *= mix(1.0, 0.42, rearInterface)` in `glass_shell.fs`. The contact glow is added
AFTER that scale on purpose — it belongs to the surface the shell touches, not to which
wall you are looking at, and half of it lives on the far wall.

**Tried and REMOVED, so the ground is not believed covered.** A screen-space width
floor `max(u_contactThickness, fwidth(gap) * 5.0)` for the missing rear band. With the
snapshot aligned it changes 104 of 921600 pixels by more than 2/255, costs two
derivatives, and lights up the silhouette where `fwidth` explodes and a 30 m gap still
scores as "touching". A note remains at the site.

**Guards.** `core/tests/soft_depth_region_test.c` (new) composes the blit's row mapping
with the sampler's and asserts the identity for partial regions, and asserts the
pre-fix formula FAILS that round-trip. `core/tests/shield_shell_test.c` gained
emission-BLOCK-scoped checks that both interfaces draw — scoped, because the body
pass's own front/back pair satisfies an unscoped check and the guard would never fail.
Both were confirmed to fail on the pre-fix code. Landmines:
`ENGINE_LANDMINES.md` (the sub-rectangle blit, cross-cutting) and
`core/docs/LANDMINES.md` (inherited cull face).

**Still open.** The contact ring is bright enough to compete with the silhouette rim at
this fixture's scale; `shield_shell_contact` is live in `tuning.cfg` if it wants
pulling back. Symptom 3's "unnatural" may also be partly this: the ring is now correct
and prominent at the same time.

## 2026-08-16 — ShieldShell mobile path

ShieldShell now avoids scene-color/refraction reads entirely. The shell uses a
single packed surface texture (`R=hex`, `G=noise`, `B=mask`), a 14×14 sphere
(392 triangles), `Cull Back`, and a vertex-computed pow-4 Fresnel term. Depth
intersection is quality-gated and expects the existing half-resolution depth
texture. `VFX_ShieldShell_SetImpact()` supplies a decaying impact ripple.

## 2026-08-16 — Glass shell: refraction + contact (recipe của user)

Quả cầu kính được viết lại theo recipe: `fresnel = pow(1-saturate(dot(N,V)),4)`,
`contact = getDepthIntersection()`, `distortion = noise * strength`, màu/alpha
cuối ghép từ ba term. Vì body pass bind thẳng `renderTex`, khúc xạ KHÔNG được
sample scene sống (landmine #15) — thêm core API snapshot an toàn:

- `ScreenDistort_RequestSceneSnapshot()` — gọi khi shield còn sống (trong
  `VC_ShieldShell_Update`; cờ per-frame, không copy khi không cần).
- `ScreenDistort_SnapshotScene()` — main.c gọi NGAY SAU `MyEndMode3D` (2D time),
  sao chép renderTex → RT riêng (đúng format, giữ HDR, orientation khớp
  gl_FragCoord; không cần bọc matrix nữa — lúc này matrix đã là identity).

**16/08/2026 (sửa tiếp) — "nó tàng hình luôn rồi":** sau khi áp recipe, shield
biến mất hoàn toàn. Gốc: `SnapshotScene()` bị đặt TRONG 3D pass (trước
`VFX_Compose_Draw3D`), mà raylib `EndTextureMode()` **reset cứng projection +
modelview về ortho màn hình, không khôi phục matrix của caller** — mọi draw sau
đó trong pass (toàn bộ composition: shield, smoke, particle, character, trail)
vẽ với projection sai → vô hình. `rlPushMatrix/rlPopMatrix` chỉ cứu modelview,
không cứu projection. Sửa: snapshot dời ra 2D time sau `MyEndMode3D`, shield vẽ
trong post-pass riêng `VFX_ShieldShell_DrawRefraction(camera)` (export qua
`visual_composer.h`, gọi trong `MyBeginMode3D` thứ hai sau snapshot) — khúc xạ
giờ còn THẤY cả character/trail/atmosphere (scene hoàn chỉnh), khớp pattern
fluid. `VC_ShieldShell_Draw3D` giữ làm stub archetype (sync_vfx_test.py).
- `ScreenDistort_GetSceneSnapshotTexture()` — texture sample được.

Shield: bind snapshot (u_sceneTex) + prev-frame depth (u_depthTex), gate
`u_hasScene/u_hasDepth` để frame đầu/thiếu depth không sample rác; contact glow
rs dọc theo mức chạm đất (`u_contactThickness` ~0.35 m). Tunables mới:
`shield_shell_distortion/noise_scale/noise_speed/contact/contact_thickness/
base_alpha/fresnel_alpha/contact_alpha`. BODY = alpha (recipe), EMISSION =
additive (rim + contact + specular + crisp edge `pow(fresnel,14)`).

Test: `shield_shell_test.c` đổi contract (bind snapshot thay vì "không texture"),
thêm mirror số cho pow-4 fresnel + contact; `uv_deform_test.c` sửa string stale
`calcFresnel(normal, viewDir` → `facingNormal`. Còn 4 suite đỏ là lỗi pre-existing
(energy_burst/tube_frame/vfx_layered_field/volume_trail).

## 2026-08-13 — Shock ring: một dây khói khép kín, bị xé ra

`VFX_ComposeShockRing` viết lại tại chỗ (cùng signature, không thêm primary).
Bản cũ là hoop giải tích thuần — không texture, không UV, không shader — nên
silhouette là annulus chính xác về toán học và **không cơ chế nào** trong nó có
thể ra rìa tơi tả.

**Kết luận cuối, sau khi phóng to footage tham chiếu:** những "sợi" sáng KHÔNG
phải sợi khói, mà là **viền erosion** — tập mức `{noise == threshold}` của một
trường fbm. Contour của fbm chính xác là đường mảnh uốn lượn, rẽ nhánh, tự khép
vòng thấy trong video. Không cấu trúc nào khác cho ra hình đó: sợi sinh ra thì
thẳng và đều, sprite thì tròn cục, còn thân khói sáng thì là mây. Nên **thân
khói mờ, viền nóng**.

- Mesh = canvas, shader = silhouette. Band 0.22×R → canvas 0.66×R; crest 2/3 →
  1/3 canvas (vẫn rơi đúng vertex 2/6); sweep phát UV.
- Hai dây đồng tâm, tách xa nhau theo `t01`; dây ngoài mỏng hơn và ngưỡng cao
  hơn nên xé trước.
- **Hai lực đổi ngôi:** giãn nở ease-out `1-(1-t)^2.6` (dồn về đầu), ngưỡng
  erosion tăng theo `t²` (dồn về cuối). Vòng bay ra khép kín rồi mới rã khi đã
  chậm lại — đúng như user mô tả.
- Tendril = **không gian lấy mẫu bị nén theo bán kính** khi vòng nở, nên feature
  bị kéo dãn ra ngoài. Không có gì animate chiều dài sợi cả.
- Pass BODY (alpha) mang màu, EMISSION (additive) là bloom.

**Ba ngõ cụt đã đi qua, ghi lại để không lặp:**
1. *Sinh sợi theo cell góc* (`u_fibers`, mỗi cell một sợi) → **cái lược**. Một
   feature mỗi cell nghĩa là các feature cách đều THEO ĐỊNH NGHĨA; mắt đọc ra
   khoảng cách đều trước khi đọc bất kỳ biến thiên nào. Warp và jitter chỉ biến
   nó thành cái lược đẹp hơn (hàng mi). Đừng đề xuất lại hướng generative.
2. *Warp biên độ bằng nhau trên u và v* → **gạch ngang tiếp tuyến**. u trải hết
   chu vi, v chỉ trải 0.66R → 1 đơn vị u = ~9.5 đơn vị v. Đã lên
   `core/docs/LANDMINES.md`.
3. *Shader mới không vào CMakeLists* → load fail → fallback hoop cũ, im lặng,
   trông y hệt bản trước. Nay có test canh cả hai dòng `configure_file`.
4. *Thắp sáng đường đồng mức* `{dens == thr}` → **viền đôi**. Mật độ cắt ngang
   dây khói là một cái bướu, nên mọi ngưỡng dưới đỉnh đều bị cắt hai lần. Contour
   đúng cho trường procedural, sai khi hình đã đến từ texture — lúc đó chỉ cần
   thắp sáng chính các vệt. Test canh bằng phủ định `abs(dens - thrA)`.
5. *Canvas 6.0* → khói phủ tới tâm, nơi polar UV bóp về một điểm nên sheet bị
   nén còn chu vi 0 và render ra vệt tia hội tụ. Trần là 5.0; test canh
   `holeRatio` từ cả HAI phía (đủ nhỏ, nhưng phải tránh điểm kỳ dị).
6. *Chia cho `widA` không có sàn* → đầu đời dây hẹp, sheet bị phóng 6× ngang
   băng, texel nhoè thành tia. `max(widA*2.2, 0.26)` — sàn để chặn độ giãn, không
   phải guard chia-cho-0.
7. *Chuẩn hoá sheet theo pixel sáng nhất* → 88% dưới alpha 0.1 → vòng rỗng mà
   không term nào sai. Dùng percentile 99.2%. Kèm theo: phải remap dải dữ liệu
   thật sự chiếm ngay sau khi sample, đừng chỉnh các ngưỡng phía dưới.
   Cả hai đã lên `core/docs/LANDMINES.md`.

**Texture riêng, MÔ PHỎNG chứ không procedural** (`scripts/gen_shock_ring_smoke.py`,
`VFX_SURFACE_SHOCK_RING_SMOKE`, 2048×512). Đây là mảnh cuối làm vòng hết vẻ máy
móc: fbm đồng nhất thống kê, nên xé kiểu gì cũng ra chuỗi hạt giống nhau. Advect
hạt qua trường curl-noise cho ra nét dài quét cạnh chi tiết mảnh cạnh chỗ trống,
vì hai vùng kề nhau có *lịch sử* khác nhau. Không cần taichi, chỉ stdlib, ~60s.
Ba nguồn bất đối xứng, theo thứ tự quan trọng: seed theo CỤM, biến thiên
per-particle, rồi mới đến trường. Tuần hoàn theo x nên vòng wrap thẳng.

**Một vòng, biến đổi theo thời gian** — không phải nhiều vòng. Ảnh tham chiếu
"nhiều vòng đồng tâm" là CÙNG một vòng ở ba thời điểm; vẽ nhiều bản cùng lúc thì
có ba mặt sóng trên màn hình và vòng thôi là một vật. Bậc thang High/Mid/Low của
Thomas Pluys do đó nằm trên trục THỜI GIAN: khói trẻ sắc, khói già tán
(`SHOCK_DETAIL_EARLY 1.60` → `SHOCK_DETAIL_LATE 0.50`).

Hai ngõ cụt nữa đã ghi trong file: ba instance gần trùng bán kính chỉ làm dày một
cạnh rồi lấp mất giữa; echo lệch thời gian đúng hình nhưng sai bản chất.

Vòng phải **TÁN đi**, không phải chỉ dừng lại: `Alpha01` mũ 1.1 → 1.6, lõi nóng
nguội dần (`mix(1.0, 0.30, t01)`), băng chỉ nở nhẹ 0.18 → 0.30 (ở 0.50 nó nuốt
luôn phần giữa và kết thúc đời như một cái đĩa đặc).

`u_layerDetail` **không được** nhân vào tần số lấy mẫu quanh vòng — cuối đời tụt
còn 0.5 thì chỉ còn ~1 chu kỳ sheet cho cả chu vi, mỗi cột texel phóng thành dải
xuyên tâm và vòng hoá SAO đúng lúc đáng lẽ mềm nhất. Tần số cố định 4 và 7.

`shock_hole` là knob live cho độ lớn khoảng trống giữa — thẩm mỹ, chỉnh bằng mắt.

- `shock_ring_test.c`: 98 checks pass. Xác nhận trực quan bằng
  `--render-vfx 22` (warmup 22/28/34).

## 1. Nguyên nhân gốc

`tuning.cfg` còn giữ `strandtrail_style = 1.0` từ một lần A/B trước đó. Đó không
phải là knob chỉnh giá trị — nó **thay nguyên hàng style** cho mọi strand trail
đang sống, mỗi frame, trong `StrandTrail_OnUpdate`:

| | ENERGY (được yêu cầu) | SMOKE (bị ép) |
|---|---|---|
| `hotWhiten` | 0.72 | **0.0** — không có lõi nóng để tô |
| tint | `glow` (nóng) | `body` (tối) |
| blend | additive | `BLEND_ALPHA` |
| sheet | `energy_wisp.png` | `smoke_strand.png` |

Nên "dải đỏ đặc, không có lõi vàng" chính là style SMOKE của material Fire, chạy
đúng như thiết kế. Mọi thay đổi shader ở phiên trước đều **đúng** nhưng không thể
nhìn thấy, vì nhánh đang sửa chạy bằng tham số của style kia.

**Bằng chứng.** `./build/wuxing --render-vfx 27` (bench fixture 27 =
`VFX_ComposeStrandTrail(&xf, VC_MAT_FIRE, 0, 2.0f, VFX_STRAND_ENERGY)`):

- với `strandtrail_style = 1.0` → dải đỏ đặc, log `style -> smoketrailfx`;
- với `strandtrail_style = -1.0` → **sợi vàng kim, lõi nóng rõ**, log
  `style -> energytrail`.

Không đổi một dòng shader nào giữa hai lần render đó.

## 2. Đã sửa gì

1. **`tuning.cfg`** — `strandtrail_style` về `-1.0`, kèm chú thích tiếng Việt nói
   rõ tại sao phải giữ ở `-1` khi không đang so sánh.
2. **`core/composition/common/vc_strand_trail.inl`** — khi override ép một style
   **khác** style được yêu cầu, log `LOG_WARNING` (log-on-change), nêu tên file,
   style bị ép **và** style được yêu cầu. Dòng `LOG_INFO` cũ chỉ nói ai thắng, nên
   đọc y hệt nhau dù override có bật hay không.
3. **Chú thích lỗi thời** ở hàng ENERGY ("trail chỉ chạy BODY, main.c không gọi
   emission") đã sửa — `main.c` gọi cả hai pass.
4. **Landmine** — `ENGINE_LANDMINES.md` §13 (cross-cutting) + một mục trong
   `core/docs/LANDMINES.md`.

## 3. Test đã sửa trong đợt này

- **`vfx_contrast_test` chưa từng BUILD được** trong harness (`core/vfx_contrast.h`
  include `raylib.h`, tier headless không có raylib). Báo cáo "PASS" ở phiên trước
  là từ một lần compile tay. Đã thêm `core/tests/stubs/raylib.h` (chỉ plain data,
  không bao giờ khai báo hàm raylib) + include path trong
  `scripts/run_core_tests.sh`, và test nay include thẳng `core/vfx_contrast.c`.
  **PASS thật.**
- **`trail_deform_test`** — assertion cũ đòi `VFX_ComposeSmokeTrail` biến mất khỏi
  public API. Việc xóa đó chưa bao giờ xảy ra (xem §4). Assertion nay chốt hợp
  đồng THỰC TẾ: bản thay thế tồn tại, và file cũ tự khai báo nó đã bị thay thế.
- **`swept_trail_test`** — assertion cũ đòi `#define SWEPT_ASSET_PATH
  "assets/textures/energy_flow.png"`. Literal đó bị bỏ có chủ đích: sheet nay lấy
  qua surface registry. Assertion nay chốt đúng nguồn đó.

`./scripts/run_core_tests.sh`: **43/47 suites PASS.** 4 suite còn đỏ
(`energy_burst_semantic_layers_test`, `tube_frame_test`,
`vfx_layered_field_contract_test`, `volume_trail_test`) đã đỏ từ trước đợt này,
không thuộc phạm vi strand trail — chưa đụng tới.

## 4. Quyết định còn treo (cần người dùng chốt)

### 4.1. `VFX_ComposeSmokeTrail` — xóa hay giữ?

Được đánh dấu xóa từ 03/08/2026, một test còn assert là nó đã biến mất, nhưng nó
vẫn còn: khai báo trong `visual_composer.h`, cài đặt trong `vc_smoke_trail.inl`
(485 dòng), và nối vào bench entry 25. Bản thay thế là
`VFX_ComposeStrandTrail(..., VFX_STRAND_SMOKE)`. Rủi ro là **hai nút bench trông
giống nhau** — đánh giá/tune nhầm cái không ship. Phiên này chỉ dán cảnh báo lên
đầu file cũ; xóa thật đụng cả `sandbox/`, nên chờ chốt.

### 4.2. Swept trail đang dựng sheet từ ảnh sai

`SweptTrail_BuildAssetSheet` (`vc_ribbon_trail.inl`) được migrate sang surface
registry, nhưng `VFX_SURFACE_ENERGY_RIBBON` trỏ tới `energy_wisp.png`
(512x512, sheet STRAND tiling, chủ sở hữu là strand trail), trong khi mọi hằng số
crop/rotate trong hàm đó được đo trên `energy_flow.png` (1792x896, vẽ nằm ngang,
nội dung chỉ ở 40% giữa chiều cao). `energy_flow.png` **chưa được đăng ký** trong
`assets/vfx_surface_profiles.json`.

Không ảnh hưởng hình ảnh đang ship: nhánh này là opt-in (`swept_sheet`, mặc định
0 = procedural). Cách sửa đúng là đăng ký `energy_flow.png` thành profile riêng —
việc đó cần khai báo channel grammar theo `assets/TEXTURE_PACKING.md`, tức là một
quyết định về asset, nên chưa tự làm. **Đừng chỉnh lại các hằng số crop để chiều
theo sheet sai.** Hiện đã ghi cảnh báo ngay tại chỗ trong code.

## 5. Ribbon trail bệt màu trên nền sáng — ĐÃ SỬA

Người dùng báo tiếp: ribbon trail (`VFX_ComposeRibbonTrail`, bench 19) vẫn bệt
màu trên nền sáng. Đây là lỗi **khác** lỗi §1 và đã tái hiện + đo được.

**Nguyên nhân.** `k_sweptLayers` viết cho trail ADDITIVE, nên `alphaMul`
(MAIN: 0.10 / 0.36 / 0.30) là **trọng số phát sáng** — chúng cộng lại thành ánh
sáng. `DrawLayeredRibbon`/`DrawLayeredTube` đưa thẳng con số đó vào pass BODY
(BLEND_ALPHA) làm **coverage**, nên body bị chặn ở 0.36. Compositor tính
`scene*(1-0.36) + bodyColor*0.36` → 64% nền sáng còn nguyên, màu của trail bị
pha loãng ngay từ đầu. `TrailMaterialConfig::bodyOpacity` chính là coverage
được viết riêng cho việc này, nhưng chỉ đường DEFORM đọc nó; đường layered cổ
điển bỏ qua hoàn toàn. Cộng thêm: `TrailLayer::whiten` cũng được áp trong pass
BODY, làm nhạt đúng cái lớp có nhiệm vụ giữ màu.

**Đo được** (peak chroma trên nền sáng, fixture 19):

| `swept_body` | chroma |
|---|---|
| 0.0 (hành vi cũ) | 0.31 |
| 0.55 | 0.40 |
| **1.0 (mặc định mới)** | **0.72** |
| nền đêm, trước khi sửa | 0.61 |

**Đã sửa.**
- `trail_system.c`: thêm `TrailLayerPassAlphaMul()` + `TrailLayerWhitensThisPass()`,
  cả hai đường layered (ribbon + tube) đi qua đó. Chỉ đổi hành vi khi trail
  additive CÓ đặt `bodyOpacity`, nên mọi trail cũ giữ nguyên.
- `vc_ribbon_trail.inl`: `s_sweptBodyOpacity = 1.0f` + tunable `swept_body`,
  đẩy lại mỗi frame (không bake lúc spawn).
- `bright_vfx_isolation_test`: thêm regression cho cả số học lẫn call-site.
- Landmine trong `core/docs/LANDMINES.md`.

1.0 **không phải** "đục hoàn toàn": chỉ layer 1 (layer mang sheet) vẽ trong pass
BODY, và coverage vẫn bị nhân bởi alpha mềm của sheet, taper bề rộng và đường
cong vòng đời.

## 6. Còn cần mắt người xác nhận

Smoke trail / smoke column sau khi gỡ coverage phi tuyến (`distortion.fs` đã trở
lại `float bodyCoverage = body.a;`) cần một lần nhìn lại. Và giá trị `swept_body`
là quyết định thẩm mỹ — sweep bằng `tuning.cfg` nếu 1.0 quá đặc cho gu của bạn.

**Cách tái hiện nền sáng khi cần lại:** clear nền thôi là chưa đủ (skybox vẽ đè —
phải bỏ luôn `MapManager_DrawActive`), và body/emission của VFX đi vào render
target riêng nên phép trộn thật sự nằm ở `distortion.fs`, không phải lúc draw.

## 7. Những hướng không được lặp lại

- **Không sửa shader trước khi chứng minh nhánh đó đang chạy.**
  `./build/wuxing --render-vfx <index>` render một fixture bench ra PNG trong vài
  giây và in kèm log chọn style. Nó trả lời "có phải effect mình đang sửa không"
  trước khi sửa, thay vì sau lần sửa thứ mười.
- Không tăng additive/HDR để chữa nền sáng; cách đó đẩy màu gần trắng hơn.
- Không dùng nonlinear alpha expansion toàn cục; nó làm lộ biên smoke, particle
  và decal.
- Không viết workaround riêng trong VFX composition nếu lỗi nằm ở core.
- Trước mọi A/B thị giác: đọc cả block `tuning.cfg` của effect đang xét. Knob
  **chỉnh giá trị** thì nêu hệ số trong báo cáo; knob **chọn biến thể** thì phải
  tự hét lên (nay đã có).

## 8. Đợt hợp nhất trail — ĐÃ CHẠY, test về 43/47

Gộp ribbon + strand thành MỘT trail mô tả bằng dữ liệu
(`core/trails/trail_recipe.h`), giữ nguyên nửa mô phỏng của `trail_system.c`.
Kế hoạch: `~/.claude/plans/inherited-hugging-kettle.md`.
Mốc an toàn NGAY TRƯỚC đợt này: commit `d372823` — bỏ đợt gộp =
`git reset --hard d372823`, không cần stash theo thư mục.

### 8.1. Đã xong

- `trail_recipe.h` — `TrailRecipe` = geometry + `UVDeformField` + `SurfaceFlow`
  + surface + mask + colour + pass policy, dựng từ `core/uv/` đúng như
  `core/uv/README.md` tuyên bố (`mesh + UVDeformField + SurfaceFlow = effect`).
- `vc_ribbon_trail.inl` → `vc_trail.inl`: `k_trailPresets[]` (6 preset) +
  `k_trailMotion[]` (thay 5 switch theo kind — một bảng thì không quên cột).
- Xoá `vc_strand_trail.inl`, `vc_smoke_ribbon_trail.inl` (392 dòng code chết,
  không được include ở đâu), nhánh `SweptTrail_BuildAssetSheet` +
  `swept_sheet` (đóng luôn mục 4.2), tầng alias `VFX_ComposeSweptTrail`.
- API mới: `VFX_ComposeTrail` / `VFX_ComposeTrailEx` / `VFX_TrailSetWidth` /
  `VFX_KillTrail` / `VFX_Trail_Stop`. `vc_projectile.inl` đã migrate.
- Test: 43/47 — bằng mức trước đợt gộp. 4 suite đỏ còn lại
  (`energy_burst_semantic_layers`, `tube_frame`, `vfx_layered_field_contract`,
  `volume_trail`) đã đỏ từ trước, không liên quan.

### 8.2. Năm lỗi "preset strand bị chạy qua đường của swept"

`TRAIL_PRESET_ENERGY` từng render ra rỗng. Tìm ra bằng **probe nhị phân**, không
phải đọc code: ép mode 2 trả magenta → không pixel nào; ép ngay đầu `main()` trả
xanh → đúng 4 pixel. Tức shader CÓ chạy, nhưng dải gần như không có diện tích.

| # | Lỗi | Sửa |
|---|---|---|
| 1 | `s_sweptWidthCurve/AlphaCurve` chỉ nạp cho 4 preset swept; **FloatCurve rỗng eval ra 0** → bề rộng 0, alpha 0 | cờ `motion.curves` |
| 2 | `aspectCap=false` vẫn bị chia đôi bề rộng | radius LÀ nửa-bề-rộng |
| 3 | `t->tint = WHITE` mỗi frame tẩy trắng preset mang màu trong tint | chỉ áp khi có gradient |
| 4 | `MaxNodes` hardcode 60 Hz cho trail 30 Hz → giữ gấp đôi lịch sử, cắt đuôi vuông | `MaxNodesFor(kind, ...)` |
| 5 | `nodeHomeSpring/MaxDev/OrderFrac` áp cho preset không cloth | gate theo `motion.cloth` |

Cộng thêm **cùng lớp lỗi với §1**: `tuning.cfg` có `swept_width = 3.0` và
`swept_alpha = 1.5` — knob GLOBAL của họ swept nhân vào preset strand vốn chưa
bao giờ có knob đó (0.45 m → 1.35 m, thành cục phình). Nay `TrailMotion` có cờ
`sweptKnobs`; **một knob global trong composer dùng chung PHẢI nêu rõ nó áp cho
preset nào.**

Và `cfg.deform.envHead/envTail/phase` bị bỏ sót — vertex deform tắt nhưng
FRAGMENT vẫn đọc chúng (ramp disorder + phase mỗi lần spawn). `envHead = 0` làm
cửa sổ head-weld sập, ramp bão hoà ngay segment đầu, dissolve cắn từ ĐẦU thay vì
từ đuôi.

**Đo được (chroma):** ENERGY 0.658 (bản duyệt 0.647) · MAIN 0.641 (0.656).
Màu khớp; hình dạng đã thuôn hai đầu, hết vết cắt vuông.

### 8.3a. Shader — mode 1 (packed wisp) ĐÃ XOÁ

`trail_deform.fs` từng có 3 mode. **Không composer nào đặt `material.mode = 1`**
kể từ khi strand trail thay thế nó — 30 dòng shader + 2 uniform
(`u_turbStrength`, `u_edgeTear`) không đường nào chạm tới, nằm trong đúng file
mà mọi trail phải debug qua. Đã xoá cùng với loc + upload phía C, và hai ngưỡng
`< 0.5` / `>= 1.5` gộp thành MỘT: dưới 1.5 là passthrough, trên là strand.

Test đã trỏ lại theo đúng kỷ luật cũ: assertion nay khẳng định **nhánh đó phải ở
trạng thái đã xoá**, không phải im lặng bỏ đi. Render không đổi (chroma 27:
0.658 = 0.658; 18: 0.878 vs 0.879).

Còn lại của bước shader: đưa phần LẤY MẪU qua `SurfaceFlow_FieldSample`
(`uv_field.glsl` đã có sẵn), để bỏ nốt `u_panSpeed`/`u_tiling` khỏi cầu tạm.

### 8.3. Cầu tạm — `TrailRecipe_ToLegacyMaterial`

`trail_deform.fs` VẪN là 3 mode viết tay. Cầu tạm dịch recipe → uniform cũ ở
ĐÚNG MỘT chỗ. Cố ý không gộp shader cùng lượt: công thức mode 2 là ~150 dòng
người dùng đã duyệt bằng mắt, viết lại cùng lúc với composer thì khi hỏng sẽ
không phân biệt được lỗi composer hay lỗi shader. **Xoá cầu tạm = định nghĩa
"xong" của bước shader.**

Cầu tạm hiện suy ra `wispMix/strandGain/flowStrength/bundleWeight/bundleWidth`
từ recipe thay vì hardcode — hardcode từng đưa số của ENERGY cho cả SMOKE.

### 8.3b. Bench — một nút MỖI PRESET (đã xong)

`scripts/sync_vfx_test.py` sinh lại toàn bộ manifest, khoá theo `.inl`, một file
một entry — nên hàng thêm tay bị bỏ, và entry cũ quay ra spawn `0` = BLADE, tức
**ENERGY không còn vào được từ bench**. Đã thêm `FIXTURE_PRESET_VARIANTS` vào
script: một composition mà chủ đề chính là "cùng một máy, nhiều look được viết
sẵn" thì các look phải nằm cạnh nhau trên bench. Đây là ngoại lệ DUY NHẤT của
bất biến một-entry-một-`.inl`, và lý do là §1: giấu lựa chọn sau một tuning value
chính là thứ khiến hai phiên render nhầm style mà không ai thấy được.

Bench nay có 6 nút: 27 MAIN · 28 ENERGY · 29 BLADE · 30 WISP · 31 BACKDROP ·
32 SMOKE.

### 8.3c. SMOKE — ĐÍNH CHÍNH, và đổi sang trắng

**Tôi đã ghi sai ở commit `c8cf913`.** Tôi viết "SMOKE HỎNG — dải đỏ phẳng,
không có sợi". Sai: sợi, chuyển động và texture đều đúng. Kết luận đó dựa trên
một render ở pha xấu, ở kích thước nhỏ, và tôi đã không kiểm lại trước khi ghi
vào doc lẫn commit message.

Màu đỏ cũng **không phải lỗi**: bench truyền `VC_MAT_FIRE`, preset lấy tint từ
material. Preset làm đúng điều nó được bảo.

Cái ĐÚNG là có, nhưng nhỏ hơn nhiều: `tailColor = lerp(base, m->body, tailDarken)`
trở thành **vô nghĩa** khi `base` đã là `m->body` — đầu và đuôi cùng màu, ramp
dọc trail bằng 0. Không phải hồi quy: `vc_strand_trail.inl` cũ có đúng công thức
đó. Và `TrailColorConfig.tail` là trường khai báo cho đúng việc này nhưng **không
ai đọc** — API chết.

Đã sửa (theo yêu cầu của người dùng, khói → trắng):
- `bool useGlowTint` → `TrailTintSource {GLOW, BODY, NEUTRAL}`. Một bool không
  nói được "trung tính", mà khói cần đúng thế: khói là SẢN PHẨM cháy, không phải
  nguyên tố — khói của Fire không nên đỏ, của Lightning không nên xanh.
- Cầu tạm nay đọc `colour.tail` khi được khai báo, rơi về phép trộn cũ khi không.
- SMOKE: trắng ở đầu, nguội về xám trung tính.

### 8.3d. Chuỗi chữ V ở BACKDROP — là `tuning.cfg`, lần thứ BA

| khoá | tuning.cfg | mặc định code |
|---|---|---|
| `swept_tile` | 0.5 | 1.10 |
| `swept_flow` | +1.0 | **−1.0** |

Tile nửa lại ⇒ 16 vệt của mask procedural lặp gấp đôi mật độ ⇒ đúng "cái lược"
mà chú thích trong `SweptTrail_BuildBladeMask` nói nó cố ý tránh. `swept_flow`
đảo dấu ⇒ hoa văn trôi về phía đầu, vật chất trông như chảy NGƯỢC vào nguồn.
Cùng build, chỉ khác tuning.cfg → khác hẳn. Cả 4 preset swept dùng chung hai
knob này; BACKDROP rộng nhất nên lộ trước.

CHƯA sửa tuning.cfg — có thể là lựa chọn cũ của người dùng.

### 8.3e. QUYẾT ĐỊNH: swept chuyển sang vật liệu STRAND

Nguyên nhân gốc của "texture xấu": preset swept chạy nhánh **passthrough**
(`texture(texture0, vSegUV) * vColor`) và đang mặc **mask procedural sinh lúc
chạy** — thứ mà code tự gọi là *fallback*. Đường art thật (`energy_flow.png`) đã
bị xoá vì nó đã trỏ sai ảnh. Nên 4 preset swept đang mặc đồ dự phòng vĩnh viễn.

`energy_wisp.png` KHÔNG dùng thẳng được cho passthrough: nó là sheet STRAND
(`R:pattern1 | G:pattern2 | B:distort | A:dissolve`), đưa vào passthrough thì RGB
bị hiểu là màu và A bị hiểu là độ đục — đúng loại nhầm mà
`assets/TEXTURE_PACKING.md` được máy kiểm để chặn.

**Hướng đã chốt:** bỏ nhánh passthrough cho preset swept, cho cả 6 preset dùng
CHUNG vật liệu strand, chỉ khác nhau ở deform/flow layer. Khi đó:
- `energy_wisp.png` dùng được ngay, đúng grammar;
- `trail_deform.fs` còn đúng MỘT công thức (passthrough chỉ còn cho trail không
  có recipe) — hoàn tất việc #3;
- tune bằng layer, không bằng ảnh.

Việc cần làm: cho 4 preset swept `topology = PARALLEL` + deform layer riêng (cloth
vẫn giữ, nó là chuyển động chứ không phải bề mặt), `surface = ENERGY_RIBBON`, bỏ
`sheetOverride` + `SweptTrail_BuildBladeMask`. Nếu cần gu sợi khác (dày/mảnh),
sinh biến thể sheet bằng script như `scripts/gen_energy_wisp_texture.py`.

### 8.4. Còn lại

- Gộp `trail_deform.fs` về một công thức đọc `u_uvField`/`u_flowLayer`
  (`uv_field.glsl` đã sẵn cả hai lối SUMMED và PARALLEL).
- Manifest bench: một entry mỗi preset (sửa tay `scripts/vfx_test_manifest.json`
  trước, rồi `scripts/sync_vfx_test.py`). Hiện bench vẫn 2 entry cũ.
- SMOKE preset (`--render-vfx 25` vẫn là puff tube cũ) chưa được nhìn bằng mắt.
- `trail_glow.fs` KHÔNG chết (có load lúc chạy) — bỏ khỏi danh sách xoá.

## 9. Texture cho trail — thiết kế và audit (10/08/2026)

Ghi lại để phiên sau chạy thẳng, không phải suy luận lại.

### 9.1. Sheet sợi KHÔNG đúng cho mọi archetype

Sau khi 4 preset swept chuyển sang vật liệu strand (§8), cả 6 preset đều đọc
`energy_wisp.png`. Đó là lý do BACKDROP trông mảnh: nó đang mặc sheet của
archetype khác. Năm archetype, năm nhu cầu khác nhau:

| Archetype | Sheet phải mang gì | Hiện có |
|---|---|---|
| **Filament** (ENERGY, WISP) | sợi mảnh + **khe hở giữa chúng** — khe hở CHÍNH LÀ hiệu ứng | ✅ `energy_wisp.png` |
| **Blade** (BLADE) | **biên ngoài sắc + lõi sáng**; một lưỡi kiếm phải đọc ra VẬT THỂ, sợi biến nó thành năng lượng | ❌ mượn sheet sợi |
| **Cloth** (MAIN) | **nếp gấp rộng, tần số thấp**, liên tục dọc chiều dài | ❌ |
| **Mass** (BACKDROP) | gần như mây: rất mờ, rất rộng, **không chi tiết** — chi tiết đánh nhau với trail phía trước | ❌ |
| **Shape** (SMOKE) | MỘT vệt hoàn chỉnh, taper vẽ sẵn hai đầu, stretch một lần | ✅ `smoke_strand.png` |

### 9.2. KHÔNG cần grammar mới — cần MỘT generator có tham số

Layout `STRAND` (`R:pattern1 | G:pattern2 | B:distort | A:dissolve`) mô tả được
cả bốn loại trên. Khác nhau chỉ là **tần số, tương phản, cách xử lý biên** —
tức là NỘI DUNG, không phải hợp đồng kênh. Nên đây là một script có preset, chứ
không phải bốn script rời (mỗi script rời lại là một bản sao của cùng một ý,
đúng thứ §8 vừa xoá ở tầng composer).

`scripts/gen_energy_wisp_texture.py` hiện hardcode: `SIZE`/`OUT` là hằng số
module, không có argparse, sinh đúng một ảnh. Việc cần làm là tách các hằng số
tạo hình thành preset (mật độ sợi, độ dày, tương phản biên, tần số nền) + CLI
`--preset <name> --out <path>`.

### 9.3. Audit `assets/textures/` — 69 file

```
69 .png  →  19 trong registry
           26 chỉ code tham chiếu  (KHÔNG qua registry)
           24 KHÔNG ai tham chiếu
```

> **ĐÍNH CHÍNH 15/08/2026 — rune glyph sheets vẫn được dùng.**
> `vc_rune_circle.inl` nạp `rune_glyphs_0..3` bằng đường dẫn dựng lúc chạy;
> không xoá asset chỉ dựa trên audit tên file.
>
> **Quy tắc:** một lần grep theo tên file KHÔNG chứng minh được file mồ côi.
> Phải quét luôn các chỗ dựng đường dẫn động (`snprintf`/`TextFormat` +
> `assets/textures`) trước khi xoá bất cứ thứ gì. Trong repo này hiện chỉ có
> đúng một chỗ như vậy — nhưng phải KIỂM, không phải nhớ.
>
> Số đúng: **22 file không có consumer lúc chạy** (không xuất hiện trong bất kỳ
> `.c`/`.h`/`.inl`/`.json` nào). ĐÃ `git rm` (chủ repo chốt xoá thẳng, không qua
> `_unused/`); 5 trong số đó sinh lại được bằng script (`gen_dust_flipbook.py`,
> `sim_fire_flipbook.py`, `gen_volume_trail_textures.py`, `flipbook/pack.py`),
> 17 file còn lại không ai tham chiếu ở đâu cả. `assets/textures/` còn 47 file.
> Kiểm sau khi xoá: cmake configure qua (validator registry chạy ở đó), render
> bench không có cảnh báo thiếu asset, test 43/47.

**24 file mồ côi.** Có một cụm flipbook trùng lặp rõ rệt:
`fire_atlas_8x8`, `fire_puff_8x8`, `fire_puff_8x8_smoke`, `smoke_puff_8x8`,
`smoke_puff_8x8_flame`, `dust_puff_4x4`, `dust_puff_4x4_smoke`,
`dust_puff_8x8`, `flame_tongue_8x8`; cộng `rune_glyphs_0..3`, một cụm
ground/grass PBR (`grass_ground_*`, `ground_composed*`, `dirt_diffuse_soft`,
`grass_detail`), và `gradient_alpha`, `petal_card`, `qi_wisp_soft`.

**26 file "chỉ code tham chiếu" mới là vấn đề kiến trúc thật** — chúng đi vòng
qua registry, nên KHÔNG ai kiểm channel grammar cho chúng. Đó đúng là loại nhầm
đã làm `energy_flow.png` bị đọc sai suốt (§4.2/§8.3e).

### 9.4. Việc phải làm, theo thứ tự

~~1. Tham số hoá `gen_energy_wisp_texture.py`~~ · ~~2. Sinh 3 sheet mới~~ ·
~~3. Trỏ `recipe.surface` sang sheet mới~~ — **HUỶ, xem §9.6.** Đo rồi: sheet
không phải biến số. Còn lại:

~~4. Dọn file mồ côi~~ — **XONG.** 22 file (không phải 24 — xem đính chính ở
   §9.3), `git rm` thẳng theo quyết định của chủ repo.
5. Đưa dần các file code-tham-chiếu vào registry — đây là việc dài, làm theo từng
   consumer, không làm một lượt. **CÒN TREO.**

### 9.5. §9.5 ĐÃ TRẢ LỜI: tune tới nơi, KHÔNG cần sheet mới

Đã sweep bằng 6 tunable tạm (`dbg_bundle/gain/edge/dissolve/wisp/third`) trên
bench 27–32, render headless, so với **mặc định code** (`tuning.cfg` đã trung
hoà — xem cảnh báo §8.3d). Kết quả: **blade, cloth và mass đều lấy được từ
`energy_wisp.png`.** Ba file art trong §9.1 rơi khỏi kế hoạch.

Knob quyết định là **`gain`** — số mũ `pow()` áp lên mật độ lấy mẫu:
- `gain < 1` nâng các KHE HỞ giữa sợi lên cho tới khi chúng dính lại thành khối;
- `gain > 1` đẩy chúng ra thành sợi rời.

Tức archetype nằm ở CÁCH ĐỌC bề mặt, không nằm ở bức ảnh. Đúng tinh thần §9.2
("cần một generator có tham số") nhưng rẻ hơn một bậc: tham số nằm ở phía đọc,
không phải phía sinh ảnh.

### 9.6. `TrailStrandConfig` — archetype là DỮ LIỆU, không phải suy diễn

Năm số điều khiển việc lấy mẫu trước đây được **cầu tạm suy ra** từ những trường
không liên quan gì tới chúng:

| số | suy ra từ (cũ) | vì sao sai |
|---|---|---|
| `bundleWidth` | `waveAmp * 0.85` | buộc bề RỘNG của bó vào biên độ nó ĐUNG ĐƯA — hai thứ độc lập |
| `thirdWeight` | `mask.tailNarrow` | sửa knob đuôi lại đổi cách lấy mẫu bề mặt |
| `flowDistort` | `mask.dissolveSoft` | như trên |
| `gain` | `colour.coreWidth > 0 ? 1.35 : 0.75` | archetype bị suy ra từ việc CÓ lõi nóng hay không |
| `fineMix` | `flow.layerCount > 1 ? 0.6 : 0.7` | |

Những ràng buộc đó được nghĩ ra chỉ để **khỏi phải viết một hằng số ra**, và giá
phải trả rất cụ thể: trường `bundle` đã được khai trong `k_sweptStrand[]` NGAY TỪ
ĐẦU rồi bị `(void)` vứt đi. Nên cả bốn preset swept bị ghim dưới **một phần tư
quad của chính nó** → mảnh như sợi tóc ở mọi bán kính. **Đó chính là "quá mảnh"
mà chủ repo báo** — không phải do sheet.

Nay là `TrailStrandConfig` trên recipe, khai báo tường minh từng preset:

| preset | bundle | gain | edge | ý đồ |
|---|---|---|---|---|
| BLADE | 0.36 | 0.34 | 0.06 | VẬT THỂ: đặc ruột, biên ngoài sắc |
| MAIN | 0.65 | 0.61 | 0.32 | vải: nếp rộng, tần số thấp |
| WISP | 0.22 | 1.35 | 0.14 | giữ nguyên chất sợi (`gain > 1`) |
| BACKDROP | 0.95 | 0.19 | 0.75 | khối: không chi tiết để đánh nhau với trail trước |
| ENERGY | 0.34 | 1.35 | 0.18 | **giữ NGUYÊN số cũ** |
| SMOKE | 0.26 | 0.75 | 0.34 | **giữ NGUYÊN số cũ** |

ENERGY là preset DUY NHẤT chủ repo đã duyệt bằng mắt, nên nó phải không đổi —
render xác nhận không đổi. BACKDROP hạ trọng số layer (0.055/0.14 → 0.025/0.063):
lấp khe hở làm diện tích phát sáng tăng vài lần, trọng số vừa đủ cho một sợi
thưa thì chói khi thành dải đặc.

Test: `swept_trail_test` khẳng định bridge **ĐỌC** archetype thay vì suy ra
(`!FileHas("out->bundleWidth = L0->amplitude")`) — nếu ai đó khôi phục lối suy
diễn, test đỏ. 43/47, đúng bốn suite đỏ cũ.

### 9.7. "Khựng một cái rồi đổi pha" — ĐÃ CHẨN, ĐÃ SỬA

`FollowerCut` (`core/trails/trail_system.c`). Cắt xảy ra khi transform được bám
nhảy xa hơn `teleportSpeed * dt` trong một frame. Nó reset history về 1 node —
**cái khựng đó CHÍNH LÀ định nghĩa của cắt, giữ nguyên**. Nhưng nó còn xoá
`laidDist` = 0, mà `laidDist` là đồng hồ đo quãng đường `nodeUV[]` ghi lại theo
từng node và fragment stage đọc lại qua `u_pathArc.x` để định pha texture strand.
Nên hoa văn nhảy về đầu ĐÚNG frame trail khựng: hai triệu chứng, một nguyên nhân.

Giữ đồng hồ là an toàn — không ai khác đọc `laidDist`, nó KHÔNG phải chiều dài
của aspect cap, và đường vẽ đã tự gấp nó ở 8192 m cho độ chính xác float.

**Kiểm bằng cách ÉP nhánh chạy, không phải bằng suy luận:** hạ tạm ngưỡng xuống
4.6 m/s cho bench cắt liên tục → đồng hồ nay báo **giữ ở 4.32 m** qua các lần
cắt, chỗ trước đây báo 0.00 m. Log của cắt nay in kèm giá trị đó vĩnh viễn.

Chưa đụng vào ngưỡng 45 m/s: mọi lần cắt bench kích hoạt đều là nhảy thật lúc
spawn (3–5 m trong một frame). Trong game có swing nào chạm ngưỡng không thì nay
chỉ cần grep log một dòng.

### 9.8. Quyết định còn treo

- **24 file mồ côi:** chuyển sang `_unused/` hay `git rm` thẳng?
- **`tuning.cfg`:** `swept_width = 3.0` / `swept_alpha = 1.5` / `swept_tile = 0.5`
  / `swept_flow = +1.0` vẫn NGUYÊN, chưa đụng. Lưu ý mới: `swept_width = 3.0` có
  lẽ **không phải rác** — nó đang bù đúng cho cái hairline mà §9.6 vừa sửa tận
  gốc. Sửa xong gốc rồi thì ×3 đó nay là thừa và sẽ làm quad phình. Đề xuất: bỏ
  cả 4 dòng về mặc định. `swept_sheet = 1` thì đã CHẾT hẳn (nhánh bị xoá ở §8.1).
### 9.9. BLADE mỏng — ĐÍNH CHÍNH GIẢ THUYẾT CỦA CHÍNH TÔI, rồi đo ra thủ phạm

Ở bản §9.8 đầu tiên tôi viết "BLADE mỏng vì hình học — `RIBBON_FIXED_NORMAL`
normal `(0,1,0)` nên nhìn nghiêng". **Sai, và tôi viết ra khi chưa đo** — đúng
loại lỗi §8.3c đã tự kiểm điểm. Mặt phẳng của BLADE không cố định `(0,1,0)`: nó
được tính lại mỗi frame từ độ cong đường đi của mũi (`SweptTrail_UpdateNormal`,
dòng 1620 ghi đè `t->fixedNormal = s->normal`). Ribbon nằm TRONG mặt phẳng vung,
không nhìn nghiêng.

Đo bằng cách tách biến trên fixture 29 (mỗi lần chỉ đổi MỘT knob):

| render | knob | kết quả |
|---|---|---|
| `bw_c` | `swept_aspect = 4` một mình | **không đổi gì** → aspect cap KHÔNG chạm ở bề rộng bench |
| `bw_d` | `swept_width = 3` một mình | rộng lên rõ → bề rộng CALLER mới là thứ đang chặn |
| `bw_a` | `swept_width = 6` một mình | vẫn mảnh → tới đây thì cap MỚI chạm |
| `bw_e` | `swept_width = 6` + `aspect = 2.2` | đọc ra lưỡi kiếm có bề bản |

Tức có **HAI** ngưỡng nối tiếp, và trước đó tôi gộp làm một:
1. Ở 0.1 m bench truyền vào → `want = 0.05 m` nửa-bề-rộng thắng. Mỏng vì
   **người gọi xin mỏng**. Đúng thiết kế: bề rộng là việc của caller.
2. Trên ~0.15 m nửa-bề-rộng (sweep 6 m) → **cap 1:20 thắng**, và mọi giá trị lớn
   hơn đều sập về CÙNG một sợi. Tham số bề rộng mất hết ý nghĩa từ đó trở lên.

Chỉ (2) là lỗi. Đã sửa `aspectK` 0.0250 → 0.0550 (1:20 → 1:9 theo bề rộng đầy
đủ). **Nâng TRẦN, không tự nới cái gì**: bench 29 render y hệt, vì ở 0.1 m cap
chưa bao giờ chạm. Vẫn chặt hơn MAIN (0.0715) và BACKDROP (0.1).

Còn treo: `radiusDefault = 0.10f` dùng chung cho cả bốn preset swept — số một
caller nhận khi truyền 0. Chưa đụng, vì bench truyền số riêng nên không có bằng
chứng nào từ đây nói nó sai; muốn biết thì phải xem một skill thật gọi nó.

## SSF nước hết "nhựa" — thickness lấy lại gradient (2026-08-11)

Regression pin được về `0262068` ("update force field", 03/08): dòng gộp
`depthGap` đổi từ `min(kernel, max(0.022, gap*1.25))` sang
`max(kernel, min(0.40, gap*0.90))`. Với vật thể bay trên không, gap > 1 m nên
CỘT NƯỚC bị ghim cứng 0.40 m trên toàn bộ silhouette — vừa dày gấp 2.5x, vừa
mất sạch biến thiên độ dày. Đã trả lại ngữ nghĩa `min` (receiver chỉ CHẶN, không
TẠO cột nước).

Nguyên nhân thứ hai, có trước regression: `DecodeOpticalThickness` bão hoà ở cap
0.16 m với knee 0.11 m, trong khi orb 2.000 splat cộng ra p ≈ 1.3 m → mọi pixel
bên trong đều dính cap. Đã chia bù chồng lấn kernel (`/1.5`) và dời knee lên
0.42 m; **giữ nguyên dải output 0..0.16** nên toàn bộ ngưỡng foam/rim/coverage ở
hạ nguồn không phải chỉnh.

Số đo (mirror trong test): tỉ lệ core/rim 1.31 → 4.28; cột nước của orb bay
0.400 m → 0.152 m.

Chưa làm, xếp theo giá trị:
- **Mobile**: 4 render target đều R32F và pass thickness blend cộng vào R32F.
  ES 3.0/3.1 báo `INVALID_OPERATION` khi blend với draw buffer float 32-bit
  (cần `EXT_float_blend`), và lọc LINEAR trên float 32-bit cần
  `OES_texture_float_linear`. Bộ lọc separable chỉ lấy mẫu đúng tâm texel nên
  NEAREST là đủ → đường ra: linear view distance trong R16F/RGBA8-packed +
  NEAREST, thickness sang unorm additive, capture nửa độ phân giải, 1 vòng lọc.
- `fluid_capture_particle.fs` vừa ghi `gl_FragDepth` vừa `discard` → tắt early-Z
  trên Mali (late-ZS), với overdraw rất nặng.
- `fluid_depth_narrow_range.fs` thực chất đang là bilateral Gaussian, không phải
  narrow-range: nó hạ trọng số mẫu ngoài dải thay vì KẸP vào `[z-r, z+r]` như
  bài báo — đúng cái gây bướu hình hạt còn thấy trên bề mặt.
- `s_materialBody` là static toàn cục → hai chất lỏng khác màu trong một frame
  sẽ giành nhau. Cần material theo từng stream nếu muốn SSF phục vụ lửa/thuỷ
  ngân/độc.

## SSF: hạt GPU mất `v_life` — capture bị discard âm thầm (2026-08-11)

`core/particles/shaders/gpu/fluid_surface_capture.vs` khai báo 3 output, trong khi
CẢ HAI fragment shader ghép với nó (`fluid_capture_particle.fs`,
`fluid_surface_thickness.fs`) mở đầu bằng `if (v_life <= 0.0) discard;`. GL nối
varying theo TÊN và để input không khớp ở trạng thái *undefined* chứ không báo lỗi
link; rlvk cũng vậy (hạ xuống biến Private). Nghĩa là toàn bộ depth + thickness của
hạt chạy trên GPU backend phụ thuộc vào một giá trị rác — không log, không lỗi.
Đây là ứng viên hàng đầu cho việc mặt nước không bao giờ liền khối mà luôn lỗ chỗ
theo từng splat.

Đã thêm `v_life = life_data.x` (life còn lại, khớp ngữ nghĩa của
`fluid_pbd_surface.vs`) và guard `core/tests/shader_stage_interface_test.c` — guard
này FAIL 2 lỗi trên shader trước khi sửa, PASS sau khi sửa.

Chưa xác nhận bằng mắt: cần chạy sandbox NEW FX → WATER ORB. Nếu mặt nước vẫn trắng
bạc sau bản sửa này thì nghi vấn tiếp theo là đường CPU (`FLUID_SURFACE_MAX_PARTICLES
= 384` trong khi water orb phát 2.000 hạt → chỉ 384 hạt đầu vào được capture, phần
còn lại biến mất) chứ không phải phần quang học.

## Đã tìm ra vì sao nước mất độ trong: b03b7b6 (2026-08-11)

Người dùng nhớ đúng — ở `0237c80` (05/08) fluid còn ổn. `b03b7b6` (10/08) rút bỏ hai
lớp VFX tách rời; từ đó `ScreenDistort_BeginVFXBody()` bind thẳng `renderTex`, đúng
cái texture mà `ScreenDistort_GetSceneTexture()` trả về. `FluidSurface_Composite()`
chạy bên trong pass đó, nên nó **lấy mẫu chính colour attachment đang ghi** — undefined
trong GL, read/write hazard trong Vulkan. Tap khúc xạ chết ⇒ chỉ còn các số hạng đục
của shader (in-scatter + specular) ⇒ vỏ nhựa.

Sửa: `FluidSurface_Capture()` chụp một bản sao scene riêng (đúng format, giữ HDR) rồi
composite lấy mẫu bản sao. Không hồi sinh lớp đã bị rút, không đổi thứ tự trong main.c.
Chi phí: một lần copy toàn màn hình, chỉ ở frame có fluid.

Guard: `core/tests/fluid_refraction_source_test.c` — FAIL 4 lỗi trên mã trước khi sửa,
PASS sau khi sửa. Nó kiểm tra ĐIỀU KIỆN hazard trước, nên nếu sau này khôi phục lớp
riêng thì yêu cầu tự hết hiệu lực.

Chưa xác nhận bằng mắt (Core không có tầng visual tự động). Hai chỉnh quang học trước
đó — `min` thay `max` khi gộp depthGap, và knee của `DecodeOpticalThickness` — vẫn giữ;
mỗi cái một dòng, dễ dial lại sau khi nhìn thấy kết quả thật.

## Biên tưa: hai bản sửa ĐÃ THỬ và ĐÃ HOÀN TÁC (2026-08-11)

Trạng thái hiện tại = commit `279a115`. Vân đường-đồng-mức đã hết; còn lại **một
chút sọc ở biên, phải nhìn rất kĩ mới thấy** — đây là mức được chấp nhận, không
phải việc còn dở.

Đã đo được (bằng `WUXING_FLUID_DEBUG`, xem `fluid_surface.c`): capture **sạch**
(chế độ 12 không sọc), và mỗi pass lọc kéo bề mặt **theo đúng trục của nó** (chế
độ 14 chỉ chạy pass ngang → sọc chuyển từ dọc sang ngang). Nguyên nhân đúng như
vậy: ở gần silhouette phía ngoài không còn bề mặt, nên phép gom mẫu **một phía**
kéo kết quả về phía thân khối.

**Hai bản sửa đã thử, cả hai làm SỌC NHIỀU HƠN, đã hoàn tác — đừng thử lại:**

1. Gom theo cặp, `break` khi một phía hết bề mặt. Đúng về mặt đối xứng (guard đo
   được: lệch một phía +0.1176 m → theo cặp +0.0000 m), nhưng trong một trường
   splat **thưa** thì bất kỳ lỗ nhỏ nào bên trong cũng chặn vòng lặp gần như
   ngay lập tức, nên bộ lọc gần như không làm mịn ở đúng những chỗ cần nhất.
2. Hole fill chỉ lấp khi bị bao quanh (`surfaceCount > 13` trên 24 hàng xóm).
   Tách đúng "lỗ" khỏi "rìa" về mặt hình học, nhưng ngưỡng đó quá chặt với thân
   khối thưa: nhiều pixel lỗ **bên trong** cũng không đủ 14 hàng xóm nên thôi
   được lấp, để lại nhiều khoảng trống hơn trước.

Cả hai đều **giảm lượng làm mịn đúng ở nơi cần nó nhất**. Muốn khử nốt phần dư
này thì phải đổi kiểu lọc (lọc 2D thật, hoặc curvature-flow) chứ không vá được
trong khuôn khổ separable — và đó là việc đắt, cân nhắc cùng lúc với đợt hiệu năng.

Thiết bị đo `u_debugView` vẫn còn trong `fluid_surface.fs` và `fluid_surface.c`
(mặc định tắt). Gỡ khi nào chốt là không đụng vào biên nữa.

## Dọn dẹp thiết bị đo + nguyên nhân cái lỗ vòng xuyến (2026-08-11)

Đã gỡ hết đồ đo tạm: `u_debugView` (11 chế độ màu trong `fluid_surface.fs` + phần
nối ở `fluid_surface.c`), ba chế độ pipeline 12/13/14, và `WUXING_PBD_FREEZE`.
Chúng đã trả lời xong câu hỏi của mình — vân = caustic, chấm vuông = cell hash,
sọc biên = lấy mẫu một phía, và chi phí solver = overhead mỗi dispatch — nên theo
quy tắc của module thì chúng phải đi.

**Cái lỗ vòng xuyến, người dùng chẩn ra bằng mắt**: hạt quá to so với bán kính
vòng. Tôi tính lỗ hình học 1.23 m và tưởng thế là đủ lớn để thấy, nhưng quên rằng
bề mặt còn được *dựng lại* — mỗi kernel nở thêm 0.095 bán kính vòng, rồi bộ lọc
bắc cầu nốt phần còn lại. Sửa là chuyện **tỉ lệ**, không phải kích thước: tỉ lệ
phủ bất biến theo tỉ lệ nên phóng to vòng không giải quyết gì. Làm mỏng CẢ HAI
(tube 0.12, kernel 0.070) giữ nguyên độ phủ 3.3/2.3/1.5 mà mở lỗ từ 1.23 lên
1.46 m. Số đã có sẵn trong `water_ring_coverage_test.c`; chưa áp vì nó đổi hình
ảnh và cần một lần chạy để nhìn.

## Sọc biên: điều kiện biên thứ ba, lần này giữ nguyên lượng lọc (2026-08-11)

Hai lần trước hỏng vì cùng một lý do — **giảm lượng lọc thay vì giảm thiên lệch**.
Lần này mẫu thiếu ở phía ngoài **đóng góp giá trị dự đoán theo mặt tiếp tuyến**,
nên cặp mẫu vẫn đối xứng (không lệch) mà kernel vẫn đủ bề rộng (không mất mịn).

Đo trong `core/tests/fluid_depth_filter_test.c`, biên còn 3 texel bề mặt một phía:

| cách xử lý | thiên lệch | số tap |
|---|---|---|
| bỏ mẫu (cũ) | **+0.1631 m** | 32 |
| `break` (đã thử, tệ hơn) | 0.0000 m | **7** |
| dự đoán (hiện tại) | 0.0000 m | **57** |

Cột "số tap" là cột giải thích vì sao `break` hỏng: nó cắt kernel còn một phần
tư trong trường splat thưa, vì mọi lỗ bên trong cũng chặn vòng lặp. Test khoá cả
ba: bỏ mẫu phải lệch, hai cách kia không được lệch, và chỉ cách dự đoán mới được
phép giữ nhiều tap hơn cách bỏ mẫu. Sâu trong thân khối cả ba phải cho **kết quả
và số tap giống hệt nhau** — điều kiện biên chỉ được đổi BIÊN.

## Sọc biên: hoá ra không phải thiên lệch mà là CƯỜNG ĐỘ lọc (2026-08-11)

Bốn điều kiện biên đã thử, và phép đo phân biệt chúng đã đổi hai lần vì tôi đo
sai thứ:

| cách | thiên lệch | số tap | bướu 5 cm còn sót ở biên |
|---|---|---|---|
| bỏ mẫu | +0.1631 m | 32 | — |
| `break` | 0 | 7 | — (gần như không lọc) |
| dự đoán tiếp tuyến | 0 | 57 | **0.02134** |
| **phản chiếu chẵn** | 0 | 57 | **0.00149** = đúng bằng ruột |

Ba cách đầu đều nhắm vào **thiên lệch**. Nhưng debug view 1 cho thấy ruột mượt
hoàn hảo còn viền ngoài vẫn sọc, tức thiên lệch đã hết mà sọc vẫn còn — thứ còn
lại là **cường độ**: pixel gần mép có một phần mẫu là bịa, mẫu bịa mang chính giá
trị TÂM nên nó kéo về phía bướu, làm mép được làm mịn yếu hơn ruột. Mức yếu ấy
biến thiên theo hình dáng mép ⇒ vạch song song trục pass.

Phản chiếu chẵn (mẫu thiếu lấy giá trị THẬT của phía đối diện) giữ cả hai tính
chất cùng lúc: đối xứng nên không lệch, và dữ liệu thật nên không yếu. Đây là
điều kiện biên tiêu chuẩn của bộ lọc ảnh, và đáng ra phải là lựa chọn đầu tiên.

Test giờ đo **bướu còn sót ở biên so với ở ruột**, không chỉ đo thiên lệch — đó
mới là đại lượng quyết định, và nó bắt được cách "dự đoán" trong khi phép đo cũ
cho nó điểm tuyệt đối.

## Bộ lọc mặt: cài lại theo bài báo gốc thay vì tự nghĩ (2026-08-11)

Sau **năm** điều kiện biên tự nghĩ (bỏ mẫu, `break`, kẹp theo tiếp tuyến, dự đoán
tiếp tuyến, phản chiếu chẵn) mà sọc biên vẫn còn, đã tra mã tham chiếu của chính
tác giả narrow-range filter (Truong & Yuksel 2018, `ttnghia/RealTimeFluidRendering`,
`Shaders/filter-narrow-range.fs.glsl`) — **cài lại thuật toán từ bài báo, không
chép mã** (mã đó ghi "All rights reserved").

Hai thứ bản gốc làm mà tôi không nghĩ ra:

1. **`FIX_OTHER_WEIGHT`** — mẫu là nền thì đặt trọng số 0 cho **cả mẫu đối xứng
   bên kia**. Đây là điều tôi loay hoay suốt: bỏ một phía thì lệch; bịa dữ liệu
   thay vào thì **thêm** trọng số mang giá trị TÂM, pha loãng phép làm phẳng. Bỏ
   cả cặp là **rút** trọng số ra, nên phần còn lại vẫn làm phẳng đúng cường độ
   tương đối sau khi chia `sum/wsum`.
2. **`RANGE_EXTENSION`** — dải trên/dưới tự nới theo mẫu hợp lệ. Nó thay toàn bộ
   bộ máy ước lượng độ dốc + dự đoán tiếp tuyến mà tôi tự dựng, và không có tham
   số nào phải chỉnh.

Đã xoá: `AccumulateSample` tự viết, ước lượng độ dốc `trust`, kẹp quanh mặt tiếp
tuyến, uniform `u_depthRange` (giờ không ai đọc). Giữ lại hai thứ của mình vì bản
gốc không có và chúng sửa lỗi thật: **sigma liên tục theo độ sâu** (bản gốc dùng
`filterSize` nguyên rồi `sigma = filterSize/3`, tức vẫn nhảy bậc → đường đồng
mức) và **trần bán kính theo tier**.

**Điều đáng ghi nhất là về phép đo, không phải về mã.** Test cũ khẳng định "bướu
còn sót ở biên phải BẰNG ruột". Đó **không phải** tính chất của phương pháp —
kernel ở biên co lại thật — và chính khẳng định sai đó đã đẩy tôi tới phản chiếu
chẵn, thứ trông tệ hơn hẳn trên màn hình. Test giờ chỉ khẳng định những gì thuật
toán thật sự bảo đảm: **không lệch** (trọng số bị rút chứ không được thêm), và
**lỗ hổng chỉ làm câm vòng của nó, không kết thúc cả dãy** (51 tap vs 7 nếu
`break`) — đúng thứ khiến trường splat thưa sống được.

## ĐÍNH CHÍNH: PBD **không** phải desktop-only (2026-08-11)

Commit `87f77e3` viết rằng đường PBD chỉ chạy desktop vì shader là `#version 430`
+ SSBO nên `FluidPBDGPU_Init()` sẽ fail trên bản Android GLES 3.0. **Sai**, và
sai vì tôi tin một ghi nhớ cũ thay vì đọc doc:

- `third_party/vulkan/docs/PROGRESS.md:170` ghi rõ rlvk **đã chạy trên Android/Mali
  thật** từ 2026-07-17, phần platform glue đã landed. Người dùng cũng đã tự test.
- rlvk biên dịch GLSL qua **shaderc → SPIR-V** (`rlvk_shaderc.inl`), không có cổng
  chặn phiên bản. `#version 430 core` compute chạy trên mọi thiết bị Vulkan.

Nên phần so sánh PBD vs force field cũng phải sửa: **ràng buộc "PBD không chạy
được trên Android" không tồn tại**. Cái còn lại là chi phí, và chi phí đó bị chi
phối bởi overhead mỗi dispatch (đã đo, đã gộp batch trên desktop) — trên driver
mobile thì phải đo riêng, chưa ai đo.

Những lo ngại Android CÒN đứng vững, đều là chuyện Vulkan chứ không phải GLES:

- `R32_SFLOAT` chỉ bảo đảm SAMPLED_IMAGE + COLOR_ATTACHMENT theo bảng Mandatory
  Format Support; **BLEND và FILTER_LINEAR là tuỳ chọn**. rlvk giờ có
  `rlvkFormatSupports*()` và runtime test in ra kết quả — một lần chạy trên máy
  là biết. Riêng FILTER_LINEAR gần như vô hại vì bộ lọc separable lấy mẫu đúng
  tâm texel, NEAREST mới là đúng ở đó.
- `gl_FragDepth` + `discard` trong pass capture tắt early-Z trên Mali. Đây là
  kiến trúc phần cứng, không phụ thuộc backend — vẫn là vấn đề **hiệu năng** thật.

## Dual-depth thickness + anisotropic splats (2026-08-12)

Executed `core/docs/HANDOFF_SSF_UPGRADE.md`. Both items landed; the accumulation
path is deleted.

### 1. Thickness is now measured, not accumulated

`T = z_back − z_front`. A second capture pass rasterizes the FAR root of every
splat into its own R32F depth target, `fluid_thickness_resolve.fs` subtracts the
two in metres, and a plain separable Gaussian smooths the result (Green, GDC
2010 — thickness is low-frequency and wants no bilateral filter). New shaders:
`fluid_capture_particle_back.fs`, `fluid_capture_back.fs`,
`fluid_capture_cpu_back.fs`, `fluid_thickness_resolve.fs`,
`fluid_thickness_blur.fs`.

**The MAX reduction without a MAX blend.** The back surface needs the farthest
fragment, and the depth test keeps the nearest. Writing `gl_FragDepth = 1 - depth`
inverts the reduction using nothing but an ordinary depth test — which matters,
because a `GL_MAX` blend equation on R32F is optional (rlvk detects it as
`Caps.floatBlendR32`) and rlgl exposes no depth-func setter at all. The two
targets therefore clear in OPPOSITE directions: front to 1, back to 0.

**Constants deleted.** `FLUID_KERNEL_OVERLAP 1.5` and the `exp(-p/1.20)` knee are
gone, along with the absorption scale that had been re-tuned twice to compensate
for them. Absorption is now one statement: one `FLUID_REFERENCE_DEPTH_M` (0.20 m)
of liquid transmits exactly `materialTransmission`. 0.20 m is measured, not
picked — the water ring's tube is 0.216 m across by construction and the PBD
crown read 0.16–0.25 m under the thickness ruler.

**The shell question, decided by measurement.** A ruler debug view (1 cm stripes
over the decoded thickness) was built BEFORE any change and run on both fixtures:

| fixture | accumulation | dual depth | ground truth |
|---|---|---|---|
| WATER RING (hollow tube) | ~10–12 cm, blotchy at splat scale | 16–21 cm, smooth | 21.6 cm tube diameter |
| FLUID IMPACT (dense crown) | ~10 cm, blotchy | 16–25 cm | — |

Dual depth does inflate the ring to its full tube diameter, exactly as the
handoff predicted — and it looks BETTER, because the tube finally carries an
interior gradient instead of one flat wash. It won on both fixtures at HIGH and
LOW, so the accumulation path was deleted rather than kept behind an emitter flag.

Also measured, and worth recording because it contradicted the handoff: the
decode was **not** saturating any more. Zero pixels sat at the 0.16 m cap on
either fixture. The earlier knee move to 1.20 had already fixed that; what was
actually wrong was that an accumulated sum is blotchy at splat scale and is not a
length.

### 2. Velocity-aligned anisotropic splats

Splats are ellipsoids stretched along the view-plane velocity, capped at 3:1, with
both cross-axes shrunk by `1/sqrt(aspect)` so the volume is unchanged (Yu & Turk's
determinant normalization; the velocity proxy for the PCA is not theirs and the
shader says so). Both vertex stages (`fluid_surface_capture.vs`,
`fluid_pbd_surface.vs`) and both fragment stages changed together — a stretched
quad over an unstretched reconstruction draws a clipped circle. The deleted
accumulation pass would have needed its chord length stretched by hand to match;
dual depth gets it for free, since both roots come from the same kernel.

The crown rim went from a row of scalloped blobs to a continuous sheet, and the
vertical banding across the crown dropped sharply. `FluidSurface_RegisterEllipsoid`
now honours its three radii too (a unit sphere under `rlScalef`) — it had been
averaging them into one scalar, so the signature promised anisotropy the renderer
never drew.

### Verification

`glslangValidator -S frag/vert` on every edited shader. Core suite 52/56 — the
four reds (`energy_burst_semantic_layers`, `tube_frame`, `vfx_layered_field_contract`,
`volume_trail`) were red before this work and are untouched by it. New guards:
`core/tests/fluid_dual_depth_test.c`, `core/tests/fluid_anisotropic_splat_test.c`.
`fluid_surface_optics_test.c` lost its decode mirror and the "densest body must
sit clear of the cap" guard, which existed only because the decode saturated.
Visual: both fixtures, both tiers, via `--render-vfx 41` / `--render-vfx 38`.

All temporary instrumentation is out: `u_debugView` (views 1 and 2, and the
host-side mode 12), `WUXING_FLUID_DEBUG`, `WUXING_FLUID_THICKNESS`,
`WUXING_FLUID_ANISO`, and the two view knobs added to drive the headless
capture (`WUXING_VFX_CAMDIST` in `main.c`, `WUXING_GFX_TIER` in `gfx_quality.c`).

### Two things this session did NOT exercise

- **The CPU/VBO particle fallback** (`fluid_capture_cpu_back.fs`) is wired and
  compiles, but this machine takes the compute path, so its back capture has
  never actually run. It is the GL-3.3-only route.
- **Anisotropy is compute-path only.** The CPU fallback still builds its billboard
  quad host-side and draws isotropic splats; making it match needs host work, not
  a shader edit. Both paths still agree on thickness, which is what matters for
  the optics.

### Still parked (carried over from the handoff, reasons unchanged)

- **Half-resolution + bilateral upsample** — ceiling ~0.6 ms; measure with
  `perf_ssf_filter` (written, never run) first.
- **Temporal accumulation** — nobody has observed the surface boiling.
- **Curvature flow** — the streaks are sub-visible.
- **Narrow-band SSFR (CGF 2022)** — wrong particle-count regime.
- **Full PCA anisotropic kernels** — needs the neighbour search the architecture
  avoids.
- **One Android run** — still answers three questions at once (R32F blend, R32F
  linear filtering, Mali early-Z cost of `gl_FragDepth` + `discard`). The back
  capture pass adds a SECOND `gl_FragDepth` pass, so the third question now
  matters more than it did.

## Bright streaks on the ring: specular AA landed, foam line is a waterline (2026-08-12)

Follow-up to the dual-depth work, on the user's report of bright streaks at the
water ring's edge, and their question of whether it was Fresnel or a back-surface
highlight. Neither: measured with a debug view that isolates the composite's
additive terms.

**Fixed — sun specular.** Both lobes were producing it: the GGX BRDF a razor
needle, `sharpGlint` a soft cigar. Now filtered by normal variance (Kaplanyan et
al. 2016 / Filament's `normalFiltering`), with the two un-normalized Blinn lobes
energy-corrected by `(n+2)` so widening redistributes instead of adding — the
first attempt skipped that and made the highlight bigger and brighter (25x the
energy at the variance ceiling). Guard: `core/tests/fluid_specular_aa_test.c`.

**Not fixed, because it is not broken — the foam line.** The remaining white
fringe on the silhouette is `shoreline` foam, and the ring genuinely intersects
the ground: a back-depth probe showed the splat cloud reaching past the receiver
over nearly the whole tube, which is what a torus centred at y = 0 with a 0.108 m
tube does. It only became visible because dual-depth steepened the thickness
gradient the term is gated on. Three mechanism fixes were tried and reverted; see
`core/docs/LANDMINES.md`. If the line is unwanted it is an authoring or a
strength decision, not a correctness one:
  - raise the ring so it does not sit half in the ground (`skills`/composition),
  - or scale the shoreline foam down in `fluid_surface.fs`.

Also worth knowing for any future A/B here: **consecutive runs of the same
fixture are not identical** (`cmp` on two `--render-vfx 41 --warmup 60` captures
differs). Silhouette-scale detail must be compared across several runs before it
is attributed to a change — a vertical-striping "regression" was chased once and
turned out to move between runs.

### Follow-up: the stripes were a plane wave (2026-08-12)

The user, looking at the ring from further out, described "alternating bright and
dark stripes, like optical interference". That is a different defect from the
specular streaks fixed above, and the probe run found it in one build:
`surfaceNoise` was `sin(dot(worldPosition, k))` — rendering the scalar on its own
showed parallel bands painted across the tube. It fed roughness, the sharp-glint
gate and the foam pattern together, which is why the bands, the white dashes and
the "streaks up close" were all one thing.

Replaced with `fbm3` at the same 0.37 m feature scale. `vnoise3` and `fbm3` are
new in `core/shaders/common/noise.glsl` (it only had 2D value noise), built on
the existing Mali-safe `hash3`. Guard:
`core/tests/fluid_surface_noise_test.c`.

Ruled out by the same probe, so do not re-investigate: the wave perturbation
(`WaterMultiOctaveWaves`) and its capillary octave were disabled independently
and the banding stayed.

## SSF frame cost, measured (2026-08-12)

The handoff recorded fluid at ~1.7 ms of a 16.7 ms frame. That is not what this
measures, and the gap is large enough to act on.

**Method.** GPU timestamps are unusable here (`RLVK_GPU_TRACE` returns 0.000 ms on
MoltenVK — see `third_party/vulkan/docs/LANDMINES.md`), so: wall clock sampled
INSIDE the render loop, after a 60-frame warm-up, over 340 frames, with the
configurations interleaved and the minimum taken. `SetTargetFPS(60)` also had to
stop applying to `--render-vfx`, which had been pacing every headless capture
frame to 16.7 ms; `autoTestMode` and `visualVerifyMode` were already exempt.
Fixture: NEW FX 41 (WATER RING) at 1280x720, the ring filling much of the frame.
The machine was under concurrent load; deltas below ~1.5 ms are not resolvable.

**Where the time went (before this session's change):**

| block | cost |
|---|---|
| back capture + thickness resolve | 0.5 ms |
| two Gaussian passes over thickness, native, r=10 | 3.3 ms |
| the rest of SSF (capture, narrow-range filter, composite) | ~5.4 ms |
| **total SSF** | **~9 ms** |

So dual-depth's *measurement* is nearly free and the *smoothing* was the whole
bill — which is the good case, because thickness is a low-frequency quantity with
no silhouettes to preserve. It now runs at half the surface resolution (blur radii
halved with it, so the world-space reach is unchanged), and the resolve renders
straight into the half-res target. Visually indistinguishable on both fixtures;
the dual-depth block's cost drops into the noise floor.

**Also measured, and it retires a parked question:** `perf_ssf_filter`, uncapped,
three runs — halving the resolution saves 1.2–2.0 ms over 8 passes, while tripling
the kernel width (adaptive radius 13 -> 32 taps per side, a real 2.5x) changes
nothing measurable (-0.27 / +0.73 / -1.34 ms).

The reading to take from that is narrower than "the filter is pixel-bound".
The benchmark's blob covers ~12% of the frame and every other pixel early-outs on
`centerDevice >= 0.99999`, so taps only ever apply to that 12% while resolution
applies to all of it — the clears, the blit, and the render-pass bandwidth
included. What the pair of numbers actually says is that for a body of this
screen coverage the cost lives in the passes' FULL-TARGET work, not in the
filtering of the covered region.

Which points at a cheaper lever than half resolution, and one nobody has tried:
**scissor the filter passes to the fluid's screen bounds.** The CPU already knows
them for the CPU-registered particles and could get a conservative box for the GPU
streams. Half resolution is still worth its measured ~0.6–1.0 ms for the game's 4
passes, and the two compose.

**Still open:** the remaining ~5.4 ms is the depth capture, the narrow-range
filter and the composite, unattributed between them. The composite is one
full-screen pass doing SSR, refraction, four lights and foam, so it is not
obviously small.

## Per-stage SSF attribution: NOT ACHIEVED on this host (2026-08-12)

The plan was attribute → scissor → half-res depth. It stopped at the first step,
and the reason is worth more than the attempt was.

**Wall-clock frame time cannot resolve a 0.5–2 ms stage here.** Three successive
methods were tried and each failed in a way the previous one hid:

| method | what went wrong |
|---|---|
| separate processes per configuration, slope of two run lengths | machine drift between processes exceeded the effect; "SSF off" measured SLOWER than "SSF on" |
| separate processes, timed inside the render loop | drift across rounds (baseline 21 -> 30 ms over three rounds) larger than every stage |
| one process, stages interleaved frame by frame in a fixed rotation | the GPU is pipelined, so a frame's cost lands partly in the NEXT frame's sample; a fixed rotation puts that bias on one bucket every time |
| one process, interleaved with a RANDOM variant per frame | two runs of the same build: baseline 34.3 then 29.4 ms, and every delta flipped sign — adding work measured as faster |

`RLVK_GPU_TRACE` would have sidestepped all of it and it does not work on this
host (see the rlvk landmine: MoltenVK reports the queries available and returns
zeros).

**Two hypotheses were tested anyway, before the numbers were known to be
worthless, and both are recorded so they are not retried on faith:**

- **Scissoring the screen-space passes to the fluid's extent measured 0.72 ms
  SLOWER**, not faster. The empty pixels are already nearly free — the filter
  early-outs after one fetch and the composite discards after one — while
  `ClearBackground` is a render-pass `loadOp CLEAR` that covers the whole target
  and which scissor does not shrink at all.
- Which suggested **dropping the redundant clears** (the filter, blur and resolve
  passes assign `finalColor` on every path, so the clear before them is dead
  work). Not applied: omitting the clear makes rlvk use `loadOp LOAD`, a
  full-target READ, which is plausibly worse on a tiler — and with no working
  measurement there is no way to tell. It is the best remaining candidate the
  moment a real timer exists.

**What a real measurement needs:** a quiet machine, or Instruments / Xcode Metal
frame capture on macOS. The one number this session produced that reproduced
across runs came from `perf_ssf_filter` — single process, uncapped, randomized
variant, hundreds of samples per bucket — which is the shape any future in-game
harness should copy.

## Silhouette antialiasing (2026-08-12)

The most visible defect left on the surface, found by looking at the ring's outer
rim at 7x: a hard one-pixel staircase, no antialiasing at all.

Fixed by dilating the depth mask one texel and deriving the fringe's alpha from
the mask neighbourhood rather than from thickness — see the landmine for why the
thickness route was tried first and only moved the cliff. Straight-edge ramp is
now 1.0 → 0.875 → 0.25 → 0. Verified on WATER RING (the rim is a two-pixel
gradient where it was a cut) and FLUID IMPACT (unchanged, its droplet boundary was
already soft). Guard: `core/tests/fluid_silhouette_coverage_test.c`.

**Not verified at LOW tier.** The game build is currently blocked by unrelated
in-flight work — `scripts/validate_vfx_surface_registry.py` fails at CMake
configure on `fire_tongue` (missing texture) and `fire_puff` (duplicate asset) —
so the temporary tier override could not be rebuilt. The change is
resolution-neutral by construction (offsets are `u_texel`, the one absolute is a
0.2 mm thickness floor), but that is an argument, not an observation.

### Remaining, in the order I would take them

1. **PCA anisotropic kernels for the PBD crown.** Yu & Turk proper, replacing the
   velocity proxy. Reachable now: `fluid_pbd_gpu.comp` already maintains a
   neighbour grid (`heads[]`/`next[]`, 32^3 cells, rebuilt every Jacobi pass), so a
   covariance accumulation rides along the loop the density constraint already
   walks. Applies to FLUID IMPACT only — the ring's particles come from the
   force-field path, which has no neighbour search.
2. **Vertical striations** inside the crown and along the rim. Known-hard: five
   boundary conditions were tried in a previous session and all reverted; the
   conclusion on record is that it needs a different filter class (true 2D, or
   curvature flow), not another patch to the separable one.
3. **The half-sunk ring's waterline** — an authoring call, not a defect.
4. **`capFade`** in `WaterMultiOctaveWaves` is still a plane wave. Amplitude 0.004
   and disabling the whole term changed nothing visible; swap it to `fbm3` when
   something else touches that function.

## Stripes: the true 2D narrow-range kernel (2026-08-12)

The user reported stripes in BOTH axes. Researched before touching anything, then
measured: running the horizontal pass alone smears the reconstructed normal into
horizontal ribbons, the vertical pass alone into vertical ones. Classic separable
artifact — neither pass sees a diagonal.

The filter was a faithful `filter1D` from the paper, run twice. Truong & Yuksel's
reference implementation ships `filter2D` behind a switch precisely because the
separation is an approximation; that variant is now implemented here (same four
rules — pair rejection, clamp-not-reject below the lower bound, range extension,
per-direction bounds — over a disc walked as point-symmetric pairs, outward from
the centre so the range extension stays a walk).

**Result, both fixtures:** the striations beside the ring's rims are gone and the
tube reads as an evenly rounded torus instead of a faceted one; the crown's
interior striations are gone too. This one is visible at normal zoom, unlike the
silhouette-AA change before it.

**Cost:** ~+1-2 ms at HIGH by wall clock (unreliable on this machine, so treat it
as an order of magnitude, not a figure). 529 taps at radius 13 against the
separable pair's 52 — but the pass count halves, which is why it is not the 10x
the tap ratio implies. Two rounds, not one: a single 2D round leaves the tube
visibly lumpy.

**HIGH only.** MED is the Android default and a several-hundred-tap dependent-fetch
loop on a Mali tiler cannot be judged from here.

### Still open after this

1. **PCA anisotropic kernels for the PBD crown** (`fluid_pbd_gpu.comp` already has
   the neighbour grid).
2. **The half-sunk ring's waterline** — an authoring call.
3. **`capFade`** is still a plane wave; amplitude 0.004, invisible so far.
4. **2D on mobile** — needs one device run to decide.

### Close-range regression from the 2D kernel, and the cap that fixed it (2026-08-12)

The 2D filter shipped with a 3.6x frame-cost regression at close range that the
default-framing measurement could not see. Reproduced with the camera distance as
the variable:

| camera distance | 2D, uncapped | separable |
|---|---|---|
| 6.0 (the framing everything else was measured at) | 18.9 ms / 53 fps | 20.0 ms / 50 fps |
| 2.0 | **111.9 ms / 9 fps** | 30.9 ms / 32 fps |
| 1.2 | 116.7 ms / 9 fps | 32.2 ms / 31 fps |

Cause and the two candidate fixes are in `core/docs/LANDMINES.md`. The one that
stands is a hard reach cap of 10 texels on the 2D disc (317 taps, whatever the
adaptive radius asks for): **31.4 ms / 32 fps at distance 2.0, level with the
separable path it replaced, with the striping gone.** Subsampling the disc was
measured faster still (26.8 ms) and reverted — it made the sampling lattice
visible as a dot grid.

### Why the water ring costs more than the PBD crown, with half the particles (2026-08-12)

Asked because the ring has no PBD solver and is still the slower of the two.
Measured at the default `--render-vfx` framing, 1280x720, HIGH:

| fixture | frame | SSF share |
|---|---|---|
| no fluid (NEW FX 0, BEAM) | 9.8 ms / 102 fps | — |
| FLUID IMPACT | 16.6 ms @ frame 100, **14.6 @ frame 200** | ~6.8 ms |
| WATER RING | 20.1 ms, flat | ~10.3 ms |

Two things to know before comparing them by eye. The crown **decays** —
`FLUID_PBD_LIFETIME` is 2.50 s, so after ~150 frames it is measuring an empty
scene, while the ring re-emits every frame and never gets cheaper. And the
particle counts run the opposite way to the cost:

| | particles (HIGH) | splat radius | total splat area |
|---|---|---|---|
| FLUID IMPACT | 2048 (`fluid_pbd_gpu.c:92`) | 1.75 cm | 1.97 m² |
| WATER RING | 1000 (`water_ring.inl:124`) | 6.30 cm | **12.47 m²** |

The ring has **half** the particles and **6.3x** the splat area, because its
kernel is `ringRadius * 0.070` = 6.3 cm against the crown's
`0.0085 * visualScale(~2.06)` = 1.75 cm. It also covers 3.2x more of the screen
(2.48% against 0.77%, measured off the captures).

**Particle count is the wrong cost metric for SSF; splat AREA is.** Every splat
fragment is shaded twice (front and back capture) and both passes write
`gl_FragDepth`, which disables early-Z, so none of that overdraw is rejected
cheaply.

Note the cost ratio (1.5x) is much smaller than the area ratio (6.3x): most of
SSF is per-frame fixed work — the filter, the thickness blur and the composite are
full-screen passes both fixtures pay in full. That is the same conclusion the
stage attribution reached, from the other direction.

## 2026-08-12 — SSF liquid materials + cost gates (Renderer/rlvk persona)

All five items of `core/docs/HANDOFF_SSF_MATERIALS.md` executed; that file is now
the record of what landed, what it cost, and what is left. Summary:

- **Liquid table** (`FluidLiquidDesc`, 4 content-addressed slots, LRU eviction)
  with a **per-pixel material id** rasterized into the front capture's `.b`
  channel — the front target is now RGBA32F. Measured free (see the handoff's
  interleaved A/B). This is what lets two liquids of different colours share a
  frame; before it, `FluidSurface_SetMaterialColors` was global.
- **Three optical classes**: dielectric (water, poison), emissive (lava —
  thickness-driven blackbody emission plus a crust that subtracts it), conductor
  (liquid metal — coloured F0, no transmission). Water's IOR is no longer
  hardcoded in three places.
- **Cost gates** (`FluidSurface_RequestBody`): priority ownership, projected-size
  cull, frame budget. Wired into `VFX_ComposeWaterRing` and
  `FluidImpact_SpawnWater`.
- **New fixture**: NEW FX -> **LIQUID BENCH** (40). NEW FX indices after it
  shifted — WATER RING is now **42**.

Two long-standing engine defects found by that fixture, both in the CPU ellipsoid
path (`FluidSurface_RegisterParticle`), both promoted to root
`ENGINE_LANDMINES.md`: `rlPushMatrix()` does not return an identity transform,
and the capture rasterized through a near=0.01 frustum the composite inverted as
near=1.0. Neither was observable before, because the path had no fixture.

New guards: `core/tests/fluid_capture_projection_test.c`,
`fluid_liquid_material_test.c`, `fluid_cost_gate_test.c`. Suite 59/63 — the same
four pre-existing failures as before this work.
## 2026-08-13 — Reusable lightning arc primitive

- Added `Ribbon_GenerateMidpointDisplacement` in `core/ribbon_strip.h/.c`: a
  reusable, allocation-free midpoint-subdivision path primitive (0–5 levels,
  exact endpoints, per-level amplitude decay, deterministic integer seed).
  It expands the caller's fixed array in place and is now the source of truth
  for lightning, roots, cracks, and other irregular paths.
- `VFX_LightningArc_Spawn` adapts the 32-slot `core/lightning/` stroke module
  for source-to-target casts. Its trunk uses 3–4 levels, branches use 3 only
  when requested. It deliberately creates no VFX light: the owning skill decides
  whether source and/or target contact lights belong to its gameplay beat.
- Its render path explicitly separates opaque coloured body (`VFXBody` + alpha)
  from cyan halo / near-white core (`VFXEmission` + additive), so the lightning
  retains its material hue over bright scenes instead of washing out.
- The initial wide-ribbon material was removed: it necessarily read as three
  parallel bands at an oblique camera. `core/lightning/lightning_stroke.c`
  instead renders one endpoint-pinned camera-facing canvas with a thin core and
  compact halo; the fragment FBM warps its centreline without exposing the
  canvas as a ribbon.
- Added `lightning_arc_contract_test`; focused Core test passes. Full visual
  verification remains the renderer suite / human game run.

### Arc readability retune

- Screenshot diagnosis: a 32-segment trunk with only 0.16 m displacement read
  as a cyan dotted cable at the isometric camera, then a wide profiled ribbon
  read as three parallel bands. Default arcs now use 8–16 primary kinks,
  0.80 m maximum lateral displacement. The final primary uses the reference
  ShaderToy shape: one endpoint-pinned camera-facing canvas, 4+3 octave FBM
  domain warp, then a bounded distance-to-centreline body/halo/core. Branches
  remain available but are opt-in by default.
- The corona was then reduced to a 1.12× line-width falloff with lower additive
  alpha, leaving the white ion channel and blue body intact while removing the
  fog-like outer glow.
- Follow-up render regression: the first dedicated implementation omitted its
  scoped backface-culling disable, so camera-facing immediate segment quads
  could all be culled. The renderer now owns the flushed culling guard.
- The reusable stroke now has a 100 ms source→target discharge phase. Its shader
  reveals only the travelled section and briefly intensifies the leading ion
  head, so a click cast reads as an electrical snap rather than a full beam
  appearing at once. The ion channel is HDR 4.5×, while the low-energy halo
  squares its SDF falloff rather than forming a separate cartoon-blue band.
- The opaque blue body was reduced to a close-in carrier beneath the white
  channel. The shared shader now tapers all layers roundly into exact endpoints
  and adds a compact endpoint contact glint, removing the broad inner blue band
  and squared canvas cuts.
- Follow-up review preserved the approved FBM silhouette and focused on the
  actual issue: colours looked like separate bands. The primary now uses a
  near-white HDR core, overlapping saturated-blue corona, pale cubic-fade outer
  field, and a very low-alpha pale body for bright-background hue retention.
- The near-core corona energy was increased without changing its radius or body
  alpha, making the contact field read stronger while preserving the smooth
  gradient into the outer field.

### Final lightning / HDR bloom pass

- The global HDR bloom bright-pass now gathers every 4×4 full-resolution source
  cell before thresholding its quarter-resolution target. This preserves a
  one-pixel custom-shader mesh core (lightning, rune, glint) that the old single
  sample could miss completely. Guard: `bloom_thin_emitter_contract_test`.
- Lightning now uses a cobalt-blue corona and a blue-white ion channel instead
  of the lightning material's purple-soft colour, preventing the cinematic
  highlight from grading into yellow/white while keeping the field smooth.
- `postImpactDuration` is the explicit animated hold after the source→target
  discharge. The default is 0.30 s after the 0.10 s travel; zero ends exactly
  on impact. During the hold the FBM phase accelerates and all layers fade
  coherently at the end, rather than holding a static completed beam.
- `LightningStroke_SmoothStep` is local C99 code, not the GLSL `smoothstep`;
  this keeps the hold fade portable under strict C compilation.

## 2026-08-13 — Moving lightning trail and Lightning Impact fixture

- Added `LightningStroke_SpawnPath` / `SetPath`: a reusable, bounded multi-point
  path renderer that maps one continuous ribbon carrier to arc-length UV then
  reuses the approved two-point lightning distance-field shader. It preserves
  the exact core/corona/field, travel, endpoint and HDR behaviour through a
  curved path without restarting a field per segment.
- The former segment-canvas `core/lightning/lightning_trail` renderer and the
  experimental Trail electric material path were removed from the lightning
  composition: neither could form a stable continuous halo.
- Added `VFX_LightningTrail_*` composition APIs plus the one-shot
  `VFX_ComposeLightningGroundRicochet` reference. Two short discharges launch
  from the impact point; every 105 ms a hop reaches its exact ground contact,
  dissipates, then a fresh hop begins there. The five hops are 0.67x shorter
  and 0.62x lower each time, avoiding the long projectile-hair silhouette. It
  has no implicit VFX light.
- The VFX tester fixture is **LIGHTNING IMPACT** and explicitly uses
  `VC_MAT_LIGHTNING`. `sync_vfx_test.py` now owns the material, label and
  category override, preventing broad word inference (`ground`/`ricochet`) from
  silently turning the visual into an Earth fixture on later syncs.
- Updated `lightning_trail_contract_test` for the multi-point stroke path,
  continuous-UV shader contract, stop lifecycle, decay envelope and tester
  wiring.

## Bloom pyramid overhaul (Đợt G — HDR bloom quality)

Goal: the glow read as "pasted on" rather than radiating. Root causes were
structural in `core/post_fx.c`, not authoring.

- **Pyramid 3 → 6 levels** (1/4 down to 1/128). The widest blur the old chain
  could make was one 1/16 texel across, which is why bright cores got a tight
  halo and no far bleed. Level count is resolved at init (`s_dfLevels`) and
  stops before a level is too small for the 13-tap footprint, so small windows
  and low-res mobile backbuffers degrade instead of allocating 0-sized targets.
- **Upsample now folds instead of overwriting.** `dst = mix(dst, tent(src),
  scatter)`, factor carried by `u_scatter` → fragment alpha → `BLEND_ALPHA`.
  Previously the chain discarded every level but the smallest.
- **13-tap Jimenez downsample + 3×3 tent upsample**, replacing a 4-tap box and
  a 4-tap cross. Required by the extra depth: the old kernels aliased into
  crawling dotted lines once a level's texel covered many screen pixels.
- **Hard energy clamp → soft ceiling.** `e * M / (M + e)`, default M = 12
  (was a hard cut at 4). Firefly control moved to a renormalised Karis weight
  on the first downsample only, which leaves large bright areas untouched.
  The 2.2 extraction gain is retained deliberately so mid-brightness bloom
  lands where it did before and only hot cores change.
- `CMakeLists.txt`: `bloom_downsample.fs` / `bloom_upsample.fs` had never been
  copied to the build dir — added.

New live knobs (tuning.cfg, all 0 = "use the built-in default"):
`bloom_threshold`, `bloom_knee`, `bloom_scatter`, `bloom_karis`,
`bloom_max_energy`, plus the existing `bloom_intensity`.

`PostFXConfig.bloomScatter` added (0 = engine default 0.65), so the only
existing initialiser in `main.c` keeps working untouched. `main.c`'s shipped
`bloomThreshold` was deliberately LEFT at 1.25 — see below.

Verification: `core/tests/bloom_pyramid_contract_test.c` (new, numeric +
shader-mirror), full core suite 66/70 with the same 4 pre-existing failures as
HEAD (verified in a clean worktree: `energy_burst_semantic_layers`,
`tube_frame`, `vfx_layered_field_contract`, `volume_trail`).
`glslangValidator -S frag` clean on all three bloom shaders. **No visual
confirmation** — the threshold/scatter/intensity trio has to be judged by eye
and is the reason those knobs are live-tunable rather than baked.

## Headless capture determinism (blocker for all visual verification)

Found while trying to sweep `bloom_intensity`: `--render-vfx` was not
reproducible, so the sweep measured run-to-run noise rather than the parameter.
Full analysis in `ENGINE_LANDMINES.md` ("A fixed timestep that only pins the
LOOP does not pin the SIMULATION").

- Added `TimeFX_SetRawDelta()` / `TimeFX_RawDelta()` (`core/time_fx.h`) — one
  publisher in `main.c` beside the existing headless pin, one accessor for every
  system that advances VFX state. Not hitstop-scaled, so no behaviour changes.
- Converted 10 call sites under `core/composition/` and 3 in
  `sandbox/vfx_test.c` (the tester drives every fixture, so its wall-clock dt
  desynchronised all of them at once).
- Left `GetFrameTime()` in place where wall clock is the correct input:
  `core/post_fx.c` perf sampling, `core/particles/particle_system.c` perf log,
  `core/fluid/fluid_surface.c` frame-budget gate.
- `core/tests/core_glow_test.c` and `converge_motes_test.c` asserted the old
  wall-clock expression as a source contract; updated.
- New guard `core/tests/frame_delta_determinism_test.c` — walks
  `core/composition/` recursively (51 sources) so a NEW file is covered without
  anyone maintaining a list, and asserts the perf/budget exceptions stay on wall
  clock so a blanket replace cannot pin them.

Core suite 67/71, same 4 pre-existing failures as HEAD.

STILL UNVERIFIED: the empirical proof — two captures of one fixture at a
content-bearing warmup having equal checksums — needs a rebuild. Until that
passes, no headless A/B result should be trusted, including the pending
`bloomIntensity` recommendation.

## G5 — colour-grade LUT

3D grading delivered as a **2D strip** (16³ → 256x16), not `sampler3D`: GLES on
Mali is a shipping target, and a strip works on every backend with no capability
query and no second shader path. Cost is one extra tap — blue is interpolated by
hand between two slices; red/green come free from hardware bilinear because they
address texel centres inside a tile, which is also what stops filtering bleeding
across a tile edge into the next slice.

- `core/color_grade_lut.h/.c` — generates the NEUTRAL (identity) strip at init,
  so enabling the system is a visual no-op until a graded asset exists. That
  keeps "is it wired" separable from "do I like the look".
- Applied in `post_process.fs` AFTER tone mapping, on display-referred 0..1
  values. A LUT fed HDR would sample outside its domain and flatten every
  highlight into the top blue slice. The formula grade stays as the coarse
  control; the LUT carries the look.
- Asset-optional: a strip at `assets/luts/grade.png` is adopted automatically at
  init — no code change, no rebuild. Absent, the frame is unchanged. A load
  failure KEEPS the current LUT and logs the expected dimensions (the usual
  mistake is a 32³ strip from another engine).
- `PostFXConfig.lutEnabled/.lutStrength` (on by default), live override
  `tuning.cfg -> lut_strength`. The shader branch is additionally gated on the
  LUT not being neutral, so the default costs a disabled uniform rather than a
  per-pixel tap that provably cannot change anything.
- `scripts/make_lut.py` — stdlib-only PNG writer (no Pillow, or it stops being
  run) with a `moonlight` look matching the art direction: cool shadows, warm
  highlights, greens desaturated, blacks TINTED NOT LIFTED — lifting them is the
  usual "cinematic" reflex and would destroy the night arena.
- Also fixed in passing: PostFX's tunables were registered inside the
  `if (bloomEnabled)` branch, so `lut_strength` would not have existed with
  bloom off. Moved to the top of `PostFX_Draw`.

Verification: `core/tests/color_grade_lut_test.c` mirrors BOTH the generator and
the shader sampler and asserts the neutral strip round-trips to identity (worst
error 0.00000), no tile-edge bleed at red=1.0 on any slice, and that the LUT is
applied after tone mapping. `scripts/make_lut.py neutral` was verified
byte-identical to the C generator's output. `glslangValidator -S frag` clean;
`-std=c99 -Wall -Wextra` clean. Core suite 68/72, same 4 pre-existing failures.

NOT VERIFIED: anything on screen. Two samplers now live in the composite shader
(bloom + LUT) and that combination has never run under rlvk here — worth a look
first when checking the build.
