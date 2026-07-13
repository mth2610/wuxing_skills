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
| **Chain Lightning** | Charge → Main Arc → Secondary Arcs → Ground Sparks → Residual | No dedicated charge primitive (`VFX_ComposeChargeUp` was removed 2026-07-10, `vc_charge.inl` deleted) — compose the charge beat by hand. Arcs: `VFX_ChainLightning(points, count, scale, hopDelay)` (`vc_archetype.inl`, staggered hop chain — `VFX_ComposeChain`/`vc_chain.inl` was also removed 2026-07-10, this is the only chain-arc primitive left). Ground sparks: `VFX_ComposeGlintBurst`. Residual: `VFX_ComposeStaticField` |
| **Healing** | Soft Glow → Particles Rise → Leaves/Light → Pulse → Fade | `VFX_ComposeAura` (HOLY/WOOD material, soft glow + rising particles) + `VC_Pulse01` (motion library, §2c below) driving a slow emissive pulse + dissolve fade-out, never a burst |
| **Charge (pre-ultimate)** | Weak Glow → Gather → Rotation → Compression → Flash → Release | No dedicated primitive (`VFX_ComposeChargeUp` removed 2026-07-10) — compose by hand from `VC_MotionSpiralIn` (converging particles) + a growing core sphere (`EffectMaterial`) + `VFX_ComposeGlintBurst` near release |
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
- `VFX_ComposeFissureStreak(start, end, width, progress, time)` (`vc_earth.inl`, viết lại 2026-07-10, sửa lần 2 cùng ngày): dùng `ProceduralMesh_BuildFissure` (`pm_magic_effects.inl`) build lát cắt ngang 5 đỉnh (mép–vai–đáy–vai–mép) dọc centerline, jitter theo noise seed từ `start`/`end` (ổn định qua các frame, không random lại mỗi lần gọi) — kiểu "midpoint displacement" nhẹ built-in. 3 lớp: ① mesh cấu trúc dùng `ProceduralMesh_DrawFissureShaded` (geometry riêng cho dạng crack — xem note dưới), ② "ember seam" — dải quad rộng `width*0.55` dọc đáy rãnh, `BLEND_ADDITIVE`, alpha pulse theo `time` (giữ mờ — earth là nguyên tố ít phát sáng nhất, không phải lava); ③ bụi rơi lác đác ở đầu vết nứt đang lan (gate xác suất mỗi frame, không rải dọc toàn bộ). `progress` (0..1) quyết định số lát được vẽ → vết nứt "chạy" A→B; `time` chỉ dùng cho pulse của ember seam.
  > [!NOTE]
  > Lần sửa đầu (`ProceduralMesh_DrawFissurePartial` + `EffectMaterial(MAT_ROCK)` lit) gần như vô hình trong scene tối — mesh lit chìm thành đen-trên-đen, chỉ còn dải ember mỏng lộ ra (ảnh user gửi: một đường chỉ mảnh nâu-cam). Root cause: `EffectMaterial` phụ thuộc ánh sáng scene thật, map test gần như không có nguồn sáng chiếu crack. Fix: tách riêng `ProceduralMesh_DrawFissureShaded(data, crossColors[5], maxSegments)` (mới, `pm_magic_effects.inl`/`procedural_mesh_utils.h`) — geometry crack giờ tự mang gradient màu theo cross-section (mép sáng ấm → vai nâu tối → đáy gần đen), không phụ thuộc lighting. Đây là "dedicated geometry cho dạng crack" mà user yêu cầu tách riêng, thay vì dùng chung EffectMaterial lit như rock thường. `DrawFissurePartial` (1 màu đặc, lit-dependent) vẫn còn cho trường hợp cần dùng chung Material lit thật sự.
  >
  > **Sửa lần 3 cùng ngày:** sau lần 2, user báo lúc bắn chỉ ra "1 đường mờ nhạt nhỏ và thẳng", lúc chỉ có bụi. Root cause thật sự nằm trong `ProceduralMesh_BuildFissure` (không phải riêng Fissure Streak): spacing giữa các lát cắt ngang bị sàn cứng ở `fmaxf(width*0.5f, 1.0f)` — tối thiểu 1.0m bất kể width, nên crack test dài 3m/rộng 0.4m chỉ có ~3 lát cắt. `progress` (reveal A→B trong test harness ramp 0→1 trong 1s) ở giai đoạn đầu chỉ vẽ 1 lát ngắn gần thẳng ("đường mờ nhạt nhỏ và thẳng"); nếu bắn lại trước khi ramp xong, chỉ thấy hạt bụi đầu mũi. Fix: spacing giờ tính theo tổng chiều dài path / `FISSURE_MAX_SEGMENTS`, floor 0.15m thay vì 1.0m → crack 3m có ~15-20 lát, đủ mật độ để reveal mượt và jaggedness rõ ngay từ đoạn đầu.
  >
  > **Sửa lần 4 cùng ngày:** vẫn "1 đường mờ nhạt, có khi không thấy gì" sau lần 3. Root cause thật: `ProceduralMesh_DrawFissureShaded` (layer ①, mesh cấu trúc) được gọi mà KHÔNG `rlDisableBackfaceCulling()`. Rãnh chữ V có 2 vách nghiêng trái/phải (không phải mặt phẳng nằm ngang đơn thuần) — với backface culling bật (state mặc định đi vào hàm này), vách nào quay lưng lại camera bị cull hẳn, tuỳ góc camera/hướng crack mà chỉ còn 1 vách hoặc gần như không còn quad nào lọt qua → đúng triệu chứng "mờ nhạt/đôi khi không thấy gì". Fix: bọc `ProceduralMesh_DrawFissureShaded` bằng `rlDisableBackfaceCulling()`/`rlEnableBackfaceCulling()` (two-sided), giống cách quad ember ở layer ② đã làm sẵn.
- `VFX_SpawnAuraRing(center, element, radius, duration)` (`vc_archetype.inl`, sửa bug 2026-07-10): vòng 8 emitter hạt quanh `center` + 1 `VFXLight`. Bug: `CreateEmitter` (`core/emitter_system.h`) chỉ tạo emitter — logic thả hạt theo `spawnRate` nằm HẲN TRONG `UpdateEmitterTarget`, không có trong bất kỳ hàm Update hàng loạt nào. `VFX_SpawnAuraRing` gọi `CreateEmitter` 1 lần rồi bỏ mặc, không ai gọi `UpdateEmitterTarget` cho 8 emitter đó nữa → `timeAccumulator` không bao giờ tăng → 0 hạt được sinh ra vĩnh viễn, chỉ còn ánh sáng điểm hiện lên (đọc như "không hiện gì"). **Cạm bẫy tên trùng:** `main.c:440` có gọi `EmitterSystem_Update(dt)` mỗi frame, nhưng đó là 1 hệ thống HOÀN TOÀN KHÁC (định nghĩa trong `skill_helper.c`, pool `s_emitters`/`EMITTER_FIRE`/`EMITTER_SNOW`...) — trùng tên với `core/emitter_system.h` nhưng không liên quan, không cứu được các emitter của AuraRing. Fix: thêm vòng lặp trong `VC_Archetype_Update` gọi `UpdateEmitterTarget(emitterIds[k], p, dt)` cho từng điểm trên vòng tròn mỗi frame khi aura còn active.
- `VFX_ComposeLightningBolt(start, end, scale)` (`vc_archetype.inl`, không phải fire-and-forget đơn giản — di chuyển từ `vc_neutral.inl` 2026-07-10): đăng ký 1 slot trong pool `Arch_Bolt` (8 slot) rồi tự động được `VC_Archetype_Update`/`Draw3D` gọi `ProcBolt_Update`/`Draw` mỗi frame trong 0.5s (leader flash sáng 1.9 giảm dần còn 0.3 — cùng công thức decay với rain bolt của thunder_orb_skill), tự `ProcBolt_Kill` khi hết. Trả về slot index để tham khảo, không cần gọi Kill thủ công (tự hết hạn). Trước đó bị lỗi: gọi `SpawnProcBolt` rồi bỏ mặc, không có Update/Draw nào theo sau nên bolt tồn tại trong pool nhưng vô hình.
- `VFX_ComposeEnergyFlow(from, to, scale, duration)` (`vc_archetype.inl`, mới 2026-07-10, sửa lần 2 cùng ngày): dòng năng lượng A→B — dùng pool `Arch_Flow` (8 slot) giống pattern `Arch_Bolt`, tự `EnergyFlow_Kill` khi hết `duration`. Cơ chế trong `core/vfx_proc_ray.c` (`SpawnEnergyFlow`/`EnergyFlow_Update`/`EnergyFlow_Draw`/`EnergyFlow_Kill`). Đặc điểm hình:
  - **Sóng cuộn hữu cơ chạy dọc thân** (`GenerateFlowWaypoints`): đa harmonic trên 2 trục vuông góc, 2 đầu ghim (envelope `sin(t·π)`), pattern DI CHUYỂN theo thời gian vì `wavePhase` tăng theo `ProcRayConfig.waveSpeed` — đây là điểm khác bản đầu tiên (bản đầu chỉ là 1 cung cong tĩnh → đọc như "cây que", bị chê). `FLOW_WAYPOINT_CNT=17` control points, resample Catmull-Rom lên `FLOW_RIBBON_PTS=48` cho mượt.
  - **Width phình giữa, thon nhọn 2 đầu** (`powf(sin(f·π), 0.55)` — "tapered width" trong ref) — mảng envelope dùng chung cho cả 3 layer.
  - **Sửa lần 2 (cùng ngày):** dựng lại qua `DrawRibbonEnergyField` (`core/ribbon_strip.h`, xem mục Primitive — thực ra nằm ở core, không phải composition, vì cả EnergyFlow lẫn Beam đều cần) thay cho `DrawRibbonStrip` camera-facing đơn phẳng cũ — cùng lý do Beam vừa migrate: 1 ribbon camera-facing nhìn nghiêng bị bẹp dí ("sao nhìn nó kì vậy?" — user báo). Giờ dùng "+" cross-section 2 mặt phẳng thật (`RIBBON_WORLD_UP`) như Beam, đọc đặc/có khối từ mọi góc camera. 3 layer: glow mềm rộng không texture / thân dùng texture cuộn UV riêng tốc độ (`ProcRayConfig.flowScrollSpeed`) / lõi trắng nóng mảnh cuộn nhanh hơn (×1.3) + `vFlip`. Gradient màu dọc thân (`FlowLerpColor`) bỏ, thay bằng 3 màu cố định theo layer (đơn giản hơn, khớp pattern Beam) — không còn track riêng `scrollOffset` tiền nhân với `flowScrollSpeed`, đổi thành `elapsedTime` thô để mỗi layer tự nhân tốc độ scroll riêng.
  - **Sửa lần 3 (cùng ngày):** vẫn nhìn "kì" — 1 dải mờ nhòe không thấy vệt texture. Root cause: dùng nhầm `water_flow.png` (texture flow-DIRECTION map cho `VFX_ComposeMagicPuddle`, không phải texture vệt sáng thị giác) thay vì `energy_flow.png` mà Beam dùng — đổi sang `energy_flow.png` cho đồng bộ. Đồng thời `glowWidthMult` 2.6→1.4: layer glow không texture rộng gấp 2.6 lần layer thân có texture, lấn át hoàn toàn chi tiết texture, khác tỉ lệ ~1.3x của Beam (outer 0.65 / inner 0.50) — giảm xuống 1.4 để layer thân có texture không bị glow nuốt mất.
  - **Sửa lần 4 (cùng ngày):** vẫn không thấy "trương năng lượng cuộn" — user chỉ đúng nguyên nhân còn lại: layer glow phủ rộng + `BLEND_ADDITIVE` cộng dồn 1 lượng màu ĐỀU lên toàn bộ bề rộng, hạ tương phản của texture bên dưới (texture cần nền tối để vệt sáng nổi rõ, cộng thêm màu nền phẳng lên trên làm giảm chênh lệch sáng/tối). Fix: `thickness` 0.06→0.025 (mỏng hẳn theo đúng yêu cầu "core cần thật mỏng"), `glowWidthMult` 1.4→1.15 (gần sát layer thân, chỉ còn là viền hazy mỏng), `colorGlow.a` 150→90 (giảm mức cộng nền), hot-core layer `widthRatio` 0.4→0.22 (dòng sáng mảnh hơn).
  - **Sửa lần 5 (cùng ngày):** lần 4 làm quá tay — thu `thickness` chung quá nhỏ khiến TOÀN BỘ (glow lẫn thân) đều mảnh, user phản hồi "lớp energy quá mỏng ko thấy đc, lớp energy cần rộng và mờ". Làm rõ 2 vai trò khác nhau: "core" = dòng sáng trong cùng (hot-core layer, `widthRatio=0.22`) — giữ MỎNG; "energy"/glow = layer ngoài cùng không texture — cần RỘNG và MỜ (khác nhau về bề rộng, không phải độ mỏng). Fix: `thickness` 0.025→0.05 (base dùng chung cho mọi layer, layer nào cần mỏng tự có `widthRatio` nhỏ, không nén base xuống làm mảnh hết); `glowWidthMult` 1.15→2.8 (rộng hẳn, đúng vai trò halo mềm bao quanh); `colorGlow.a` 90→110 (đủ nhìn thấy khi diện tích lớn hơn, vẫn thấp hơn body/core để không nuốt chi tiết texture).
  - **Sửa lần 6 (cùng ngày) — bỏ hẳn lớp glow:** user quyết định bỏ lớp "bloom" (layer glow không texture) thay vì tiếp tục chỉnh width/alpha — sau 3 lần chỉnh qua lại (2.6→1.4→1.15→2.8) vẫn không tìm được điểm cân bằng đẹp giữa "đủ rộng để thấy" và "không nuốt chi tiết texture của layer thân". `DrawFlowChannel` giờ chỉ còn 2 layer: thân dùng `energy_flow.png` (widthRatio=1.0) + lõi trắng nóng mảnh (widthRatio=0.22, vFlip, cuộn nhanh hơn ×1.3). Halo mềm quanh dòng năng lượng (nếu cần) để hệ bloom post-process của game (`core/post_fx.c`) tự lo — không giả lập bằng 1 quad additive phẳng nữa. `cfg->colorGlow`/`cfg->glowWidthMult` không còn dùng trong hàm này (vẫn còn field trong `ProcRayConfig`, dùng bởi ProcRay/ProcBolt).
  - **Sửa lần 7 (cùng ngày):** yêu cầu "cho dòng năng lượng rộng hơn nữa" — tăng `thickness` 0.05→0.10 (base dùng chung, lõi vẫn mảnh tương đối nhờ `widthRatio=0.22` riêng).
- `VFX_ComposeGhostTendrils` — **đã xóa** (2026-07-10, cùng ngày với lúc thêm). Từng là N=5 strand `EnergyFlow` cycling hidden/visible độc lập (`Arch_TendrilGroup` pool trong `vc_archetype.inl`), dùng preset `ProcRay_GhostFlowConfig` (đã xóa theo). Bị chê "xấu hoắc" sau cả 2 lần chỉnh sửa (thickness, alpha) — xóa hẳn theo yêu cầu user thay vì tiếp tục vá. Nếu cần lại khái niệm "nhiều luồng năng lượng ngắt quãng từ nguồn đến mục tiêu" sau này, tham khảo cách tiếp cận cũ (multi-strand timing layer chồng lên `EnergyFlow`) làm điểm khởi đầu nhưng thiết kế lại phần shape/material — vấn đề gốc chưa rõ là do EnergyFlow's ribbon quá mảnh về bản chất, hay do cách phối màu/preset chưa đúng.
  > [!NOTE]
  > Bối cảnh: user đề xuất kiến trúc `RibbonMesh`/`MeshBuilder` tách biệt (points → mesh) cho mọi VFX dạng dải. Khảo sát cho thấy `core/ribbon_strip.h`'s `RibbonPoint`/`DrawRibbonStrip` đã LÀ đúng module đó (đã dùng chung cho Beam/Bolt/Trail) — tránh tạo module song song ([[feedback_vfx_reuse_before_invent]]). Thay vào đó mở rộng `ribbon_strip.h`: thêm `RibbonMode` (`RIBBON_CAMERA_FACING`/`RIBBON_WORLD_UP`/`RIBBON_FIXED_NORMAL`) qua `DrawRibbonStripEx`, và `Ribbon_ComputeArcLengthUV` (sửa bug UV = index/count thay vì độ dài thật, ảnh hưởng cả `DrawChannel` dùng chung bởi ProcRay/ProcBolt). `EnergyFlow` là use-case đầu tiên thật sự cần texture-scroll qua ribbon (trước giờ mọi ribbon trong project đều vẽ không texture, `(Texture2D){0}`).
  > Chưa làm: persistent VBO (`Ribbon_Create/Update/Draw/Destroy`) — không có bottleneck đo được, lệch pattern "rebuild mỗi frame" nhất quán của toàn bộ `core/geometry`.
- `VFX_ComposeShardDebris(pos, count, speed, matId)` (`vc_archetype.inl`, mới): Bắn ra cụm `count` mảnh vỡ hình học 3D dạng khối hộp méo mó (6 mặt / 12 tam giác) bay tỏa theo hình nón hướng lên dưới dạng bán cầu từ `pos`.
  - **Mô hình vật lý & Va chạm**: Mỗi mảnh vỡ sở hữu quỹ đạo động lực học hoàn toàn riêng độc lập, chịu ảnh hưởng của trọng lực (gravity Y = -9.81 m/s) và lực cản không khí (viscosity drag), đồng thời tự quay nhào lộn (tumble rotation) ngẫu nhiên quanh trục riêng.
  - **Phản lực nảy (Elastic Bounce)**: Khi chạm mặt phẳng đất (Y = 0), các mảnh vỡ tự động nẩy đàn hồi (damped vertical bounce), ma sát làm trượt chậm dần trên sàn, và tự giảm dần tốc độ tự quay. Khi năng lượng suy giảm dưới ngưỡng tĩnh, mảnh vỡ dừng chuyển động hẳn.
  - **Khói bụi / Tia sáng lấp lánh (Trail & Impact Particles)**: Suốt quá trình bay, mỗi mảnh vỡ tự giải phóng các hạt tơ bụi/lấp lánh kéo vệt sau thân. Đặc biệt hệ Băng/Kim loại tự sinh hạt lấp lánh additive trắng-xanh cực sáng. Khi va chạm nảy đất, một cụm nhỏ hạt bắn tóe ra tại điểm chạm để tạo cảm giác lực đập mạnh mẽ.
  - **Erosion Dissolve**: Khi gần hết tuổi thọ (`lifetime`), mảnh vỡ tiêu biến dần bằng cách tăng dần uniform `u_dissolve` gửi vào shader `effect_material.fs` để viền cạnh cháy sáng tan rã tự nhiên. Nhờ cơ chế batching, uniform này chỉ được kích hoạt trong 20% thời gian cuối, tối ưu hóa tối đa số lượng Draw Calls.
  - **Đa dạng nguyên tố & Hình học ngẫu nhiên**: Tự động chuyển đổi `matId` sang `MaterialPreset` tương ứng (`MAT_FIRE` nham thạch, `MAT_ICE` băng giá, `MAT_METAL` kim loại, `MAT_ROCK` đất đá). Mỗi mảnh vỡ tự tính toán offset ngẫu nhiên xác định trên 8 đỉnh của hình lập phương dựa trên `seed` riêng (không tốn cache, không tốn nạp GPU) để tạo ra các khối đá vỡ dẹt/méo độc nhất vô nhị. Mức đa giác 6 mặt (12 tam giác) đạt điểm cân bằng lý tưởng về mặt thị giác (đọc rõ khối lăng trụ) và tối ưu hóa hiệu năng.
  >
  > **Migrate lần 2 cùng ngày:** `VFX_ComposeBeam` (`vc_beam.inl`) đã migrate sang ribbon module — 2 mặt phẳng giao nhau cũ (raw `rlBegin(RL_QUADS)`) thay bằng 2 lần gọi `DrawRibbonStripEx(..., RIBBON_FIXED_NORMAL, fixedNormal)`. Phát hiện thêm 1 bug latent trong `DrawRibbonStripEx`: hàm không `rlSetTexture(0)` sau khi vẽ — vô hại với các caller cũ (luôn truyền `(Texture2D){0}`) nhưng rò texture state cho caller thật đầu tiên (`EnergyFlow`'s core pass, `VFX_ComposeBeam`) — đã fix thêm `rlSetTexture(0)` vào cuối `DrawRibbonStripEx` (`core/ribbon_strip.c`).
  >
  > **Sau đó user tự viết lại `vc_beam.inl` 2 lần** (twisted-multi-layer ribbon, rồi 3-layer crossed-plane với dual-scroll + V-flip + pulsing width) — bỏ qua `DrawRibbonStripEx`, quay lại raw `rlBegin`. Khảo sát kỹ thuật trong bản cuối phát hiện: (a) width pulsing (`sinf(time*25)*0.1f`) trùng y hệt `VC_Breathe(time,freq,amp)` đã có ở `vc_motion.h` nhưng chưa dùng; (b) `perp1`/`perp2` (cross-basis từ `dir`) chỉ hợp với path THẲNG 2 điểm, không tổng quát cho trail/spiral sau này. User đồng ý tách, muốn số layer cấu hình được → thêm `DrawRibbonEnergyField` + `Ribbon_ComputeCrossFrame` vào `core/ribbon_strip.h` (ban đầu thử đặt ở `vc_common.inl` nhưng dời xuống core ngay sau đó cùng ngày vì `EnergyFlow` cũng cần — xem note bên dưới) — `VFX_ComposeBeam` giờ chỉ còn ~35 dòng: build màu/layer config rồi gọi `DrawRibbonEnergyField(points={start,end}, count=2, ...)`.
  >
  > **Dời xuống core + áp cho EnergyFlow (cùng ngày):** user hỏi "cập nhật cho energy flow trong taiji đi" sau khi thấy Beam đẹp hẳn — đúng lúc lộ ra vấn đề layering: `VC_DrawEnergyField` khi đó còn ở `vc_common.inl` (composition-only, `static`), nhưng `EnergyFlow` sống ở `core/vfx_proc_ray.c` (dưới composition) nên KHÔNG gọi được. Dời toàn bộ struct + hàm xuống `core/ribbon_strip.h`/`.c`, đổi tên `VC_EnergyFieldLayer`→`RibbonEnergyFieldLayer`, `VC_DrawEnergyField`→`DrawRibbonEnergyField`. `VC_Breathe` (chỉ có ở `vc_motion.h`, composition-layer) được inline lại thành `RibbonBreathe` ngay trong `ribbon_strip.c` — chấp nhận trùng công thức 1 dòng để core không phải include header composition. Thêm tham số `widthEnvelope` (mảng nhân bề rộng theo điểm, NULL = đều) để giữ được "tapered width" (`powf(sin(f·π),0.55)`) mà bản cross-plane gốc của Beam không cần nhưng EnergyFlow cần (dòng năng lượng phình giữa, thon 2 đầu).
- `VFX_ComposeImpact`: Sinh hiệu ứng va chạm theo ElementPresetType.
- `VFX_ComposeCast`: Sinh hiệu ứng tụ khí theo ElementPresetType.
- `VFX_ComposeProjectileTrail`: Sinh vệt đạn bay theo ElementPresetType.
- `VFX_ComposeWaterStream`: Dựng dòng nước cuộn trào dạng ống Bezier mềm mại uốn lượn sử dụng shader `tube.fs` và texture `water_caustics.png` trong chế độ `BLEND_ALPHA`.
- `VFX_BeginWaterStreams` / `VFX_DrawWaterStreamOnPath` / `VFX_EndWaterStreams`: Bộ ba hàm vẽ dòng nước uốn lượn theo các khúc cua của một đường đi (path) bất kỳ. Hỗ trợ gom tất cả các dòng nước vẽ trong 1 Draw Call duy nhất trên GPU (Single-Pass Batching) và hỗ trợ lệch pha (`phaseOffset`) độc lập cho từng dòng nước để chuyển động tự nhiên không trùng khớp.
- `VFX_ComposeWaterStreamOnPath`: Hàm tiện ích tự động đóng gói chuỗi gọi Begin -> Draw -> End cho một dòng nước duy nhất đi dọc theo path.
- `VFX_ComposeGlowingVine`: Dựng dải dây leo phát sáng ngọc bích tự động bò và xoắn ốc quấn chặt lấy mục tiêu. Thực hiện vẽ 2-pass (pass 1 ngọc bích trong suốt phát quang viền Fresnel qua `Material_LoadCustom`, pass 2 lõi sáng trắng tăng cường chế độ cộng màu `BLEND_ADDITIVE`).
- `VFX_ComposeProjectile(VC_MaterialId, ...)`: Vẽ đạn bay theo nguyên tố với đầy đủ hiệu ứng tích hợp (lõi cầu, vệt đuôi hạt, ánh sáng tỏa, tự xoay). 6 material có biến thể cấu trúc riêng (FIRE cầu lửa, ICE mảnh băng xoay, LIGHTNING tia sét, WOOD hạt mầm, EARTH đá xoay, TAIJI lưỡng nghi); material khác rơi về orb generic (lõi `soft` + vỏ `body` + hạt từ `grad`).
- `VFX_GroundPattern`: Tạo hoa văn pháp trận trên mặt đất dạng Quad ngang tắt Culling (đất nứt, vòng ma thuật xoay, nham thạch sủi bọt, sương băng, gai mọc, chữ rune cổ).
- `VFX_ComposeBeam(VC_MaterialId, start, end, width, progress, time)`: Tia laser/chùm năng lượng A→B — dựng qua `DrawRibbonEnergyField` (`core/ribbon_strip.h`, xem `CORE_API.md`), 3 layer cấu hình sẵn (vỏ ngoài scroll chậm / điện trong scroll nhanh + V-flip tạo cảm giác đan chéo / lõi trắng nóng không texture), width pulsing qua công thức breathe nội bộ (tương đương `VC_Breathe` ở `vc_motion.h` nhưng inline trong `ribbon_strip.c` để tránh core phụ thuộc composition). Blend + màu lấy từ material (ADDITIVE→`glow`, ALPHA→`body`), width phóng to trong 10% progress đầu.
- `VFX_PathWave`: Sinh đợt hiệu ứng mọc tuần tự dọc theo một danh sách điểm (cột đá nhô, gai băng mọc, gai mộc bò, lửa phun, sét truyền), phù hợp với kỹ năng vẽ đường casting kéo chuột.
- `VFX_SummonCircle`: Tạo vòng tròn triệu hồi với hai lớp pháp trận xoay ngược chiều nhau, hút các luồng hạt năng lượng vào tâm.
- `VFX_TriggerExplosion(VC_MaterialId, ...)`: Kích nổ theo công thức chuẩn — mọi nguyên tố; gradient/force field/màu sáng từ material, decal nứt cho hệ giòn (ICE/LIGHTNING/EARTH/METAL) và decal cháy cho hệ còn lại, kèm Screen Distortion, Point Light flash, hạt nổ tỏa tròn và rung camera tùy chọn.
- `VFX_ComposeAura(VC_MaterialId, pos, radius, time)`: Tạo hào quang/vòng buff lơ lửng quanh chân và tỏa các hạt năng lượng hướng lên trên — mọi nguyên tố, màu = `glow` (riêng LIGHTNING = `body` tím ambient); khí thuần dùng `VC_MAT_QI`.
- `VFX_ComposeQiAura` / `VFX_AttachQiAura` / `VFX_DetachQiAura` / `VFX_UpdateQiAuras`: Hào quang khí công quấn quanh nhân vật theo `casterAgentId` (cột khí xoáy ngẫu nhiên bốc lên, sparkle rải rác) — `Attach` gắn/khởi tạo theo agent, `Update` chạy mỗi frame cho toàn bộ pool, `Detach` gỡ khi kết thúc.
- `VFX_ComposeCylinderAura(VC_MaterialId, pos, radius, progress, time)` (`vc_cylinder_aura.inl`): Cột màng năng lượng hình trụ không nắp — phù hợp cho buff giáp/hộ thể. 4 layer: (1) lưới `VortexFunnel` với shader `AuraShellMaterial` (`aura_shell.vs/.fs`) — FBM filaments cuộn lên theo trục Y + scanline rings ngang + Fresnel rim boost; (2) wisp curl quanh thân trụ (ForceField: NOISE_CURL + VISCOSITY + GRAVITY_DIR lên); (3) rune xoay kép trên mặt đất (`mat->runeDecal`, outer 18°/s + inner ngược chiều 32°/s); (4) ember hạt nhỏ phun thẳng lên trong lòng trụ (phân bố đều `sqrtf` random trong disc). `progress` điều khiển scale-in (0..0.2) + guard early-exit. Màu body/glow cập nhật mỗi frame từ `VFX_Material(matId)`.
- `VFX_ComposeGroundAura(VC_MaterialId, pos, radius, scrollSpeed, time)` (`vc_ground_aura.inl`): Đĩa năng lượng phát sáng trên mặt đất — shader `ground_aura.vs/.fs` vẽ quad UV-mapped, FS tính radial mask (edge fade 0.6→1.0, center hole 0→0.3) + FBM wisps tọa độ cực tỏa từ tâm. `scrollSpeed > 0` = năng lượng tỏa ra ngoài; `scrollSpeed < 0` = hút vào tâm. 3 layer: (1) ground disc shader + BLEND_ADDITIVE; (2) edge sparks hạt nhỏ ở vành ngoài; (3) ambient light pulse tại tâm. Màu từ `mat->body/glow`.
- `VFX_ComposeEnergySmoke(pos, scale, progress, time, sourceUV)` (`vc_smoke_energy.inl`, mới — thay thế hướng flipbook/video sau khi tìm/tạo asset atlas quá khó): **1 đốm khói duy nhất** toả ra chậm rồi tan biến. Vẽ trên **billboard quad camera-facing** (`DrawCoreBillboardQuad`, mới — `core/geometry/pm_core_shapes.inl`) thay vì sphere mesh. Rẻ hơn sphere nhiều (1 quad = 2 tam giác so với sphere 20×20 = 800 tam giác).
  > [!NOTE]
  > **Đổi shader lần 2 (cùng ngày):** bản FBM erosion-dissolve (radial bias) chưa đẹp — thử raymarch turbulence 50 bước phỏng theo shader "Extinguish" (@XorDev) — vẫn "vô cùng mờ" do FOV bị set ngược (rộng ngay từ lúc sinh thay vì lúc gần tan) + cường độ tích luỹ quá thấp trước `tanh()`.
  > **Đổi shader lần 3 (cùng ngày) — nghiệm giải tích PT khuếch tán:** dùng **nghiệm giải tích dạng đóng** cho nguồn điểm/đĩa 2D: `C(r,t) = C₀·exp(-r²/4Dt)/(4πDt)` — đúng là 1 hàm Gauss có phương sai tăng theo `t` (lan rộng) và biên độ đỉnh giảm theo `1/t` (mờ dần, tự động bảo toàn "khối lượng"). `u_progress` map trực tiếp thành `t`, `u_diffusion` = hệ số `D`. Thêm FBM domain-warp lên toạ độ sample trước khi tính khoảng cách tâm.
  > **Sửa lần 4 (cùng ngày):** giảm `t₀` xuống `0.005` + hiệu chỉnh lại `D` (composer: 0.35→0.18) sao cho half-width đi từ ~0.05 (điểm nhỏ) lúc sinh lên ~0.7 (phủ gần hết quad) lúc tan.
  > **Tối ưu hóa Phase 6 (mới):** Chuyển đổi toàn bộ thuật toán Shader từ FBM 3D sang FBM 2D giúp giảm tải toán học đến 86%. Hỗ trợ batching thông qua cặp hàm bao đầu/cuối `VFX_BeginEnergySmokeBatch` và `VFX_EndEnergySmokeBatch` giúp triệt tiêu render state flushing khi vẽ nhiều đốm khói cùng lúc.
- `VFX_ComposeMagicFilaments(pos, scale, progress, color, thickness, frequency, speed, sourceUV)` (`vc_smoke_energy.inl`, mới): Các sợi năng lượng lấp lánh mảnh mai, khuếch tán từ nguồn điểm `sourceUV` (UV-local) và tự tiêu tan dần.
  - **Cơ chế vật lý & Tạo hình**: Thừa hưởng mô hình giải tích khuếch tán chất khí của `energy_smoke` để mô phỏng dãn nở và tan biến, kết hợp ridged FBM để sinh các sợi tơ năng lượng mảnh và gaseous fresnel phát quang ở rìa biên. Các đỉnh lấp lánh (sparkles) chạy dọc thân sợi sinh ngẫu nhiên từ noise có tần số cao.
  - **Tối ưu cực đỉnh (GPU Fill-rate)**: Sử dụng 1-octave noise cho domain warping và 2-octave ridged FBM cho dải tơ, cắt giảm tổng số truy vấn noise của Shader từ 10 xuống còn 5 lần cho mỗi pixel (tiết kiệm 50% tải GPU, đảm bảo chồng lấp nhiều hạt vẫn giữ 60 FPS).
  - **Batching CPU**: Tích hợp các hàm gom nhóm `VFX_BeginMagicFilamentsBatch()` and `VFX_EndMagicFilamentsBatch()` giúp gửi toàn bộ quads lên GPU trong 1 Draw Call duy nhất nếu dùng chung thông số.
- `VFX_ComposeMagicFilamentsOnPlane(center, normal, scale, progress, color, thickness, frequency, speed, sourceUV)` (`vc_smoke_energy.inl`, mới): Phiên bản vẽ các sợi tơ lấp lánh định hướng nằm trên mặt phẳng có vec-tơ pháp tuyến `normal` (sử dụng `DrawCoreOrientedQuad` để dán phẳng thay vì quay mặt về camera). Tự động đẩy nhẹ quad 0.03m theo phương pháp tuyến để triệt tiêu hiện tượng Z-fighting.
- `VFX_ComposeBlackHole(VC_MaterialId, pos, radius, time)` (`vc_black_hole.inl`): Hố đen/kỳ dị hấp dẫn **lơ lửng trên cao** (dùng với `pos.y` > 0 — chủ ý hút vật chất từ mặt đất lên, không phải hiệu ứng đặt trên nền). Pipeline "sphere + shader xoáy" (rẻ hơn raymarch thật, đủ đẹp cho MMORPG nhiều hiệu ứng cùng lúc — xem thang chi phí/độ đẹp trong comment đầu file). 7 layer: (1) sphere sự kiện chân trời (`DrawCoreSphere` + `EffectMaterial` thân đen gần tuyệt đối `translucency=0`, chỉ có viền fresnel mỏng phát sáng); (2) **swirl shells** — 3 sphere đồng tâm bán kính tăng dần, dùng chung shader `black_hole_swirl.fs` (mới): shader đổi UV kinh độ/vĩ độ sẵn có của `DrawCoreSphere` thành tọa độ cực (angle=longitude, radius=khoảng cách tới xích đạo), xoáy góc theo bán kính + thời gian (`angle += radius*10; angle -= time*speed`), sample FBM trong miền đã xoáy, rồi `density *= exp(-radius/bandWidth)` để năng lượng tụ thành dải quanh xích đạo thay vì sáng đều cả khối — vẽ thẳng lên UV mặt cầu, không phải mặt phẳng cắt ngang. Mỗi shell tốc độ xoay/chiều xoay/noiseScale/bandWidth khác nhau (shell trong xoay nhanh ngược chiều, giữa xoay chậm thuận, ngoài mờ dần) — kỹ thuật "nhiều shell mỏng chồng lớp = giả volume" thay vì raymarch thật; (3) hạt vật chất rơi vào tâm cự ly gần — spawn trên mặt cầu (uniform sphere sampling) quanh hố đen + vận tốc thẳng vào tâm, đúng kỹ thuật `VFX_SummonCircle` dùng cho bước "hút hạt vào tâm"; (4) **ground drain** — lớp chính tạo cảm giác "hút đồ dưới đất lên": hạt spawn rải rác trên mặt đất thật (Y=0) ngay dưới hố đen, bay thẳng lên vào tâm, `lifetime = distance/speed` (clamp 0.3–3.0s) để hạt thực sự "tới nơi" thay vì tắt giữa chừng hoặc bay quá; (5) rune xoay dưới đất đánh dấu điểm hút (`mat->runeDecal`, cùng primitive `VC_DrawGroundRune` mà `VFX_ComposeShield` dùng), bán kính theo `radius` của hố đen; (6) `ScreenDistort_Add` xác suất mỗi frame (bẻ cong không gian, không dùng static timer để tránh lỗi chia sẻ state giữa nhiều instance cùng lúc); (7) ambient light nhạt tại tâm (hố đen không nên chói). Màu từ `mat->body/glow` cho các layer hạt/rune; swirl shells dùng tím sâu/tím-trắng riêng (không đọc bảng material — hố đen luôn tím vũ trụ bất kể `matId` truyền vào gì, có comment tại chỗ định nghĩa). Mặc định gợi ý `VC_MAT_VOID`.

---

## Nhóm 2a (Mới): Ranh giới gom nhóm vẽ (Batch Render Boundaries)
Các hàm quản lý trạng thái OpenGL, gán shader và thiết lập blend/depth mask một lần để vẽ hàng loạt thực thể VFX hiệu suất cao, loại bỏ Raylib Batching Hazards:
- `VFX_BeginEnergySmokeBatch()` / `VFX_EndEnergySmokeBatch()`
- `VFX_BeginMagicFilamentsBatch()` / `VFX_EndMagicFilamentsBatch()`
- `VFX_BeginSmokeColumnBatch()` / `VFX_EndSmokeColumnBatch()`

> [!TIP]
> Sử dụng các cặp hàm này bao bọc bên ngoài các vòng lặp vẽ hạt/khói cùng loại để tăng FPS gấp nhiều lần nhờ giảm thiểu tối đa việc thay đổi Uniform của Shader và chia cắt Draw Call trên GPU.

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
- `DrawRibbonEnergyField(...)` — **chuyển ra `core/ribbon_strip.h`** (không còn ở `vc_common.inl`), xem `CORE_API.md` §"Ribbon Strip". Lý do dời: cả `core/vfx_proc_ray.c`'s EnergyFlow lẫn `vc_beam.inl`'s `VFX_ComposeBeam` đều cần dùng — composition được phép phụ thuộc core, không được ngược lại, nên primitive dùng chung bởi cả 2 phải nằm ở core. Cả 2 caller hiện tại (Beam 2 điểm thẳng, EnergyFlow ~48 điểm Catmull-Rom) đều đọc như trường năng lượng 3D thật (2 mặt phẳng chữ thập), không phải ribbon phẳng nhìn nghiêng bị bẹp.

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

## Nhóm 5: Phase 3 Archetypes — shield/zone/slash
Cùng quy ước tham số `(style, pos, ..., progress, time)` như các archetype ở Nhóm 2 (`VFX_ComposeBeam`, `VFX_GroundPattern`...), để AI dựng skill mới dễ đoán chữ ký hàm:
- `VFX_ComposeShield(VC_MaterialId, pos, radius, progress, time)` (`vc_shield.inl`): Khiên/vòm chắn — scale-in 0..0.3, giữ nguyên, fade-out ở 0.85..1.0 (gọi liên tục mỗi frame trong lúc khiên còn tồn tại). Sphere lõm nửa dưới đất tạo hiệu ứng dome mà không cần mesh hemisphere riêng, cộng vòng rune xoay ở chân (`runeDecal` của material) + glint bề mặt ngẫu nhiên.
- `VFX_ComposeZone(VC_MaterialId, pos, radius, progress, time)` (`vc_zone.inl`): Vùng AoE tồn tại lâu dài — tái dùng `VFX_GroundPattern` cho nền (hoa văn chọn theo material: FIRE→LAVA, ICE→FROST, WOOD→THORNS, VOID→RUNE, EARTH/METAL/POISON→CRACK, còn lại→MAGIC_CIRCLE) + hạt/light rải theo xác suất mỗi lần gọi, gọi mỗi frame suốt thời gian zone active.
- `VFX_ComposeSlashArc(VC_MaterialId, pos, dir, radius, arcDegrees, progress, time)` (`vc_slash.inl`): Vệt chém cận chiến dạng ribbon cong, mỏng ở đuôi/dày ở đỉnh sweep, chỉ hiện phần cung đã quét tới `progress`; glint ở mép đang chém. Màu = `body`.

> **Đã xóa 2026-07-10** (theo yêu cầu user, `vc_chain.inl`/`vc_charge.inl` bị xóa khỏi đĩa, không phải bug — file thật, đã dọn hết include/declaration/manifest liên quan): `VFX_ComposeChain` (chain/bounce archetype — dùng `VFX_ChainLightning` trong `vc_archetype.inl` cho chain lightning thay thế, không có thay thế trực tiếp cho chain nguyên tố khác) và `VFX_ComposeChargeUp` (charge/channel archetype — không có primitive thay thế, tự phối từ `VC_MotionSpiralIn` + core sphere đang lớn dần + `VFX_ComposeGlintBurst`, xem §0 "Charge (pre-ultimate)" ở trên).

Tất cả Nhóm 3-5 đã gắn sẵn vào tab **"NEW FX"** trong `sandbox/vfx_test.c` để xem trực quan — không cần viết skill thật mới xem được. Chạy `python3 scripts/sync_vfx_test.py` sau khi thêm/xóa `VFX_Compose*` để giữ tab đồng bộ.

---

## `.inl` include order (in `visual_composer.c`)
```
vc_common.inl     — render primitives (VC_DrawGroundQuadXZ, VC_DrawGroundRune)
vc_beauty.inl     — beauty primitives — MUST precede element .inl (vc_zone calls GlintBurst)
vc_preset.inl     — preset-driven: SmokePuff, SmokeTrail, LightningBolt, Impact, Cast, ProjectileTrail
vc_metal.inl / vc_wood.inl / vc_water.inl / vc_fire.inl / vc_earth.inl / vc_plasma.inl / vc_taiji.inl
vc_projectile.inl / vc_ground.inl / vc_beam.inl / vc_path.inl / vc_summon.inl / vc_explosion.inl / vc_aura.inl / vc_cylinder_aura.inl
vc_shield.inl / vc_zone.inl / vc_slash.inl
vc_elemental_mist.inl
vc_ground_aura.inl
vc_black_hole.inl
```

## Sync script
`scripts/vfx_test_manifest.json` is the source of truth for the NEWFX tab.
Run `python3 scripts/sync_vfx_test.py` after every `VFX_Compose*` add/remove.
`--check` flag for dry-run (no writes).
