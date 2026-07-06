#ifndef VISUAL_COMPOSER_H
#define VISUAL_COMPOSER_H

#include "raylib.h"
#include "core/skill_helper.h"          // for EffectPresetType
#include "core/particle_system.h"       // for ParticleRadialBurstConfig
#include "core/composition/vc_motion.h" // Motion Library (quỹ đạo/shaper thuần toán học)

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

// 1. Spawning Smoke Puff
void VFX_ComposeSmokePuff(Vector3 pos, float size);

// 2. Spawning Smoke Trail
void VFX_ComposeSmokeTrail(Vector3 start, Vector3 end, float duration);

// 3. Spawning Ground Fissure Streak (instantly draws decals along path)
void VFX_ComposeFissureStreak(Vector3 start, Vector3 end, float width);

// 4. Spawning Lightning Bolt (procedural ray-based crackle)
int VFX_ComposeLightningBolt(Vector3 start, Vector3 end, float scale);

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
void VFX_ComposeIceCrystal(Vector3 basePos, int seed);
void VFX_ComposeMagicPuddle(Vector3 pos);
void VFX_ComposeFireball(Vector3 pos, float time);
void VFX_ComposeWaterStream(Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float radius, float progress, float time);
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
void VFX_ComposeQuakeRumble(Vector3 pos, float radius, float time);
// Fire skill set (Phase 2) — all continuous: FlameBreath is a directional
// flamethrower cone along `dir`; BurningGround an ignited patch; FireWhirl a
// fire tornado.
void VFX_ComposeFlameBreath(Vector3 pos, Vector3 dir, float scale, float time);
void VFX_ComposeBurningGround(Vector3 pos, float radius, float time);
void VFX_ComposeFireWhirl(Vector3 pos, float radius, float time);

// 10. High-level Archetypes & Styles
typedef enum
{
    PROJECTILE_FIREBALL,
    PROJECTILE_ICE,
    PROJECTILE_LIGHTNING,
    PROJECTILE_WOOD_SEED,
    PROJECTILE_ROCK,
    PROJECTILE_YINYANG
} ProjectileStyle;

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
    BEAM_FIRE,
    BEAM_LIGHTNING,
    BEAM_ICE,
    BEAM_HOLY,
    BEAM_VOID
} BeamStyle;

typedef enum
{
    PATH_THORNS,
    PATH_STONE_PILLAR,
    PATH_ICE_SPIKE,
    PATH_FIRE_ERUPTION,
    PATH_LIGHTNING_CHAIN
} PathStyle;

typedef enum
{
    EXP_FIRE,
    EXP_ICE,
    EXP_LIGHTNING,
    EXP_EARTH,
    EXP_POISON,
    EXP_HOLY,
    EXP_VOID
} ExplosionStyle;

typedef enum
{
    AURA_FIRE,
    AURA_ICE,
    AURA_WIND,
    AURA_LIGHTNING,
    AURA_TAIJI,
    AURA_QI
} AuraStyle;

typedef enum
{
    QI_ICE,
    QI_FIRE,
    QI_LIGHTNING,
    QI_WOOD,
    QI_XIANXIA
} QiStyle;

// 11. Phase 3 archetypes — shield/chain/zone/slash/charge
typedef enum
{
    SHIELD_METAL,
    SHIELD_WOOD,
    SHIELD_WATER,
    SHIELD_EARTH,
    SHIELD_TAIJI
} ShieldStyle;

typedef enum
{
    CHAIN_LIGHTNING,
    CHAIN_VINE,
    CHAIN_WATER,
    CHAIN_FIRE,
    CHAIN_TAIJI
} ChainStyle;

typedef enum
{
    ZONE_LAVA,
    ZONE_FROST,
    ZONE_POISON,
    ZONE_HOLY,
    ZONE_VOID
} ZoneStyle;

typedef enum
{
    SLASH_METAL,
    SLASH_WOOD,
    SLASH_FIRE,
    SLASH_ICE,
    SLASH_EARTH
} SlashStyle;

typedef enum
{
    CHARGE_FIRE,
    CHARGE_METAL,
    CHARGE_WATER,
    CHARGE_WOOD,
    CHARGE_EARTH,
    CHARGE_TAIJI
} ChargeStyle;

void VFX_ComposeProjectile(ProjectileStyle style, Vector3 pos, Vector3 target, float progress, float scale, float time);
void VFX_GroundPattern(GroundPatternStyle style, Vector3 pos, float radius, float progress, float time);
void VFX_ComposeBeam(BeamStyle style, Vector3 start, Vector3 end, float width, float progress, float time);
void VFX_PathWave(PathStyle style, const Vector3 *points, int count, float scale, float progress, float time);
void VFX_SummonCircle(Vector3 pos, float radius, float progress, float time, Color color);
void VFX_TriggerExplosion(ExplosionStyle style, Vector3 pos, float scale, bool cameraShake);
void VFX_ComposeAura(AuraStyle style, Vector3 pos, float radius, float time);
void VFX_ComposeShield(ShieldStyle style, Vector3 pos, float radius, float progress, float time);
void VFX_ComposeChain(ChainStyle style, const Vector3 *targets, int count, float progress, float time);
void VFX_ComposeZone(ZoneStyle style, Vector3 pos, float radius, float progress, float time);
void VFX_ComposeSlashArc(SlashStyle style, Vector3 pos, Vector3 dir, float radius, float arcDegrees, float progress, float time);
void VFX_ComposeChargeUp(ChargeStyle style, Vector3 pos, float radius, float progress, float time);
void VFX_ComposeQiAura(QiStyle style, Vector3 casterPos, float progress, float time, float radius);
void VFX_AttachQiAura(int casterAgentId, Vector3 anchorPos, float bodyHeight, EffectPresetType element, float scale, int wispCount);
void VFX_DetachQiAura(int casterAgentId);
void VFX_UpdateQiAuras(float dt);

#endif // VISUAL_COMPOSER_H
