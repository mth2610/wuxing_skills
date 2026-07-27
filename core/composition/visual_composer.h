#ifndef VISUAL_COMPOSER_H
#define VISUAL_COMPOSER_H

#include "raylib.h"
#include "core/skill_helper.h"                   // for EffectPresetType
#include "core/particle_system.h"                // for ParticleRadialBurstConfig
#include "core/composition/common/vc_motion.h"          // Motion Library (quỹ đạo/shaper thuần toán học)
#include "core/presets/vc_material.h"            // Element Material Table (VC_MaterialId — trục nguyên tố của mọi archetype)
#include "core/geometry/procedural_mesh_utils.h" // for GroundHeightSampleFn (VFX_ComposeGroundSmoke)

typedef struct
{
    /* --- Step 1: screen distortion --- */
    bool distortEnabled;
    float distortRadius, distortStrength, distortLife, distortSpeed;

    /* --- Step 2: ground decal --- */
    bool decalEnabled;
    Texture2D decalTex;
    float decalScale; /* multiplied by sizeScale at call time */
    float decalLife;
    Color decalTint;
    bool decalRandomRotation; /* true = GetRandomValue(0,360), false = use decalFixedRotation */
    float decalFixedRotation;

    /* --- Step 3: point light flash --- */
    bool lightEnabled;
    Color lightColor;
    float lightRadius; /* multiplied by sizeScale at call time */
    float lightLife;

    /* --- Step 4: radial particle burst --- */
    bool particlesEnabled;
    ParticleRadialBurstConfig particles;
} ImpactBurstConfig;

#define VFX_TriggerImpactBurst VFX_ComposeTriggerImpactBurst

// 1. Smoke / dust puff — Đợt E F2, rebuilt on the F1 lit-particle foundation
// (core/composition/common/vc_smoke_puff.inl). Layered alpha sprites with
// per-sprite spin that grow while they fade, deliberately dark so the lighting
// pass supplies the brightness. Draw with BLEND_ALPHA; a glowing puff is this
// plus a SECOND additive draw, never this one flipped to additive.
// `density` 0..1 scales the sprite count. Needs particle lighting on to look
// right: tuning.cfg → particle_lighting_strength / particle_scatter_strength.
void VFX_ComposeSmokePuff(Vector3 pos, VC_MaterialId matId, float scale, float density);

// 2. Spawning Smoke Trail
void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration);

// 3. Ground Fissure Streak — 3D V-groove crack mesh (ProceduralMesh_BuildFissure),
//    progress (0..1) reveals A→B, time drives the ember-seam pulse. Continuous
//    draw call, not a one-shot decal stamp.
void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width, float progress, float time);

// 4. Spawning Lightning Bolt (procedural ray-based crackle)
int VFX_ComposeLightningBolt(Vector3 start, Vector3 end, float scale);

// 4b. Energy Flow — smooth A→B channel, scrolling flow texture (mana stream,
//     power conduit). Not a fire-and-forget draw: registers a pool slot in
//     vc_archetype.inl, auto-expires after `duration`. See EnergyFlow_* in
//     core/vfx_proc_ray.h for the underlying mechanism.
int VFX_ComposeEnergyFlow(Vector3 from, Vector3 to, float scale, float duration);

// 5. Spawning Impact Effect (elemental: water, fire, wood, earth, metal, taiji)
void VFX_ComposeImpact(Vector3 pos, EffectPresetType preset, float scale);

// 6. Spawning Cast Effect (casting/windup elements)
void VFX_ComposeCast(Vector3 pos, EffectPresetType preset, float scale);

// 7. Spawning Projectile Trail
int VFX_ComposeProjectileTrail(Vector3 start, Vector3 target, EffectPresetType preset, float scale, float speed);

// 8. Triggering full generic 4-step Impact Burst (from impact_burst.h)
void VFX_ComposeTriggerImpactBurst(Vector3 pos, float sizeScale, const ImpactBurstConfig *cfg);

// 8b. Beauty primitives — reusable "polish" pieces (particle/decal/light
// only, no post-process pipeline — see CORE_ISSUES.md Item 35)
void VFX_ComposeShockwaveRing(Vector3 pos, float radius, float life, Color tint);
void VFX_ComposeGlintBurst(Vector3 pos, int count, float spread, Color tint);
void VFX_ComposeEmberDrift(Vector3 pos, float radius, int count, Color tint);
void VFX_ComposeStreakFlare(Vector3 pos, float scale, Color tint);

// 9. Procedural Visual Components (Mesh-based compositions)
void VFX_ComposeStonePillar(Vector3 basePos, float progress);
void VFX_ComposeBoulder(Vector3 pos);
void VFX_ComposeIceCrystal(Vector3 basePos, int seed); // cụm nhỏ 3 viên, ambient — build lại + rlBegin mỗi frame, chỉ dùng cho chi tiết phụ

// Cụm pha lê băng "hero burst" (nhiều viên, chi tiết cao — vd skill bắn ra
// 10 viên pha lê cùng lúc, cast dồn dập/nhiều nhân vật cùng lúc). Dùng 1
// mesh "viên mẫu" build đúng 1 lần duy nhất (vĩnh viễn, không build lại/
// không unload) rồi vẽ N viên bằng N lần DrawMesh với transform khác nhau
// (dịch/xoay/scale tính trên CPU) — KHÔNG gọi UploadMesh mỗi cast (tránh
// giật khung hình khi nhiều cast dồn vào cùng lúc, xem CORE_API.md mục
// "Crystal Cluster — GPU-resident mesh"). Không cần Build/Unload riêng ở
// phía skill — gọi thẳng hàm này mỗi frame trong lúc VFX còn sống.
// growProgress 0..1: 0 = chưa mọc, 1 = mọc đầy đủ (GPU shader lo, không tốn
// CPU). `seed` khác nhau mỗi lần cast (vd trộn GetTime()) → cụm khác nhau;
// cùng seed → cùng hình dạng (xác định, không phải bug).
void VFX_DrawIceCrystalBurst(Vector3 center, int crystalCount, int seed, float growProgress);
void VFX_ComposeMagicPuddle(Vector3 pos);
void VFX_ComposeFireball(Vector3 pos, float time);
void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time);
void VFX_BeginWaterStreams(float time);
void VFX_DrawWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time, float phaseOffset);
void VFX_EndWaterStreams(void);
void VFX_ComposeWaterStreamOnPath(const Vector3 *pathPoints, int pathCount, float radius, float progress, float segmentLengthRatio, float time);
void VFX_ComposeGlowingVine(Vector3 startPos, Vector3 targetPos, Vector3 p1, Vector3 p2, Vector3 contactPos, float progress, float time, float sizeScale, int branchIndex, int branchCount);
void VFX_ComposeFlameWisp(Vector3 pos, float time);
void VFX_ComposeFirePillar(Vector3 basePos, float progress);
void VFX_ComposeMetalShardCluster(Vector3 basePos, int seed);
void VFX_ComposeBladeRing(Vector3 pos, float radius, int bladeCount, float rotationDeg);
// Plasma energy orb — wispy noise membrane + hot core + pink interior filament
// arcs. Continuous: call once per frame with a running `time`.
void VFX_ComposePlasmaOrb(Vector3 pos, float radius, float time);
// Wood ambience set — glowing leaves/petals/pollen as particle flows.
// LeafSwirl/LeafFall are continuous (call per frame); BloomBurst is one-shot.
void VFX_ComposeLeafSwirl(Vector3 pos, float radius, float time);
void VFX_ComposeBloomBurst(Vector3 pos, float scale);
void VFX_ComposeLeafFall(Vector3 pos, float radius, float time);
// Metal skill set — BladeStorm is continuous (orbiting blades around caster);
// ShrapnelBurst (fragment explosion) and RicochetSpark (directional parry/
// deflect spark fan along `dir`) are one-shot.
void VFX_ComposeBladeStorm(Vector3 pos, float radius, float time);
void VFX_ComposeShrapnelBurst(Vector3 pos, float scale);
void VFX_ComposeRicochetSpark(Vector3 pos, Vector3 dir, float scale);
// Water skill set — SplashBurst is one-shot (crown splash + rings);
// BubbleStream (rising bubbles that pop) and MistVeil (low fog bank) are
// continuous.
void VFX_ComposeSplashBurst(Vector3 pos, float scale);
void VFX_ComposeBubbleStream(Vector3 pos, float radius, float time);
void VFX_ComposeMistVeil(Vector3 pos, float radius, float time);
// Taiji element set (wind/storm/static/yin-yang) — GustSlash is one-shot
// (directional wind blade along `dir`); Cyclone, StaticField and YinYangOrbit
// are continuous.
void VFX_ComposeGustSlash(Vector3 pos, Vector3 dir, float scale);
void VFX_ComposeCyclone(Vector3 pos, float radius, float time);
void VFX_ComposeStaticField(Vector3 pos, float radius, float time);
void VFX_ComposeYinYangOrbit(Vector3 pos, float radius, float time);
// Earth skill set — RockBurst is one-shot (debris + dust + shake);
// FloatingStones (levitating rocks around caster) and QuakeRumble (trembling
// zone) are continuous.
void VFX_ComposeRockBurst(Vector3 pos, float scale);
void VFX_ComposeFloatingStones(Vector3 pos, float radius, float time);
struct MeshAdjacency;

void VFX_ComposeQuakeRumble(Vector3 pos, float radius, float time);
void VFX_ComposeParticleUpgradesTest(Vector3 pos);
void VFX_ComposeTrailUpgradesTest(Vector3 pos);

// Mesh Adjacency Electricity Composition APIs
struct ForceField;
int VFX_SpawnMeshElectricity(const struct MeshAdjacency *adj, Color color, float duration, const struct ForceField *forceField);
void VFX_UpdateMeshElectricity(int handle, Matrix transform);
void VFX_KillMeshElectricity(int handle);
void VFX_ComposeMeshElectricity(Vector3 position, Color color, float duration);
void ComposeMeshElectricityEx(Vector3 position, Color color, float duration, const struct ForceField *forceField);
// Fire skill set (Phase 2) — all continuous: FlameBreath is a directional
// flamethrower cone along `dir`; BurningGround an ignited patch; FireWhirl a
// fire tornado.
void VFX_ComposeFlameBreath(Vector3 pos, Vector3 dir, float scale, float time);
void VFX_ComposeBurningGround(Vector3 pos, float radius, float time);
void VFX_ComposeFireWhirl(Vector3 pos, float radius, float time);
void VFX_ComposeFireFunnel(Vector3 pos, float bottomRadius, float topRadius, float height, float time);
// Elemental dry-ice mist — thin, cold, ground-hugging vapor that radiates
// outward from a point (like dry ice sublimation). Continuous; call once per
// frame. Colors and glow come from VFX_Material(matId) — available for all
// elements, same structure different palette.
void VFX_ComposeElementalMist(VC_MaterialId matId, Vector3 pos, float radius, float time);
void VFX_ComposePathMistWave(VC_MaterialId matId, const Vector3 *pathPoints, int pathCount, float progress, float radius);

// 10. High-level Archetypes
// Trục nguyên tố của mọi archetype là VC_MaterialId (core/presets/vfx_presets.h).
// Hai enum dưới đây là trục HÌNH DẠNG (không phải nguyên tố) nên giữ riêng.
typedef enum
{
    GROUND_CRACK_RADIAL,
    GROUND_CRACK_LINE,
    GROUND_MAGIC_CIRCLE,
    GROUND_LAVA,
    GROUND_FROST,
    GROUND_THORNS,
    GROUND_RUNE
} GroundPatternStyle;

typedef enum
{
    PATH_THORNS,
    PATH_STONE_PILLAR,
    PATH_ICE_SPIKE,
    PATH_FIRE_ERUPTION,
    PATH_LIGHTNING_CHAIN
} PathStyle;

// Mọi archetype nhận VC_MaterialId — 12 material dùng được cho tất cả
// (material không có biến thể cấu trúc riêng rơi về nhánh generic).
// Quy ước slot: body = shell/ribbon/rune, glow = beam/điểm nóng, soft = aura/light;
// ngoại lệ nhỏ per-archetype (vd. lightning aura dùng body tím) có comment tại chỗ.
void VFX_ComposeProjectile(VC_MaterialId matId, Vector3 pos, Vector3 target, float progress, float scale, float time);
void VFX_GroundPattern(GroundPatternStyle style, Vector3 pos, float radius, float progress, float time);
void VFX_ComposeBeam(VC_MaterialId matId, Vector3 start, Vector3 end, float width, float progress, float time);
void VFX_PathWave(PathStyle style, const Vector3 *points, int count, float scale, float progress, float time, int seed);
void VFX_SummonCircle(Vector3 pos, float radius, float progress, float time, Color color);
void VFX_TriggerExplosion(VC_MaterialId matId, Vector3 pos, float scale, bool cameraShake);
void VFX_ComposeShield(VC_MaterialId matId, Vector3 pos, float radius, float progress, float time);
void VFX_ComposeZone(VC_MaterialId matId, Vector3 pos, float radius, float progress, float time);
void VFX_ComposeSlashArc(VC_MaterialId matId, Vector3 pos, Vector3 dir, float radius, float arcDegrees, float progress, float time);

// ── Stateful archetype VFX (pools managed by VFX_Compose_Update / VFX_Compose_Draw3D) ──

// Drive all archetype pools — call once per frame (replaces SkillHelper_Update).
void VFX_Compose_Update(float dt);
// Draw 3D archetype elements — call inside BeginMode3D (replaces SkillBuilder_DrawWorld).
void VFX_Compose_Draw3D(Camera3D cam);

// ProcRay beam with element-tinted glow at both endpoints. Returns handle or -1.
int VFX_SpawnProcBeam(Vector3 from, Vector3 to, EffectPresetType element, float width, float duration);
void VFX_KillProcBeam(int handle);

// Expanding ground shockwave ring that travels outward at `speed` m/s up to `range` m.
void VFX_SpawnGroundWave(Vector3 origin, Vector3 dir, EffectPresetType element, float range, float speed);

// N glowing orbs orbiting `center` at `radius` for `duration` seconds. Returns handle or -1.
int VFX_SpawnOrbitals(Vector3 center, EffectPresetType element, int count, float radius, float duration);

// Ring of 8 particle emitters + center VFXLight. Returns handle or -1. Kill explicitly or let duration expire.
int VFX_SpawnAuraRing(Vector3 center, EffectPresetType element, float radius, float duration);
void VFX_KillAuraRing(int handle);

// Long rising smoke column from a fixed base point (cigarette-smoke style).
// duration <= 0 = keeps looping until VFX_KillSmokeColumn; > 0 = auto-kills
// after that many seconds. Returns handle or -1.
int VFX_SpawnSmokeColumn(Vector3 pos, float duration);
void VFX_KillSmokeColumn(int handle);

// Staggered lightning bolts along a hop chain (use SkillHelper_ChainTargets to build `points`).
void VFX_ChainLightning(const Vector3 *points, int count, float scale, float hopDelay);

// Aura dạng "Cột màng năng lượng" (Hình trụ không nắp)
// Phù hợp cho các chiêu thức buff giáp, hộ thể.
void VFX_ComposeCylinderAura(VC_MaterialId matId, Vector3 pos, float radius, float progress, float time);

// Meditation qi-gather — soft ground runes + rising wisps + spiral-in motes.
// Call once per frame while meditating. progress 0..1 = channel fraction
// (0 = start, 1 = finishing). Pure visual component; no gameplay side effects.

// Batch rendering helpers for high-performance visual composition
void VFX_BeginEnergySmokeBatch(void);
void VFX_EndEnergySmokeBatch(void);
void VFX_BeginMagicFilamentsBatch(void);
void VFX_EndMagicFilamentsBatch(void);
void VFX_BeginSmokeColumnBatch(void);
void VFX_EndSmokeColumnBatch(void);

void VFX_ComposeMagicFilamentsOnPlane(Vector3 center, Vector3 normal, float scale, float progress, Color color, float thickness, float frequency, float speed, Vector2 sourceUV);
// E5.1 — anisotropic star glints over a Fibonacci point cloud (the holy/faith
// signature). Continuous: call once per frame with a running `time`. `scale` is
// the cloud radius in metres. Additive + unlit per the blend law. Needs no
// asset: falls back to a generated 4-point star if glint_star_4pt.png is absent.
void VFX_ComposeGlintSparkle(Vector3 center, VC_MaterialId mat, float scale, float time);

void VFX_ComposeShardDebris(Vector3 pos, int count, float speed, VC_MaterialId matId);
void VFX_ComposeCrownSplash(Vector3 pos, float radius, float height, float duration, VC_MaterialId matId);

// F4 — Character aura. Three layers: discrete motes crossing the silhouette
// (the layer that actually reads as an aura), a breathing shell + ground
// contact, and a real VFXLight tracking the agent so the character is lit BY
// their own aura (needs E2). Attaches to an agent and follows it via
// SkillManager_GetAgentPos.
//
// Long-lived and per-agent, so unlike a fire-and-forget composition it must be
// released: call VFX_KillCharacterAura on cleanse/death. It also self-releases
// when the agent stops resolving (despawn), so a missed Kill costs one slot
// until then rather than leaking forever. Re-attaching to an agent that already
// has one RETUNES it instead of stacking a second.
// Returns a handle, or the recycled slot's handle when the pool (8) is full.
int  VFX_ComposeCharacterAura(int agentId, VC_MaterialId matId, float intensity);
void VFX_AuraSetIntensity(int handle, float intensity01); // ramped, never popped
void VFX_KillCharacterAura(int handle);

// @gen:vc_declarations begin
void VFX_ComposeBlackHole(VC_MaterialId matId, Vector3 pos, float radius, float time);
void VFX_ComposeChainLink(VC_MaterialId matId, Vector3 start, Vector3 end, float width, float sag, float progress, float time);
void VFX_ComposeChargeConverge(Vector3 center, VC_MaterialId mat, float radius, float t01, int moteCount);
void VFX_ComposeEnergySmoke(Vector3 pos, float scale, float progress, float time, Vector2 sourceUV);
void VFX_ComposeFlameVolume(Vector3 pos, VC_MaterialId matId, float scale, float intensity);
void VFX_ComposeGroundAura(VC_MaterialId matId, Vector3 pos, float radius, float scrollSpeed, float time);
void VFX_ComposeGroundSmoke(Vector3 center, float halfSize, float progress, GroundHeightSampleFn heightFn, void *userData);
void VFX_ComposeMagicFilaments(Vector3 pos, float scale, float progress, Color color, float thickness, float frequency, float speed, Vector2 sourceUV);
void VFX_ComposeRuneCircle(Vector3 center, Vector3 normal, VC_MaterialId mat, float radius, float t01, int ringCount);
void VFX_ComposeSmokeColumnFX(Vector3 base, float halfWidth, float height, float progress, int planeCount);
void VFX_ComposeSmokeOnPlane(Vector3 center, Vector3 normal, float halfSize, float progress, Color color);
void VFX_ComposeTaijiArcStrike(Vector3 pos, float scale);
void VFX_ComposeTornado(VC_MaterialId matId, Vector3 pos, float radius, float height, float time);
void VFX_ReleaseTornado(Vector3 pos);
// @gen:vc_declarations end
void VFX_ComposePathLink(VC_MaterialId matId, const Vector3 *points, int count, float width, float progress, float time);
#endif // VISUAL_COMPOSER_H
