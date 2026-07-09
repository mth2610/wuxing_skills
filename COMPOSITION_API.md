# Composition API — Visual Composer & Procedural Meshes

> Source headers: `core/composition/visual_composer.h`, `core/geometry/procedural_mesh_utils.h`
> Implementation: `core/composition/visual_composer.c` + modular `.inl` files
> Cross-reference: [`CORE_API.md`](CORE_API.md) §7 (Particle), §19 stub

> [!IMPORTANT]
> **This document is the mechanical catalog — WHAT exists.** For WHY/HOW to
> combine it into something that actually looks good, **read
> [`WUXING_ART_DIRECTION.md`](WUXING_ART_DIRECTION.md) first** — it's the
> aesthetic authority: Chapter 2 (Universal VFX Language), Chapter 5 (AI
> Design Rules, mandatory), Chapter 6 (VFX Cookbook — reusable layer
> recipes), Chapter 7 (10-step AI workflow + worked examples). Composing
> directly from this catalog without that context reliably produces
> mechanically-correct-but-flat effects (every function called, no
> hierarchy, no silence, no "one idea"). Section 0 below is the bridge
> between the two: it maps every Chapter 6.1 cookbook pattern to the actual
> function calls cataloged in this file, so you can go from "I want a
> Meteor" to a concrete call sequence without reading `.c`/`.inl` source.

---

## 0. Cookbook Pattern → Concrete API (read alongside WUXING_ART_DIRECTION.md §6.1)

Each row translates one of Chapter 6.1's abstract layer recipes into the
actual functions this file (and `CORE_API.md`'s primitive sections) already
document. Layer order in the recipe = call/draw order. Not every layer is
mandatory every time — see WUXING_ART_DIRECTION.md §5.4/§6.4 ("one purpose
per layer", "remove lowest layers first, never remove Core Shape or
Motion").

| Cookbook pattern (§6.1) | Recipe | Concrete calls |
|---|---|---|
| **Projectile** | Projectile → Trail → Emitter → Light → Impact → Smoke | `VFX_ComposeProjectile(VC_MaterialId,...)` (bundles core+trail+light+spin) or `SpawnProjectileTrail`/`VFX_ComposeProjectileTrail` alone → `VFX_ComposeImpact`/`VFX_TriggerExplosion` on hit → `VFX_ComposeSmokePuff`/`SmokeTrail` |
| **Beam** | Beam Core + Flow Map + Glow + Edge Sparks + Impact | `VFX_ComposeBeam(VC_MaterialId,...)` (core+glow+jitter already bundled) + `VFX_ComposeGlintBurst` (edge sparks) + `VFX_ComposeImpact` at the endpoint |
| **Sword Slash** | Motion → Trail → Ribbon → Spark → Flash → Decal | Character motion (skill state machine) + `Afterimage_Spawn` (CORE_API.md §14, ghost trail) + `VFX_ComposeSlashArc(VC_MaterialId,...)` (ribbon core) + `VFX_ComposeGlintBurst` + `VFX_ComposeStreakFlare` + `DecalSystem_Add` (§8) |
| **Explosion** | Flash → Shockwave → Explosion → Debris → Smoke → Residual Light | `VFX_TriggerExplosion(VC_MaterialId,...)` is already the full standard formula (flash/distort/light/particles/decal) — add `VFX_ComposeShockwaveRing` for an explicit ring and `VFX_ComposeEmberDrift` for lingering residue |
| **Portal** | Circle → Flow Map → Ribbon → Orbit Particles → Distortion → Glow | `VFX_SummonCircle` (2-layer counter-rotating circle + inward particle pull) + `VFX_ComposeMagicPuddle`/`VFX_ComposeGroundAura` (flow-map glow) + `ScreenDistort_Add` (§8) |
| **Aura** | Emitter → Orbit → Curl Noise → Ribbon → Soft Glow | `VFX_ComposeAura(VC_MaterialId,...)` (generic) — or `VFX_ComposeCylinderAura`/`VFX_ComposeQiAura` for a body-hugging column shape instead of a ground ring |
| **Dragon** | Head → Body Motion → Ribbon Spine → Emitter → Glow → Trail → Impact | No dedicated primitive — compose by hand: `VFX_ComposeBeam` or a mesh head as the core, Ribbon Strip (§7) as the spine, `ForceField` orbit/curl (§5) for body motion, particle emitters (§6) along the spine. Follow §6.1's stated order; this is the one pattern still requiring original composition, not a single ready-made call. |
| **Meteor** | Meteor → Long Trail → Smoke → Light → Impact → Debris → Shockwave → Dust | `VFX_ComposeProjectile`/`SpawnProjectileTrail` (long trail variant) + `VFX_ComposeSmokeTrail` + `VFX_TriggerExplosion` on impact + `VFX_ComposeShockwaveRing` |
| **Tornado** | Vortex Motion → Ribbon Spiral → Particles → Debris → Mist → Leaves | Element-specific ready-made: `VFX_ComposeCyclone` (Taiji), `VFX_ComposeFireWhirl` (Fire). Other elements: compose from `ForceField` VORTEX+UPDRAFT (§5) + `VC_MotionHelix`/`VC_MotionSpiralIn` (Nhóm 2c motion library below) + `VFX_ComposeMistVeil` (Water) as reference |
| **Summon** | Portal → Materialize → Glow → Shape Reveal → Idle Aura | `VFX_SummonCircle` (portal) → dissolve-in via `u_dissolve` (CORE_API.md §12.4 — animate 1.0→0.0, never pop) → `VFX_ComposeAura` (idle) |
| **Shield** | Core Shape → Flow Map → Energy Edge → Ripple → Light Pulse | `VFX_ComposeShield(VC_MaterialId, pos, radius, progress, time)` — already the full bundled pattern |
| **Chain Lightning** | Charge → Main Arc → Secondary Arcs → Ground Sparks → Residual | `VFX_ComposeChargeUp` (charge) → `SpawnChainLightning`/`VFX_ComposeChain` (arcs; LIGHTNING gets forks automatically) → `VFX_ComposeGlintBurst` (ground sparks) → `VFX_ComposeStaticField` (residual crackle) |
| **Healing** | Soft Glow → Particles Rise → Leaves/Light → Pulse → Fade | `VFX_ComposeAura` (HOLY/WOOD material, soft glow + rising particles) + `VC_Pulse01` (motion library, §2c below) driving a slow emissive pulse + dissolve fade-out, never a burst |
| **Charge (pre-ultimate)** | Weak Glow → Gather → Rotation → Compression → Flash → Release | `VFX_ComposeChargeUp(VC_MaterialId, pos, radius, progress, time)` — already exactly this pattern (glint bursts past `progress > 0.7`) |
| **Dash** | Motion → Afterimage → Trail → Dust → Arrival Flash | `Afterimage_Spawn` every 0.04s while dashing (§14) + a trail (Ribbon Strip §7 or particle trail) + `VFX_ComposeStreakFlare` on arrival |
| **Impact** | Hit → Flash → Light → Sparks → Shake → Distortion → Smoke | `VFX_ComposeImpact`/`VFX_TriggerImpactBurst` (already bundles flash/decal/light/particles/distort — `ImpactBurstConfig`'s 4 steps below) + `CameraFX_Shake` (§8) — not every impact needs every layer, see §6.5 |

---

## Impact Burst (`core/composition/visual_composer.h`)
```c
typedef struct {
    /* --- Step 1: screen distortion --- */
    bool  distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;
    // distortRadius multiplied by sizeScale inside TriggerImpactBurst

    /* --- Step 2: ground decal --- */
    bool      decalEnabled;
    Texture2D decalTex;
    float     decalScale;            /* multiplied by sizeScale at call time */
    float     decalLife;
    Color     decalTint;
    bool      decalRandomRotation;   /* true = GetRandomValue(0,360), false = decalFixedRotation */
    float     decalFixedRotation;

    /* --- Step 3: point light flash --- */
    bool  lightEnabled;
    Color lightColor;
    float lightRadius;  /* multiplied by sizeScale at call time */
    float lightLife;

    /* --- Step 4: particle burst --- */
    bool particlesEnabled;
    ParticleRadialBurstConfig particles;
    // speedMin/speedMax are DIRECT m/s — no internal throttle factor applied.
    // colorStart is auto-resolved from gradient at t=0 if gradient is set,
    // so gradient-only presets (colorStart.a==0) work correctly.
} ImpactBurstConfig;

void VFX_TriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);
// alias: #define VFX_ComposeTriggerImpactBurst VFX_TriggerImpactBurst (both names valid)
```

**`VFX_ImpactPreset` (in `core/presets/vfx_presets.h`)** — used by `VFX_ComposeImpact`:
```c
typedef struct {
    bool  distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;

    bool            decalEnabled;
    DecalPresetType decalPreset;
    float           decalScale, decalLife;
    Color           decalTint;  // {0,0,0,0} defaults to WHITE inside VFX_ComposeImpact

    bool  lightEnabled;
    Color lightColor;
    float lightRadius, lightLife;

    bool                      particlesEnabled;
    ParticleRadialBurstConfig particles;
    // particles.speedMin/Max: direct m/s (1m-scale). No throttle factor.
    // particles.gradient: auto-drives colorStart; set colorStart.a>0 OR set gradient.
} VFX_ImpactPreset;
```

> **Scale note (1m-scale presets):** `particles.speedMin/speedMax` are used as-is (m/s).
> The old 0.3×/0.4× throttle factors have been removed — calibrate presets directly.
> `lightRadius` should be ≤0.4m for a standard hit so it doesn't bleach the particle cloud.

---

## Nhóm 1: Mesh & Hình khối tĩnh (`core/geometry/procedural_mesh_utils.h`, gọi trong Draw)
Tập hợp các hàm vẽ hình học thô ráp, không tự gán vật liệu hay blend mode:
- `ProceduralMesh_DrawOrganicStonePillar`: Vẽ cột đá lăng trụ thô ráp với nắp phẳng ở đỉnh lăng trụ bát giác phẳng đầu.
- `ProceduralMesh_DrawOrganicPuddle`: Vẽ vũng nước phẳng thô dạng đa giác nhấp nhô hữu cơ.
- `ProceduralMesh_DrawRock`: Vẽ tảng đá răng cưa lởm chởm theo cấu trúc `RockMeshData`.
- `ProceduralMesh_DrawShardCluster`: Vẽ chùm tinh thể nhọn nhấp nhô bát diện theo cấu trúc `ShardClusterMeshData`.

---

## Nhóm 2: Bộ phối cảnh hiệu ứng hoàn chỉnh (`core/composition/visual_composer.h`)
Tự gán shader, texture, vật liệu và quản lý blend mode / Z-buffer phù hợp để tạo hiệu ứng hoàn chỉnh:
- `VFX_ComposeStonePillar`: Dựng cột đá nhô lên theo `progress` sử dụng vật liệu `MAT_ROCK`.
- `VFX_ComposeBoulder`: Dựng tảng đá răng cưa lởm chởm kết hợp tâm cầu trơn mịn sử dụng vật liệu `MAT_ROCK`.
- `VFX_ComposeIceCrystal`: Dựng tinh thể băng lăng trụ kết chùm phát sáng trong suốt với vật liệu `MAT_ICE` (blend alpha + tắt ghi độ sâu).
- `VFX_ComposeMagicPuddle`: Dựng vũng nước ma thuật cuộn chảy động (flow map) sử dụng shader `puddle.fs` kết nối slot đa cấu hình `water_caustics.png` (slot 0) và `water_flow.png` (slot 1) dạng lặp `REPEAT`.
- `VFX_ComposeFireball`: Dựng quả cầu lửa hai lớp (lõi phát xạ mạnh sáng rực và lớp vỏ bập bùng biến dạng) vẽ qua blending cộng màu `BLEND_ADDITIVE`.
- `VFX_ComposeSmokePuff`: Bùng khói đặc tại một điểm bằng `ParticleSystem_SpawnRadialBurst`.
- `VFX_ComposeSmokeTrail`: Rải một đường hạt khói bay bay.
- `VFX_ComposeFissureStreak`: Tạo vệt rạn nứt đất dài liền mạch dưới dạng Quad 3D phẳng được map kết cấu `tex_crack_mask.png` (đã loại bỏ culling để hiển thị ổn định trên mọi góc quay camera).
- `VFX_ComposeLightningBolt`: Bắn một tia sét giật (proc bolt) từ điểm đầu đến điểm cuối, trả về ID thực thể tia sét để quản lý.
- `VFX_ComposeImpact`: Sinh hiệu ứng va chạm theo ElementPresetType.
- `VFX_ComposeCast`: Sinh hiệu ứng tụ khí theo ElementPresetType.
- `VFX_ComposeProjectileTrail`: Sinh vệt đạn bay theo ElementPresetType.
- `VFX_ComposeWaterStream`: Dựng dòng nước cuộn trào dạng ống Bezier mềm mại uốn lượn sử dụng shader `tube.fs` và texture `water_caustics.png` trong chế độ `BLEND_ALPHA`.
- `VFX_ComposeGlowingVine`: Dựng dải dây leo phát sáng ngọc bích tự động bò và xoắn ốc quấn chặt lấy mục tiêu. Thực hiện vẽ 2-pass (pass 1 ngọc bích trong suốt phát quang viền Fresnel qua `Material_LoadCustom`, pass 2 lõi sáng trắng tăng cường chế độ cộng màu `BLEND_ADDITIVE`).
- `VFX_ComposeProjectile(VC_MaterialId, ...)`: Vẽ đạn bay theo nguyên tố với đầy đủ hiệu ứng tích hợp (lõi cầu, vệt đuôi hạt, ánh sáng tỏa, tự xoay). 6 material có biến thể cấu trúc riêng (FIRE cầu lửa, ICE mảnh băng xoay, LIGHTNING tia sét, WOOD hạt mầm, EARTH đá xoay, TAIJI lưỡng nghi); material khác rơi về orb generic (lõi `soft` + vỏ `body` + hạt từ `grad`).
- `VFX_GroundPattern`: Tạo hoa văn pháp trận trên mặt đất dạng Quad ngang tắt Culling (đất nứt, vòng ma thuật xoay, nham thạch sủi bọt, sương băng, gai mọc, chữ rune cổ).
- `VFX_ComposeBeam(VC_MaterialId, ...)`: Vẽ tia laser/chùm sáng 3D đa hướng (crossed-quads) cuốn chảy kết cấu — mọi nguyên tố; blend + màu lấy từ material (ADDITIVE→`glow`, ALPHA→`body`), LIGHTNING có jitter điện riêng.
- `VFX_PathWave`: Sinh đợt hiệu ứng mọc tuần tự dọc theo một danh sách điểm (cột đá nhô, gai băng mọc, gai mộc bò, lửa phun, sét truyền), phù hợp với kỹ năng vẽ đường casting kéo chuột.
- `VFX_SummonCircle`: Tạo vòng tròn triệu hồi với hai lớp pháp trận xoay ngược chiều nhau, hút các luồng hạt năng lượng vào tâm.
- `VFX_TriggerExplosion(VC_MaterialId, ...)`: Kích nổ theo công thức chuẩn — mọi nguyên tố; gradient/force field/màu sáng từ material, decal nứt cho hệ giòn (ICE/LIGHTNING/EARTH/METAL) và decal cháy cho hệ còn lại, kèm Screen Distortion, Point Light flash, hạt nổ tỏa tròn và rung camera tùy chọn.
- `VFX_ComposeAura(VC_MaterialId, pos, radius, time)`: Tạo hào quang/vòng buff lơ lửng quanh chân và tỏa các hạt năng lượng hướng lên trên — mọi nguyên tố, màu = `glow` (riêng LIGHTNING = `body` tím ambient); khí thuần dùng `VC_MAT_QI`.
- `VFX_ComposeQiAura` / `VFX_AttachQiAura` / `VFX_DetachQiAura` / `VFX_UpdateQiAuras`: Hào quang khí công quấn quanh nhân vật theo `casterAgentId` (cột khí xoáy ngẫu nhiên bốc lên, sparkle rải rác) — `Attach` gắn/khởi tạo theo agent, `Update` chạy mỗi frame cho toàn bộ pool, `Detach` gỡ khi kết thúc.
- `VFX_ComposeCylinderAura(VC_MaterialId, pos, radius, progress, time)` (`vc_cylinder_aura.inl`): Cột màng năng lượng hình trụ không nắp — phù hợp cho buff giáp/hộ thể. 4 layer: (1) lưới `VortexFunnel` với shader `AuraShellMaterial` (`aura_shell.vs/.fs`) — FBM filaments cuộn lên theo trục Y + scanline rings ngang + Fresnel rim boost; (2) wisp curl quanh thân trụ (ForceField: NOISE_CURL + VISCOSITY + GRAVITY_DIR lên); (3) rune xoay kép trên mặt đất (`mat->runeDecal`, outer 18°/s + inner ngược chiều 32°/s); (4) ember hạt nhỏ phun thẳng lên trong lòng trụ (phân bố đều `sqrtf` random trong disc). `progress` điều khiển scale-in (0..0.2) + guard early-exit. Màu body/glow cập nhật mỗi frame từ `VFX_Material(matId)`.
- `VFX_ComposeGroundAura(VC_MaterialId, pos, radius, scrollSpeed, time)` (`vc_ground_aura.inl`): Đĩa năng lượng phát sáng trên mặt đất — shader `ground_aura.vs/.fs` vẽ quad UV-mapped, FS tính radial mask (edge fade 0.6→1.0, center hole 0→0.3) + FBM wisps tọa độ cực tỏa từ tâm. `scrollSpeed > 0` = năng lượng tỏa ra ngoài; `scrollSpeed < 0` = hút vào tâm. 3 layer: (1) ground disc shader + BLEND_ADDITIVE; (2) edge sparks hạt nhỏ ở vành ngoài; (3) ambient light pulse tại tâm. Màu từ `mat->body/glow`.

---

## Nhóm 2b: Element Material Table (`core/presets/vc_material.h`) — nguồn sự thật màu/chất liệu nguyên tố
`VC_MaterialId` là **trục nguyên tố của mọi archetype** — các enum style cũ (`BeamStyle`, `AuraStyle`, `ShieldStyle`, `ChainStyle`, `ZoneStyle`, `SlashStyle`, `ChargeStyle`, `QiStyle`, `ProjectileStyle`, `ExplosionStyle`) đã bị xóa; chỉ còn `GroundPatternStyle`/`PathStyle` (trục hình dạng). Enum/struct/`VFX_Material()` nằm trong `vc_material.h` (header tối giản, include được từ `visual_composer.h` không dính vòng include qua `skill_helper.h`); `VFX_MaterialFromPreset()` nằm trong `vfx_presets.h`.
```c
typedef enum { VC_MAT_FIRE, VC_MAT_ICE, VC_MAT_WATER, VC_MAT_LIGHTNING,
               VC_MAT_EARTH, VC_MAT_WOOD, VC_MAT_METAL, VC_MAT_TAIJI,
               VC_MAT_HOLY, VC_MAT_VOID, VC_MAT_POISON, VC_MAT_QI, VC_MAT_COUNT } VC_MaterialId;
typedef struct {
    Color body;                   // màu bản sắc nguyên tố (shell, ribbon, rune)
    Color glow;                   // màu điểm nóng phát sáng (beam, ember)
    Color soft;                   // pastel nhạt cho aura/VFXLight/glint (bảng qi aura cũ)
    int   blendMode;              // blend khuyến nghị cho layer sheet/beam (ICE/VOID = BLEND_ALPHA, còn lại ADDITIVE)
    const ColorGradient *grad;    // gradient hạt chuẩn
    const ColorGradient *hotGrad; // biến thể sáng hơn (chỉ LIGHTNING khác grad); không bao giờ NULL
    const ForceField *fld;        // trường lực chuẩn
    const char *runeDecal;        // texture vòng rune dưới đất (shield/charge)
} VFX_ElementMaterial;
const VFX_ElementMaterial* VFX_Material(VC_MaterialId id);       // luôn trả entry hợp lệ (id sai → TAIJI)
VC_MaterialId VFX_MaterialFromPreset(EffectPresetType preset);   // map 8 element preset → material
static inline Color VC_WithAlpha(Color c, unsigned char a);      // gắn alpha tại call site
```
Toàn bộ composition (`vc_beam/aura/shield/chain/zone/slash/charge/ground/explosion/summon.inl` + các điểm VFXLight/glint/ember trong `vc_fire/metal/water/earth/wood/path.inl`) đã tra bảng này thay vì hard-code màu — **đổi look một nguyên tố = sửa một entry trong `vfx_presets.c`**. Quy ước 3 slot: `body` = bản sắc (crimson lửa, bạc kim...), `glow` = điểm nóng (cam lửa, cyan điện...), `soft` = pastel cho aura/light/glint (cam nhạt lửa, trắng bạc kim...). Hai bản sắc có chủ ý được giữ: LIGHTNING glow cyan (hồ quang) vs body tím (ambient, khớp `s_lightningGrad`); TAIJI body tím vs glow vàng gold (aura). `VFX_TriggerExplosion` các style POISON/HOLY/VOID giờ dùng gradient bản sắc riêng (`s_poisonGrad/s_holyGrad/s_voidGrad`) thay vì mượn wood/taiji. Component mới **phải** lấy màu/gradient/force field từ bảng này, chỉ hard-code khi cố tình lệch bản sắc (kèm comment) — các ngoại lệ hiện có: `GROUND_CRACK_*` (nâu đất trung tính), `GROUND_THORNS` (xanh gai tối), `GROUND_RUNE` (tím arcane sáng), palette riêng của `vc_plasma`/`vc_taiji` (cyan-hồng plasma, duotone yin-yang), và các gradient shading nhiều stop cục bộ per-component (`s_fireBodyGrad`, `s_dropGrad`, `s_steelGrad`...).

---

## Nhóm 2c: Motion Library (`core/composition/vc_motion.h`) — quỹ đạo & shaper thuần toán học
Toàn bộ là `static inline`, stateless, không pool, không side effect — được include sẵn qua `visual_composer.h`. Quy ước: trục đứng Y, góc radian. Dùng thay cho sin/cos ad-hoc khi lắp chuyển động:
```c
// Vị trí
Vector3 VC_RingPointXZ(Vector3 center, float radius, float angle);   // điểm trên vòng ngang (spawn ring/orbit — khối cơ bản nhất)
Vector3 VC_MotionOrbit(Vector3 center, float radius, float speed, float time, float phase); // quỹ đạo tròn đều
Vector3 VC_MotionHelix(Vector3 base, float radius, float riseSpeed, float spinSpeed, float t, float phase); // xoắn ốc dâng theo trục Y
Vector3 VC_MotionSpiralIn(Vector3 center, float startRadius, float turns, float phase, float t01); // xoáy hút vào tâm (charge/absorb)
Vector3 VC_MotionJitter(Vector3 pos, float amp, float freq, float time, float seed); // rung lắc 3 trục lệch pha (tremble)
Vector3 VC_MotionBob(Vector3 base, float amp, float freq, float time, float phase);  // dập dềnh dọc Y (lơ lửng)
// Hướng
Vector3 VC_TangentXZ(float angle, float up);                        // tiếp tuyến vòng ngang (KHÔNG chuẩn hóa) — vận tốc hạt bay quanh vành
Vector3 VC_DirCone(Vector3 dir, float coneRad, float u1, float u2); // hướng ngẫu nhiên trong nón quanh dir (u1,u2 = Random01())
// Shaper vô hướng
float VC_Pulse01(float time, float freq);            // sin chuẩn hóa 0..1 (alpha/emissive tuần hoàn)
float VC_Breathe(float time, float freq, float amp); // hệ số 1±amp (nhân vào radius/scale)
float VC_Flicker01(float time, float seed);          // nhiễu hash 0..1 đổi theo frame (lửa/điện; seed tách pha chống chớp đồng bộ)
```
Các archetype (`vc_charge/shield/aura/zone/slash.inl`) đã dùng các hàm này cho orbit mote, tremble, breathe, spawn ring, tangent spark. Component/skill mới nên lắp quỹ đạo từ đây trước, chỉ viết công thức riêng khi motion library không diễn tả được (và cân nhắc bổ sung hàm mới vào `vc_motion.h` thay vì viết inline một chỗ).

---

## Nhóm 3: Beauty Primitives (`core/composition/vc_beauty.inl`) — mảnh trang trí tái sử dụng
Thuần particle/decal/light, **không đụng post-process pipeline** (xem `CORE_ISSUES.md` Item 35 — chỉnh sửa bloom/streak dùng chung rất dễ vỡ trên GPU cũ, tập trung "lấp lánh" vào các primitive nhỏ gọn/ngắn hạn như dưới đây mới an toàn):
- `VFX_ComposeShockwaveRing(pos, radius, life, tint)`: Vòng sóng xung kích mặt đất — decal ring giãn nở (`assets/textures/generic/impact_ring.png`, `BLEND_ADDITIVE`) + flash light.
- `VFX_ComposeGlintBurst(pos, count, spread, tint)`: Chùm tia lấp lánh nhỏ, bung nhanh rồi tắt (~0.12-0.22s/hạt) — dùng làm điểm nhấn "sparkle" cho bất kỳ hiệu ứng nào, kể cả gắn vào các archetype khác.
- `VFX_ComposeEmberDrift(pos, radius, count, tint)`: Hạt tàn lửa/bụi trôi lơ lửng (noise-curl + trọng lực nhẹ hướng lên), lifetime dài (~1.2-2.2s) — dùng cho hào quang/môi trường liên tục.
- `VFX_ComposeStreakFlare(pos, scale, tint)`: Chớp sáng bùng nổ tại điểm (particle tròn cực ngắn + flash light) — đọc là "flash" chứ không phải hình ngôi sao (particle hệ thống chỉ dùng chung 1 texture toàn cục, xem `core/particle_system.h`).

Primitive nội bộ (static trong translation unit `visual_composer.c`, không public — mọi `.inl` đều gọi được):
- `VC_DrawGroundQuadXZ(tex, halfX, halfZ, tint)`: Quad texture nằm ngang tâm gốc tọa độ hiện hành (gọi bên trong push/translate/rotate của caller). Caller quản blend/depth. Nền tảng của mọi `GROUND_*` pattern.
- `VC_DrawGroundRune(tex, pos, radius, angleDeg, tint)`: Vòng rune/glow xoay quanh trục Y tại `pos`, tự push/pop matrix — dùng bởi shield (rune + glow ring đáy vòm) và charge (pháp trận chân).

---

## Nhóm 4: Element Parity Additions (Phase 1 — `vc_metal.inl` / `vc_fire.inl`)
- `VFX_ComposeMetalShardCluster(basePos, seed)`: Cụm mảnh kim loại sắc nhọn dùng chung hệ crystal-mesh với băng nhưng đục/sáng bóng/không refract (`CrystalMaterialParams`: `refraction=0`, `crack=0`, `sparkle` cao).
- `VFX_ComposeBladeRing(pos, radius, bladeCount, rotationDeg)`: Vòng lưỡi kim loại chĩa ra ngoài quanh tâm, dùng vật liệu `MAT_METAL` có sẵn.
- `VFX_ComposeFlameWisp(pos, time)`: Đốm lửa nhỏ lập lờ, lệch pha theo vị trí spawn để nhiều đốm không nhấp nháy đồng bộ.
- `VFX_ComposeFirePillar(basePos, progress)`: Cột lửa trồi lên theo `progress`, cùng công thức smoothstep-rise với `VFX_ComposeStonePillar`.

---

## Nhóm 4b: Element Skill Sets — per-element one-shot & continuous pieces
Mỗi hệ có một cụm hàm riêng trong `vc_<element>.inl`. Quy ước chung: hàm có tham số `time` = **continuous** (gọi mỗi frame với time chạy dồn, tự gate decal/light bằng xác suất — không stack); hàm chỉ có `scale` = **one-shot** (gọi đúng 1 lần). Mọi hàm tự quản blend mode/depth mask.
- `VFX_ComposePlasmaOrb(pos, radius, time)` (`vc_plasma.inl`): Quả cầu năng lượng plasma — 1 lõi bloom sphere cyan (EffectMaterial translucency), 2 lớp màng noise wispy (PlasmaMaterial, xem Shader Material System) counter-scroll + backface, và các wisp trail hồng (head particle + onLiveEmit tail, curl-noise mạnh) uốn éo bên trong. Continuous. Spawn rate dt-based (~27 head/s, tail 80/s × 0.25s) — pool 2000, đừng nhân đôi rate/lifetime mà không tính lại budget.
- `VFX_ComposeLeafSwirl(pos, radius, time)` (`vc_wood.inl`): Lốc lá xoáy quanh điểm (vortex + updraft + curl), lá = particle nhấp nháy size/emissive theo curve "flutter", kèm pollen + moss decal + light. Continuous.
- `VFX_ComposeBloomBurst(pos, scale)` (`vc_wood.inl`): Nở hoa one-shot — vòng cánh hoa bung ra rơi lượn, pollen bay lên, flash trung tâm mềm, decal root ring, light. Gọi 1 lần.
- `VFX_ComposeLeafFall(pos, radius, time)` (`vc_wood.inl`): Tán lá rơi lượn lờ trong vùng (gravity nhẹ + curl mạnh + viscosity chống rơi thẳng), spore lơ lửng, moss decal. Continuous.
- `VFX_ComposeBladeStorm(pos, radius, time)` (`vc_metal.inl`): 7 lưỡi kim (cone `MAT_METAL`) quay quanh caster trên 2 băng đảo chiều, mỗi lưỡi tự có bán kính/độ cao/rake riêng theo hash; streak bạc văng khỏi mũi lưỡi + glint catch-light. Continuous.
- `VFX_ComposeShrapnelBurst(pos, scale)` (`vc_metal.inl`): Nổ mảnh kim loại one-shot — quạt mảnh bay là sát đất (pitch -5°..45°, gravity 7), hero streak kéo đuôi, flash + glint tâm nổ, decal crater (chỉ khi `pos.y` gần đất), distort + light lạnh. Gọi 1 lần.
- `VFX_ComposeRicochetSpark(pos, dir, scale)` (`vc_metal.inl`): Quạt tia lửa parry/deflect one-shot bắn theo `dir` (nón ~35°, chết trong ~0.3s) + micro-flash "ping". Gọi 1 lần khi chặn đòn/đạn nảy.
- `VFX_ComposeSplashBurst(pos, scale)` (`vc_water.inl`): Vương miện nước one-shot — vòng giọt bắn dốc 55°-75° rơi lại dưới gravity thật 9.8, jet trung tâm, mist puff, 2 decal ring giãn nở lệch tốc độ (splash nhanh + ripple chậm), light + distort nhẹ. Gọi 1 lần khi đạn nước chạm/đáp đất.
- `VFX_ComposeBubbleStream(pos, radius, time)` (`vc_water.inl`): Bọt khí nổi lên (buoyancy + curl wobble + viscosity), mỗi bọt chết nổ thành 3 micro-droplet (`onDeathEmit`), caustic light shimmer + ripple decal nền. Continuous.
- `VFX_ComposeMistVeil(pos, radius, time)` (`vc_water.inl`): Màn sương thấp bò quanh vùng (vortex chậm + curl + viscosity mạnh — sương bò chứ không thổi), kèm giọt ngưng tụ rơi lác đác + sheen ánh trăng lạnh. Continuous.
- `VFX_ComposeGustSlash(pos, dir, scale)` (`vc_taiji.inl`): Lưỡi gió one-shot chém theo `dir` — quạt streak dẹt ~80° (blade, không phải cone), bụi văng ngang, decal wind groove xoay đúng hướng chém, distort xé không khí. Gọi 1 lần.
- `VFX_ComposeCyclone(pos, radius, time)` (`vc_taiji.inl`): Cột lốc — cánh tay spawn xoay quanh chân đế tạo dải xoắn ốc leo phễu (trick của FirePillar), vortex 6.0 + updraft + gravity-point giữ phễu chụm, váy bụi xám + mảnh vụn bay cao + cột distort. Continuous.
- `VFX_ComposeStaticField(pos, radius, time)` (`vc_taiji.inl`): Trường tĩnh điện — 3-4 micro-arc tím/trắng bò trên mặt cầu bán kính `radius`, re-seed mỗi ~0.09s (nhịp crackle, không phải noise trắng per-frame), spark bắn khỏi điểm neo + light tím giật cục. Continuous. Dùng `DrawLightningBoltEx` + `camera` global.
- `VFX_ComposeYinYangOrbit(pos, radius, time)` (`vc_taiji.inl`): Cặp cầu âm-dương đuổi nhau trên vành nghiêng bob ngược pha — dương additive trắng phát sáng, âm BLEND_ALPHA đen viền tím fresnel (additive không vẽ được bóng tối), trail mote đuổi chéo nhau, mandala taiji ring xoay dưới đất. Continuous.
- `VFX_ComposeRockBurst(pos, scale)` (`vc_earth.inl`): Nổ đá vụn one-shot — chunk nâu văng 20°-70° rơi gravity thật 9.8, hạt cát sáng bắt sáng, mây bụi phình chậm sống lâu hơn chunk, decal stone shatter 3.5s, `CameraFX_Shake` + distort (thổ = cảm nhận, không phát sáng — light rất mờ). Gọi 1 lần.
- `VFX_ComposeFloatingStones(pos, radius, time)` (`vc_earth.inl`): 5 rock mesh thật (`MeshCache_GetRock` seed cố định) lơ lửng quanh caster, orbit/bob/tumble chậm rãi (khối lượng đọc qua quán tính), bụi cát bay LÊN dưới mỗi hòn (dấu hiệu anti-gravity) + thỉnh thoảng 1 viên sỏi rơi thẳng xuống. Continuous.
- `VFX_ComposeQuakeRumble(pos, radius, time)` (`vc_earth.inl`): Vùng địa chấn — sỏi nhảy khỏi mặt đất rồi rơi lại, bụi phụt lên ngẫu nhiên, vết nứt mới stamp dần trong lúc rung, `CameraFX_Shake(0.05)` liên tục xác suất thấp. Continuous.
- `VFX_ComposeFlameBreath(pos, dir, scale, time)` (`vc_fire.inl`): Hơi thở lửa continuous phun theo `dir` — packet FireFlow bắn xuống nón ~14° với nhịp surge (không phải jet đều), buoyancy sẵn trong `s_flameFld` tự cong đuôi luồng lên trên, khói ở điểm tắt, shimmer + muzzle light.
- `VFX_ComposeBurningGround(pos, radius, time)` (`vc_fire.inl`): Mảng đất cháy continuous — lưỡi lửa mọc ở điểm ngẫu nhiên thiên về tâm, decal lava-crack pulse dưới lửa + scorch tích tụ, ember + khói bốc, firelight nhấp nháy.
- `VFX_ComposeFireWhirl(pos, radius, time)` (`vc_fire.inl`): Lốc lửa continuous — vortex 7.0 + updraft 3.0, cánh tay spawn xoay tạo dải lửa xoắn leo phễu, jet trắng nóng giữa trục, ember văng ly tâm, vương miện khói trên đỉnh, distort + light mạnh (ult).
- `VFX_ComposeElementalMist(VC_MaterialId, pos, radius, time)` (`vc_elemental_mist.inl`): Sương khô (dry-ice) liên tục tỏa từ một điểm — 3 layer: fog body (hạt lớn 0.08–0.15m, mat→soft alpha 50, hugging ground Y<0.05m), wisp tendril (curl noise, tập trung gần tâm), sublimation breath (hơi nhỏ ngay tại điểm nguồn). Force field rebuilt mỗi frame: radial push outward (GRAVITY_POINT âm) + curl noise + gravity xuống nhẹ + viscosity nặng (fog creep không float). Màu/glow lấy từ `VFX_Material(matId)->soft/glow`; đổi element = đổi palette, giữ nguyên hình dạng.

---

## Nhóm 5: Phase 3 Archetypes — shield/chain/zone/slash/charge
Cùng quy ước tham số `(style, pos, ..., progress, time)` như các archetype ở Nhóm 2 (`VFX_ComposeBeam`, `VFX_GroundPattern`...), để AI dựng skill mới dễ đoán chữ ký hàm:
- `VFX_ComposeShield(VC_MaterialId, pos, radius, progress, time)` (`vc_shield.inl`): Khiên/vòm chắn — scale-in 0..0.3, giữ nguyên, fade-out ở 0.85..1.0 (gọi liên tục mỗi frame trong lúc khiên còn tồn tại). Sphere lõm nửa dưới đất tạo hiệu ứng dome mà không cần mesh hemisphere riêng, cộng vòng rune xoay ở chân (`runeDecal` của material) + glint bề mặt ngẫu nhiên.
- `VFX_ComposeChain(VC_MaterialId, const Vector3 *targets, count, progress, time)` (`vc_chain.inl`): Nối tuần tự các điểm mục tiêu (bounce/jump targeting) — đoạn `i` chỉ hiện khi `progress` vượt qua `i/(count-1)` (giống `VFX_PathWave`); mỗi target mới chạm tới nổ `VFX_ComposeGlintBurst`. LIGHTNING = tia sét giật + nhánh fork, WOOD = dây leo, material khác = beam cùng nguyên tố.
- `VFX_ComposeZone(VC_MaterialId, pos, radius, progress, time)` (`vc_zone.inl`): Vùng AoE tồn tại lâu dài — tái dùng `VFX_GroundPattern` cho nền (hoa văn chọn theo material: FIRE→LAVA, ICE→FROST, WOOD→THORNS, VOID→RUNE, EARTH/METAL/POISON→CRACK, còn lại→MAGIC_CIRCLE) + hạt/light rải theo xác suất mỗi lần gọi, gọi mỗi frame suốt thời gian zone active.
- `VFX_ComposeSlashArc(VC_MaterialId, pos, dir, radius, arcDegrees, progress, time)` (`vc_slash.inl`): Vệt chém cận chiến dạng ribbon cong, mỏng ở đuôi/dày ở đỉnh sweep, chỉ hiện phần cung đã quét tới `progress`; glint ở mép đang chém. Màu = `body`.
- `VFX_ComposeChargeUp(VC_MaterialId, pos, radius, progress, time)` (`vc_charge.inl`): Hiệu ứng tích khí/kênh phép — lõi cầu lớn dần + hạt hội tụ từ vòng ngoài co lại theo `progress`, glint bùng khi gần release (`progress > 0.7`). Màu = `body` (riêng METAL = `glow` xanh điện), rune chân = `runeDecal`.

Tất cả Nhóm 3-5 đã gắn sẵn vào tab **"NEW FX"** trong `sandbox/vfx_test.c` (34 mục) để xem trực quan — không cần viết skill thật mới xem được. Chạy `python3 scripts/sync_vfx_test.py` sau khi thêm/xóa `VFX_Compose*` để giữ tab đồng bộ.

---

## `.inl` include order (in `visual_composer.c`)
```
vc_common.inl     — render primitives (VC_DrawGroundQuadXZ, VC_DrawGroundRune)
vc_beauty.inl     — beauty primitives — MUST precede element .inl (vc_zone calls GlintBurst)
vc_preset.inl     — preset-driven: SmokePuff, SmokeTrail, LightningBolt, Impact, Cast, ProjectileTrail
vc_metal.inl / vc_wood.inl / vc_water.inl / vc_fire.inl / vc_earth.inl / vc_plasma.inl / vc_taiji.inl
vc_projectile.inl / vc_ground.inl / vc_beam.inl / vc_path.inl / vc_summon.inl / vc_explosion.inl / vc_aura.inl / vc_cylinder_aura.inl
vc_shield.inl / vc_chain.inl / vc_zone.inl / vc_slash.inl / vc_charge.inl
vc_elemental_mist.inl
vc_ground_aura.inl
```

## Sync script
`scripts/vfx_test_manifest.json` is the source of truth for the NEWFX tab.
Run `python3 scripts/sync_vfx_test.py` after every `VFX_Compose*` add/remove.
`--check` flag for dry-run (no writes).
